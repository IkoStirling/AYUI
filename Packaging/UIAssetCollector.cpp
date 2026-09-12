#include "AYUI/Packaging/UIAssetCollector.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace ayt::ui::packaging {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;
using AssetIdentity = std::pair<UIAssetKind, std::string>;

constexpr std::uintmax_t kMaximumLayoutBytes = 16u * 1024u * 1024u;

const char* kindName(UIAssetKind kind) {
    switch (kind) {
    case UIAssetKind::Layout: return "layout";
    case UIAssetKind::Texture: return "texture";
    }
    return "unknown";
}

const char* issueName(UIAssetCollectionIssueCode code) {
    switch (code) {
    case UIAssetCollectionIssueCode::MissingContentRoot:
        return "missingContentRoot";
    case UIAssetCollectionIssueCode::NonPortableAsset:
        return "nonPortableAsset";
    case UIAssetCollectionIssueCode::EscapingAsset:
        return "escapingAsset";
    case UIAssetCollectionIssueCode::MissingAsset: return "missingAsset";
    case UIAssetCollectionIssueCode::AssetTooLarge: return "assetTooLarge";
    case UIAssetCollectionIssueCode::InvalidJson: return "invalidJson";
    case UIAssetCollectionIssueCode::InvalidLayout: return "invalidLayout";
    case UIAssetCollectionIssueCode::InvalidResourceReference:
        return "invalidResourceReference";
    case UIAssetCollectionIssueCode::UnresolvedResource:
        return "unresolvedResource";
    }
    return "unknown";
}

std::string portable(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

bool hasUriScheme(std::string_view value) {
    const std::size_t colon = value.find(':');
    const std::size_t slash = value.find('/');
    return colon != std::string_view::npos
        && (slash == std::string_view::npos || colon < slash);
}

bool absoluteLike(std::string_view value) {
    return !value.empty()
        && (value.front() == '/' || value.front() == '\\'
            || (value.size() > 1u && value[1] == ':')
            || hasUriScheme(value));
}

bool escapesRoot(std::string_view value) {
    const fs::path normalized = fs::path(value).lexically_normal();
    const auto first = normalized.begin();
    return first != normalized.end() && *first == "..";
}

bool pathEscapesRoot(const fs::path& root, const fs::path& candidate) {
    const fs::path relative = candidate.lexically_relative(root);
    if (relative.empty()) return candidate != root;
    const auto first = relative.begin();
    return first != relative.end() && *first == "..";
}

std::string normalizeAsset(std::string value) {
    if (value.empty()) return {};
    value = portable(std::move(value));
    return fs::path(value).lexically_normal().generic_string();
}

void addIssue(UIAssetCollectionResult& result,
              UIAssetCollectionIssueCode code,
              std::string asset,
              std::string consumer,
              std::string message) {
    result.issues.push_back(
        {code, std::move(asset), std::move(consumer), std::move(message)});
}

std::string widgetConsumer(const std::string& layoutAsset,
                           const json& widget,
                           const std::string& jsonPath) {
    const auto id = widget.find("id");
    if (id != widget.end() && id->is_string() && !id->empty()) {
        return layoutAsset + "#" + id->get<std::string>();
    }
    return layoutAsset + "#" + jsonPath;
}

void collectWidgetResources(const json& value,
                            const std::string& layoutAsset,
                            const std::string& jsonPath,
                            std::vector<std::pair<std::string, std::string>>&
                                textures,
                            UIAssetCollectionResult& result) {
    if (value.is_object()) {
        const auto type = value.find("type");
        if (type != value.end() && type->is_string()
            && type->get_ref<const std::string&>() == "Image") {
            const auto texture = value.find("textureName");
            if (texture != value.end()) {
                const std::string consumer = widgetConsumer(
                    layoutAsset, value, jsonPath);
                if (!texture->is_string()) {
                    addIssue(result,
                        UIAssetCollectionIssueCode::InvalidResourceReference,
                        {}, consumer,
                        "Image.textureName must be a string.");
                } else if (!texture->get_ref<const std::string&>().empty()) {
                    textures.emplace_back(
                        texture->get<std::string>(), consumer);
                }
            }
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            collectWidgetResources(it.value(), layoutAsset,
                jsonPath + "." + it.key(), textures, result);
        }
    } else if (value.is_array()) {
        for (std::size_t index = 0; index < value.size(); ++index) {
            collectWidgetResources(value[index], layoutAsset,
                jsonPath + "[" + std::to_string(index) + "]",
                textures, result);
        }
    }
}

bool isLayoutDocument(const json& document) {
    if (!document.is_object()) return false;
    const auto root = document.find("root");
    const json* widget = root != document.end() ? &*root : &document;
    const auto type = widget->find("type");
    return widget->is_object() && type != widget->end() && type->is_string();
}

} // namespace

bool UIAssetCollector::accepts(std::string_view asset) const noexcept {
    constexpr std::string_view suffix = ".ui.json";
    if (asset.size() < suffix.size()) return false;
    const std::size_t offset = asset.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        const unsigned char actual = static_cast<unsigned char>(
            asset[offset + index]);
        if (static_cast<char>(std::tolower(actual)) != suffix[index]) {
            return false;
        }
    }
    return true;
}

UIAssetCollectionResult UIAssetCollector::collect(
    const UIAssetCollectionRequest& request) const {
    UIAssetCollectionResult result;
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(request.contentRoot, ec);
    if (request.contentRoot.empty() || ec || !fs::is_directory(root, ec)) {
        addIssue(result, UIAssetCollectionIssueCode::MissingContentRoot,
            request.contentRoot, {},
            "AYUI package collection requires an existing content root.");
        return result;
    }

    std::map<AssetIdentity, UIAssetContribution> closure;
    const auto contribute = [&](UIAssetKind kind,
                                const std::string& packageKey,
                                const std::string& sourceAsset,
                                const std::string& consumer) {
        const AssetIdentity identity{kind, packageKey};
        auto [it, inserted] = closure.try_emplace(identity);
        UIAssetContribution& contribution = it->second;
        if (inserted) {
            contribution.kind = kind;
            contribution.packageKey = packageKey;
            contribution.sourceAsset = sourceAsset;
        }
        contribution.consumers.push_back(consumer);
    };

    const auto validateSource = [&](UIAssetKind kind,
                                    const std::string& packageKey,
                                    const std::string& sourceAsset,
                                    const std::string& consumer) -> bool {
        if (sourceAsset.empty()) {
            addIssue(result, UIAssetCollectionIssueCode::UnresolvedResource,
                packageKey, consumer,
                "AYUI could not resolve the resource to a source asset.");
            return false;
        }
        if (absoluteLike(sourceAsset)) {
            addIssue(result, UIAssetCollectionIssueCode::NonPortableAsset,
                sourceAsset, consumer,
                "AYUI package assets must be relative to the content root.");
            return false;
        }
        if (escapesRoot(sourceAsset)) {
            addIssue(result, UIAssetCollectionIssueCode::EscapingAsset,
                sourceAsset, consumer,
                "AYUI package asset escapes the content root.");
            return false;
        }
        const std::string normalized = normalizeAsset(sourceAsset);
        const fs::path candidate = (root / normalized).lexically_normal();
        const bool sourceExists = fs::is_regular_file(candidate, ec);
        if (request.requireSourceFiles && (!sourceExists || ec)) {
            ec.clear();
            addIssue(result, UIAssetCollectionIssueCode::MissingAsset,
                normalized, consumer,
                "AYUI package source asset does not exist.");
            return false;
        }
        ec.clear();
        if (sourceExists) {
            const fs::path resolved = fs::weakly_canonical(candidate, ec);
            if (ec || pathEscapesRoot(root, resolved)) {
                ec.clear();
                addIssue(result, UIAssetCollectionIssueCode::EscapingAsset,
                    sourceAsset, consumer,
                    "AYUI package asset resolves outside the content root.");
                return false;
            }
        }
        contribute(kind, packageKey, normalized, consumer);
        return true;
    };

    std::vector<std::string> entries = request.entryLayouts;
    for (std::string& entry : entries) entry = normalizeAsset(std::move(entry));
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());

    for (const std::string& entry : entries) {
        if (entry.empty() || absoluteLike(entry)) {
            addIssue(result, UIAssetCollectionIssueCode::NonPortableAsset,
                entry, "$entry",
                "UI layout entry must be a portable content-relative path.");
            continue;
        }
        if (escapesRoot(entry)) {
            addIssue(result, UIAssetCollectionIssueCode::EscapingAsset,
                entry, "$entry",
                "UI layout entry escapes the content root.");
            continue;
        }
        if (!accepts(entry)) {
            addIssue(result, UIAssetCollectionIssueCode::InvalidLayout,
                entry, "$entry", "AYUI only accepts *.ui.json layouts.");
            continue;
        }

        const fs::path candidate = (root / entry).lexically_normal();
        if (!fs::is_regular_file(candidate, ec) || ec) {
            ec.clear();
            addIssue(result, UIAssetCollectionIssueCode::MissingAsset,
                entry, "$entry", "UI layout entry does not exist.");
            continue;
        }
        const fs::path resolved = fs::weakly_canonical(candidate, ec);
        if (ec || pathEscapesRoot(root, resolved)) {
            ec.clear();
            addIssue(result, UIAssetCollectionIssueCode::EscapingAsset,
                entry, "$entry",
                "UI layout entry resolves outside the content root.");
            continue;
        }
        const std::uintmax_t byteCount = fs::file_size(resolved, ec);
        if (ec || byteCount > kMaximumLayoutBytes) {
            ec.clear();
            addIssue(result, UIAssetCollectionIssueCode::AssetTooLarge,
                entry, "$entry", "UI layout exceeds the 16 MiB package limit.");
            continue;
        }

        std::ifstream input(resolved, std::ios::binary);
        json document;
        try {
            input >> document;
        } catch (const std::exception& exception) {
            addIssue(result, UIAssetCollectionIssueCode::InvalidJson,
                entry, "$entry",
                std::string("UI layout JSON is invalid: ") + exception.what());
            continue;
        }
        if (!isLayoutDocument(document)) {
            addIssue(result, UIAssetCollectionIssueCode::InvalidLayout,
                entry, "$entry",
                "UI layout must contain a root Widget with a type.");
            continue;
        }

        contribute(UIAssetKind::Layout, entry, entry, "$entry");
        std::vector<std::pair<std::string, std::string>> textures;
        collectWidgetResources(document, entry, "$", textures, result);
        for (const auto& [originalKey, consumer] : textures) {
            const std::string packageKey = portable(originalKey);
            if (absoluteLike(packageKey)) {
                addIssue(result,
                    UIAssetCollectionIssueCode::NonPortableAsset,
                    packageKey, consumer,
                    "Image.textureName must use a portable package key.");
                continue;
            }
            if (escapesRoot(packageKey)) {
                addIssue(result, UIAssetCollectionIssueCode::EscapingAsset,
                    packageKey, consumer,
                    "Image.textureName escapes the package root.");
                continue;
            }
            const std::string normalizedKey = normalizeAsset(packageKey);
            const std::string source = request.sourceResolver
                ? request.sourceResolver(UIAssetKind::Texture, normalizedKey)
                : normalizedKey;
            validateSource(UIAssetKind::Texture, normalizedKey,
                           source, consumer);
        }
    }

    result.assets.reserve(closure.size());
    for (auto& [_, contribution] : closure) {
        std::sort(contribution.consumers.begin(),
                  contribution.consumers.end());
        contribution.consumers.erase(
            std::unique(contribution.consumers.begin(),
                        contribution.consumers.end()),
            contribution.consumers.end());
        result.assets.push_back(std::move(contribution));
    }
    std::sort(result.issues.begin(), result.issues.end(),
        [](const UIAssetCollectionIssue& left,
           const UIAssetCollectionIssue& right) {
            if (left.asset != right.asset) return left.asset < right.asset;
            if (left.consumer != right.consumer) {
                return left.consumer < right.consumer;
            }
            return left.code < right.code;
        });
    return result;
}

std::string UIAssetCollectionResult::serializeManifest(bool pretty) const {
    json document = {
        {"format", "AYUIModulePackageContribution"},
        {"version", 1},
        {"module", UIAssetCollector::moduleId()},
        {"ready", ready()},
        {"assets", json::array()},
        {"issues", json::array()},
    };
    for (const UIAssetContribution& asset : assets) {
        document["assets"].push_back({
            {"kind", kindName(asset.kind)},
            {"packageKey", asset.packageKey},
            {"sourceAsset", asset.sourceAsset},
            {"consumers", asset.consumers},
        });
    }
    for (const UIAssetCollectionIssue& issue : issues) {
        document["issues"].push_back({
            {"code", issueName(issue.code)},
            {"asset", issue.asset},
            {"consumer", issue.consumer},
            {"message", issue.message},
        });
    }
    return document.dump(pretty ? 2 : -1);
}

} // namespace ayt::ui::packaging
