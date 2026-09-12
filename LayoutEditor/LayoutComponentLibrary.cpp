#include "AYUI/LayoutEditor/LayoutComponentLibrary.h"

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

bool LayoutComponentLibrary::validateId(const std::string& id,
                                        std::string* error) {
    if (id.empty()) return fail(error, "Component ID cannot be empty");
    if (id.size() > 96u) return fail(error, "Component ID is limited to 96 bytes");
    const auto first = static_cast<unsigned char>(id.front());
    if (!(std::isalpha(first) || first == '_')) {
        return fail(error, "Component ID must start with a letter or underscore");
    }
    for (const unsigned char ch : id) {
        if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.')) {
            return fail(error, "Component ID uses an unsupported character");
        }
    }
    return true;
}

bool LayoutComponentLibrary::define(
    const std::string& id, const std::string& displayName,
    const std::string& category, Widget* widget, std::string* error,
    const std::string& description, const std::vector<std::string>& tags) {
    if (widget == nullptr) return fail(error, "No Widget selected");
    return defineJson({id, displayName, category, description, tags,
                       WidgetSerializer::serializeWidget(widget)}, error);
}

bool LayoutComponentLibrary::defineJson(
    const LayoutComponentDefinition& definition, std::string* error) {
    if (!validateId(definition.id, error)) return false;
    try {
        const json widget = json::parse(definition.widgetJson);
        if (!widget.is_object() || !widget.contains("type")) {
            return fail(error, "Component must contain one Widget object");
        }
        LayoutComponentDefinition normalized = definition;
        normalized.displayName = normalized.displayName.empty()
            ? normalized.id : normalized.displayName;
        if (normalized.tags.size() > 32u) {
            return fail(error, "Component is limited to 32 tags");
        }
        for (std::string& tag : normalized.tags) {
            tag.erase(tag.begin(), std::find_if(tag.begin(), tag.end(),
                [](unsigned char ch) { return !std::isspace(ch); }));
            tag.erase(std::find_if(tag.rbegin(), tag.rend(),
                [](unsigned char ch) { return !std::isspace(ch); }).base(),
                tag.end());
            if (tag.size() > 64u) {
                return fail(error, "Component tag is limited to 64 bytes");
            }
        }
        normalized.tags.erase(std::remove(normalized.tags.begin(),
            normalized.tags.end(), std::string{}), normalized.tags.end());
        std::sort(normalized.tags.begin(), normalized.tags.end());
        normalized.tags.erase(std::unique(normalized.tags.begin(),
            normalized.tags.end()), normalized.tags.end());
        normalized.widgetJson = widget.dump();
        auto found = std::lower_bound(
            _components.begin(), _components.end(), normalized.id,
            [](const LayoutComponentDefinition& value,
               const std::string& candidate) {
                return value.id < candidate;
            });
        if (found != _components.end() && found->id == normalized.id) {
            *found = std::move(normalized);
        } else {
            _components.insert(found, std::move(normalized));
        }
        return true;
    } catch (const std::exception& ex) {
        return fail(error, std::string("Invalid component Widget JSON: ") + ex.what());
    }
}

bool LayoutComponentLibrary::remove(const std::string& id) {
    const auto found = std::lower_bound(
        _components.begin(), _components.end(), id,
        [](const LayoutComponentDefinition& value,
           const std::string& candidate) { return value.id < candidate; });
    if (found == _components.end() || found->id != id) return false;
    _components.erase(found);
    return true;
}

const LayoutComponentDefinition* LayoutComponentLibrary::find(
    const std::string& id) const {
    const auto found = std::lower_bound(
        _components.begin(), _components.end(), id,
        [](const LayoutComponentDefinition& value,
           const std::string& candidate) { return value.id < candidate; });
    return found != _components.end() && found->id == id ? &*found : nullptr;
}

Widget* LayoutComponentLibrary::instantiate(const std::string& id) const {
    const LayoutComponentDefinition* definition = find(id);
    return definition != nullptr
        ? WidgetSerializer::deserialize(definition->widgetJson) : nullptr;
}

std::string LayoutComponentLibrary::serialize(bool pretty) const {
    json root = {
        {"format", "AYUIComponentLibrary"},
        {"version", 2},
        {"components", json::array()},
    };
    for (const LayoutComponentDefinition& definition : _components) {
        root["components"].push_back({
            {"id", definition.id},
            {"displayName", definition.displayName},
            {"category", definition.category},
            {"description", definition.description},
            {"tags", definition.tags},
            {"widget", json::parse(definition.widgetJson)},
        });
    }
    return pretty ? root.dump(4) : root.dump();
}

bool LayoutComponentLibrary::deserialize(const std::string& jsonText,
                                         std::string* error) {
    try {
        const json root = json::parse(jsonText);
        if (!root.is_object() || root.value("format", std::string{})
                != "AYUIComponentLibrary") {
            return fail(error, "Not an AYUI component library");
        }
        const int version = root.value("version", 0);
        if (version != 1 && version != 2) {
            return fail(error, "Unsupported AYUI component library version");
        }
        if (!root.contains("components") || !root["components"].is_array()) {
            return fail(error, "Component library requires a components array");
        }
        if (root["components"].size() > 1024u) {
            return fail(error, "Component library contains too many entries");
        }
        LayoutComponentLibrary decoded;
        for (const json& value : root["components"]) {
            if (!value.is_object() || !value.contains("widget")
                || !value["widget"].is_object()) {
                return fail(error, "Component entry is malformed");
            }
            LayoutComponentDefinition definition;
            definition.id = value.value("id", std::string{});
            definition.displayName = value.value("displayName", definition.id);
            definition.category = value.value("category", std::string{});
            definition.description = value.value("description", std::string{});
            if (value.contains("tags")) {
                if (!value["tags"].is_array()) {
                    return fail(error, "Component tags must be an array");
                }
                for (const json& tag : value["tags"]) {
                    if (!tag.is_string()) {
                        return fail(error, "Component tag must be a string");
                    }
                    definition.tags.push_back(tag.get<std::string>());
                }
            }
            definition.widgetJson = value["widget"].dump();
            if (!decoded.defineJson(definition, error)) return false;
        }
        _components = std::move(decoded._components);
        return true;
    } catch (const std::exception& ex) {
        return fail(error, std::string("Invalid component library JSON: ") + ex.what());
    }
}

} // namespace ayt::ui
