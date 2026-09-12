#include "AYUI/LayoutEditor/LayoutThemeEditorModel.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace ayt::ui {

using nlohmann::json;

namespace {

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

bool validKey(const std::string& key) {
    if (key.empty() || key.front() == '.' || key.back() == '.') return false;
    return key.find("..") == std::string::npos;
}

std::vector<std::string> split(const std::string& key) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin <= key.size()) {
        const std::size_t end = key.find('.', begin);
        parts.push_back(key.substr(begin, end == std::string::npos
            ? std::string::npos : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1u;
    }
    return parts;
}

std::vector<std::string> tokenPath(const std::string& key) {
    if (key.rfind("color.", 0) == 0 || key.rfind("space.", 0) == 0) {
        return split(key);
    }
    return {key};
}

json* findToken(json& root, const std::string& key, bool create) {
    std::vector<std::string> parts = tokenPath(key);
    if (parts.empty()) return nullptr;
    json* current = &root["tokens"];
    for (const std::string& part : parts) {
        if (!current->is_object()) {
            if (!create) return nullptr;
            *current = json::object();
        }
        if (!create && !current->contains(part)) return nullptr;
        current = &(*current)[part];
    }
    return current;
}

const json* findToken(const json& root, const std::string& key) {
    std::vector<std::string> parts = tokenPath(key);
    if (parts.empty() || !root.contains("tokens")) return nullptr;
    const json* current = &root.at("tokens");
    for (const std::string& part : parts) {
        if (!current->is_object() || !current->contains(part)) return nullptr;
        current = &current->at(part);
    }
    return current;
}

bool eraseToken(json& root, const std::string& key) {
    std::vector<std::string> parts = tokenPath(key);
    if (parts.empty() || !root.contains("tokens")) return false;
    json* parent = &root["tokens"];
    for (std::size_t i = 0; i + 1u < parts.size(); ++i) {
        if (!parent->is_object() || !parent->contains(parts[i])) return false;
        parent = &(*parent)[parts[i]];
    }
    return parent->is_object() && parent->erase(parts.back()) != 0u;
}

void collectTokens(const json& value, const std::string& prefix,
                   LayoutThemeTokenKind kind,
                   std::vector<LayoutThemeTokenEntry>& out) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            collectTokens(it.value(), prefix.empty() ? it.key()
                : prefix + "." + it.key(), kind, out);
        }
        return;
    }
    LayoutThemeTokenEntry entry;
    entry.key = prefix;
    entry.kind = kind;
    if (kind == LayoutThemeTokenKind::Color && value.is_array()
        && value.size() == 4u) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << '['
               << value[0].get<float>() << ',' << value[1].get<float>() << ','
               << value[2].get<float>() << ',' << value[3].get<float>() << ']';
        entry.value = stream.str();
        out.push_back(std::move(entry));
    } else if (kind == LayoutThemeTokenKind::Float && value.is_number()) {
        entry.value = value.dump();
        out.push_back(std::move(entry));
    }
}

std::size_t replaceReferences(json& value, const std::string& from,
                              const std::string& to) {
    std::size_t count = 0;
    if (value.is_object() || value.is_array()) {
        for (auto& child : value) count += replaceReferences(child, from, to);
    } else if (value.is_string() && value.get_ref<const std::string&>() == from) {
        value = to;
        ++count;
    }
    return count;
}

std::size_t countReferences(const json& value, const std::string& reference) {
    std::size_t count = 0;
    if (value.is_object() || value.is_array()) {
        for (const auto& child : value) count += countReferences(child, reference);
    } else if (value.is_string() && value.get_ref<const std::string&>() == reference) {
        ++count;
    }
    return count;
}

} // namespace

bool LayoutThemeEditorModel::load(const std::string& jsonText,
                                  std::string* error) {
    try {
        const json value = json::parse(jsonText);
        if (!value.is_object()) return fail(error, "Theme root must be an object");
        Theme validation;
        if (!validation.loadFromJson(value.dump())) {
            return fail(error, "Theme JSON could not be parsed");
        }
        _json = value.dump();
        return true;
    } catch (const std::exception& ex) {
        return fail(error, std::string("Invalid theme JSON: ") + ex.what());
    }
}

std::string LayoutThemeEditorModel::serialize(bool pretty) const {
    const json value = json::parse(_json);
    return pretty ? value.dump(4) : value.dump();
}

void LayoutThemeEditorModel::clear() { _json = "{}"; }

std::vector<LayoutThemeTokenEntry> LayoutThemeEditorModel::tokens() const {
    const json root = json::parse(_json);
    std::vector<LayoutThemeTokenEntry> result;
    if (root.contains("tokens") && root["tokens"].is_object()) {
        const json& values = root["tokens"];
        if (values.contains("color")) {
            collectTokens(values["color"], "color",
                          LayoutThemeTokenKind::Color, result);
        }
        if (values.contains("space")) {
            collectTokens(values["space"], "space",
                          LayoutThemeTokenKind::Float, result);
        }
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (it.key() != "color" && it.key() != "space"
                && it.value().is_number()) {
                result.push_back({it.key(), LayoutThemeTokenKind::Float,
                                  it.value().dump()});
            }
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.key < b.key; });
    return result;
}

std::vector<LayoutThemeStyleEntry> LayoutThemeEditorModel::styles() const {
    const json root = json::parse(_json);
    std::vector<LayoutThemeStyleEntry> result;
    if (!root.contains("sheets") || !root["sheets"].is_object()) return result;
    for (auto fragment = root["sheets"].begin();
         fragment != root["sheets"].end(); ++fragment) {
        if (!fragment.value().is_object()
            || !fragment.value().contains("styles")
            || !fragment.value()["styles"].is_object()) continue;
        for (auto style = fragment.value()["styles"].begin();
             style != fragment.value()["styles"].end(); ++style) {
            result.push_back({fragment.key(), style.key()});
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.fragment == b.fragment ? a.styleId < b.styleId
                                        : a.fragment < b.fragment;
    });
    return result;
}

bool LayoutThemeEditorModel::setColorToken(
    const std::string& key, const math::FVector4& value, std::string* error) {
    if (!validKey(key) || key.rfind("color.", 0) != 0) {
        return fail(error, "Color token key must start with color.");
    }
    if (!std::isfinite(value.x) || !std::isfinite(value.y)
        || !std::isfinite(value.z) || !std::isfinite(value.w)) {
        return fail(error, "Color token contains a non-finite value");
    }
    json root = json::parse(_json);
    json* token = findToken(root, key, true);
    if (token == nullptr) return fail(error, "Invalid color token key");
    *token = {value.x, value.y, value.z, value.w};
    _json = root.dump();
    return true;
}

bool LayoutThemeEditorModel::setFloatToken(
    const std::string& key, float value, std::string* error) {
    if (!validKey(key) || key.rfind("color.", 0) == 0) {
        return fail(error, "Float token key must not use the color namespace");
    }
    if (!std::isfinite(value)) return fail(error, "Float token must be finite");
    json root = json::parse(_json);
    json* token = findToken(root, key, true);
    if (token == nullptr) return fail(error, "Invalid float token key");
    *token = value;
    _json = root.dump();
    return true;
}

bool LayoutThemeEditorModel::renameToken(
    const std::string& oldKey, const std::string& newKey,
    std::string* error) {
    if (!validKey(newKey)) return fail(error, "Invalid replacement token key");
    json root = json::parse(_json);
    const json* source = findToken(static_cast<const json&>(root), oldKey);
    if (source == nullptr) return fail(error, "Source token does not exist");
    if (findToken(static_cast<const json&>(root), newKey) != nullptr) {
        return fail(error, "Replacement token already exists");
    }
    const bool sourceIsColor = source->is_array();
    const bool sameKind = sourceIsColor
        ? newKey.rfind("color.", 0) == 0
        : newKey.rfind("color.", 0) != 0;
    if (!sameKind) return fail(error, "Token rename cannot change value kind");
    const json copy = *source;
    json* destination = findToken(root, newKey, true);
    if (destination == nullptr) return fail(error, "Invalid replacement token key");
    *destination = copy;
    if (!eraseToken(root, oldKey)) {
        return fail(error, "Source token disappeared");
    }
    replaceReferences(root["sheets"], "$" + oldKey, "$" + newKey);
    _json = root.dump();
    return true;
}

bool LayoutThemeEditorModel::removeToken(
    const std::string& key, bool allowReferenced, std::string* error) {
    if (!allowReferenced && referenceCount(key) != 0u) {
        return fail(error, "Token is still referenced by a style");
    }
    json root = json::parse(_json);
    if (!eraseToken(root, key)) return false;
    _json = root.dump();
    return true;
}

std::size_t LayoutThemeEditorModel::referenceCount(const std::string& key) const {
    const json root = json::parse(_json);
    if (!root.contains("sheets")) return 0u;
    return countReferences(root["sheets"], "$" + key);
}

bool LayoutThemeEditorModel::setStyleProperty(
    const std::string& fragment, const std::string& styleId,
    const std::string& property, const std::string& tokenOrLiteral,
    std::string* error) {
    if (fragment.empty() || styleId.empty() || property.empty()) {
        return fail(error, "Fragment, style ID and property are required");
    }
    if (!tokenOrLiteral.empty() && tokenOrLiteral.front() == '$'
        && findToken(json::parse(_json), tokenOrLiteral.substr(1)) == nullptr) {
        return fail(error, "Style references an unknown token");
    }
    json root = json::parse(_json);
    root["sheets"][fragment]["styles"][styleId][property] = tokenOrLiteral;
    _json = root.dump();
    return true;
}

Theme LayoutThemeEditorModel::buildPreviewTheme() const {
    Theme theme;
    theme.loadFromJson(_json);
    return theme;
}

} // namespace ayt::ui
