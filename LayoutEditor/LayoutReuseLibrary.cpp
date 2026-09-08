#include "AYUI/LayoutEditor/LayoutReuseLibrary.h"

#include "AYUI/WidgetSerializer.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace ayt::ui {

using nlohmann::json;

namespace {

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

} // namespace

bool LayoutReuseLibrary::validateName(const std::string& name,
                                      std::string* error) {
    if (name.empty()) return fail(error, "Reusable block name cannot be empty");
    if (name.size() > 64u)
        return fail(error, "Reusable block name is limited to 64 bytes");
    const auto first = static_cast<unsigned char>(name.front());
    if (!(std::isalpha(first) || first == '_')) {
        return fail(error,
                    "Reusable block name must start with a letter or underscore");
    }
    for (const unsigned char ch : name) {
        if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == ' ')) {
            return fail(error,
                        "Reusable block name uses an unsupported character");
        }
    }
    return true;
}

bool LayoutReuseLibrary::define(const std::string& name, Widget* widget,
                                std::string* error) {
    if (widget == nullptr) return fail(error, "No widget selected");
    return defineJson(name, WidgetSerializer::serializeWidget(widget), error);
}

bool LayoutReuseLibrary::defineJson(const std::string& name,
                                    const std::string& widgetJson,
                                    std::string* error) {
    if (!validateName(name, error)) return false;
    try {
        const json value = json::parse(widgetJson);
        if (!value.is_object() || !value.contains("type")) {
            return fail(error, "Reusable block must contain one Widget object");
        }
        const std::string normalized = value.dump();
        auto found = std::lower_bound(
            _blocks.begin(), _blocks.end(), name,
            [](const LayoutReusableBlock& block, const std::string& candidate) {
                return block.name < candidate;
            });
        if (found != _blocks.end() && found->name == name) {
            found->widgetJson = normalized;
        } else {
            _blocks.insert(found, LayoutReusableBlock{name, normalized});
        }
        return true;
    } catch (const std::exception& ex) {
        return fail(error, std::string("Invalid reusable block JSON: ") + ex.what());
    }
}

bool LayoutReuseLibrary::remove(const std::string& name) {
    const auto found = std::find_if(
        _blocks.begin(), _blocks.end(),
        [&name](const LayoutReusableBlock& block) {
            return block.name == name;
        });
    if (found == _blocks.end()) return false;
    _blocks.erase(found);
    return true;
}

const LayoutReusableBlock* LayoutReuseLibrary::find(
    const std::string& name) const {
    const auto found = std::lower_bound(
        _blocks.begin(), _blocks.end(), name,
        [](const LayoutReusableBlock& block, const std::string& candidate) {
            return block.name < candidate;
        });
    return found != _blocks.end() && found->name == name ? &*found : nullptr;
}

Widget* LayoutReuseLibrary::instantiate(const std::string& name) const {
    const LayoutReusableBlock* block = find(name);
    return block != nullptr
        ? WidgetSerializer::deserialize(block->widgetJson) : nullptr;
}

std::string LayoutReuseLibrary::encodeDocument(Widget* root,
                                               bool pretty) const {
    if (root == nullptr) return {};
    const json rootJson = json::parse(WidgetSerializer::serialize(root, false));
    if (_blocks.empty()) return pretty ? rootJson.dump(4) : rootJson.dump();

    json reusable = json::object();
    for (const LayoutReusableBlock& block : _blocks) {
        reusable[block.name] = json::parse(block.widgetJson);
    }
    json document = {
        {"format", "AYUILayout"},
        {"version", 2},
        {"reusable", std::move(reusable)},
        {"root", rootJson}
    };
    return pretty ? document.dump(4) : document.dump();
}

bool LayoutReuseLibrary::decodeDocument(const std::string& documentJson,
                                        std::string& outRootJson,
                                        std::string* error) {
    try {
        const json document = json::parse(documentJson);
        if (!document.is_object())
            return fail(error, "Layout document root must be an object");

        if (document.contains("type") || !document.contains("root")) {
            _blocks.clear();
            outRootJson = document.dump();
            return true;
        }
        if (!document["root"].is_object())
            return fail(error, "Layout document root field must be a Widget object");

        LayoutReuseLibrary decoded;
        if (document.contains("reusable")) {
            if (!document["reusable"].is_object())
                return fail(error, "Layout reusable field must be an object");
            if (document["reusable"].size() > 256u)
                return fail(error, "Layout contains too many reusable blocks");
            for (auto it = document["reusable"].begin();
                 it != document["reusable"].end(); ++it) {
                if (!it.value().is_object() ||
                    !decoded.defineJson(it.key(), it.value().dump(), error)) {
                    return false;
                }
            }
        }
        _blocks = std::move(decoded._blocks);
        outRootJson = document["root"].dump();
        return true;
    } catch (const std::exception& ex) {
        return fail(error, std::string("Invalid layout document JSON: ") + ex.what());
    }
}

} // namespace ayt::ui
