#include "AYUI/LayoutEditor/LayoutComponentLibrary.h"

#include "AYUI/WidgetSerializer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

std::string sourceRevision(const std::string& canonicalJson) {
    std::uint64_t value = 14695981039346656037ull;
    for (const unsigned char byte : canonicalJson) {
        value ^= byte;
        value *= 1099511628211ull;
    }
    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0') << std::setw(16) << value;
    return encoded.str();
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
        normalized.sourceRevision = sourceRevision(normalized.widgetJson);
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

bool LayoutComponentLibrary::createInstance(
    const std::string& id, LayoutComponentInstance& outInstance) const {
    const LayoutComponentDefinition* definition = find(id);
    if (definition == nullptr) return false;
    outInstance = {};
    outInstance.componentId = definition->id;
    outInstance.sourceRevision = definition->sourceRevision;
    return true;
}

LayoutComponentMaterialization LayoutComponentLibrary::materialize(
    const LayoutComponentInstance& instance) const {
    LayoutComponentMaterialization result;
    const LayoutComponentDefinition* definition = find(instance.componentId);
    if (definition == nullptr) {
        result.conflicts.push_back("Component source is missing: "
                                   + instance.componentId);
        return result;
    }
    result.appliedRevision = definition->sourceRevision;
    result.sourceChanged = !instance.sourceRevision.empty()
        && instance.sourceRevision != definition->sourceRevision;
    try {
        json widget = json::parse(definition->widgetJson);
        for (const LayoutComponentOverride& overrideValue : instance.overrides) {
            try {
                const json::json_pointer pointer(overrideValue.path);
                if (!widget.contains(pointer)) {
                    result.conflicts.push_back(
                        "Override target no longer exists: " + overrideValue.path);
                    continue;
                }
                widget[pointer] = json::parse(overrideValue.valueJson);
            } catch (const std::exception& ex) {
                result.conflicts.push_back("Invalid override "
                    + overrideValue.path + ": " + ex.what());
            }
        }
        if (result.conflicts.empty()) {
            result.widget = WidgetSerializer::deserialize(widget.dump());
            if (result.widget == nullptr) {
                result.conflicts.push_back(
                    "Materialized component Widget could not be created");
            }
        }
    } catch (const std::exception& ex) {
        result.conflicts.push_back(std::string("Component source is invalid: ")
                                   + ex.what());
    }
    return result;
}

LayoutComponentMaterialization LayoutComponentLibrary::rebase(
    LayoutComponentInstance& instance) const {
    LayoutComponentMaterialization result = materialize(instance);
    if (result.widget != nullptr && result.conflicts.empty()) {
        instance.sourceRevision = result.appliedRevision;
    }
    return result;
}

std::string LayoutComponentLibrary::serialize(bool pretty) const {
    json root = {
        {"format", "AYUIComponentLibrary"},
        {"version", 3},
        {"components", json::array()},
    };
    for (const LayoutComponentDefinition& definition : _components) {
        root["components"].push_back({
            {"id", definition.id},
            {"displayName", definition.displayName},
            {"category", definition.category},
            {"description", definition.description},
            {"tags", definition.tags},
            {"sourceRevision", definition.sourceRevision},
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
        if (version != 1 && version != 2 && version != 3) {
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

std::string LayoutComponentLibrary::serializeInstance(
    const LayoutComponentInstance& instance, bool pretty) {
    json overrides = json::array();
    for (const LayoutComponentOverride& overrideValue : instance.overrides) {
        overrides.push_back({
            {"path", overrideValue.path},
            {"value", json::parse(overrideValue.valueJson)},
        });
    }
    const json encoded = {
        {"format", "AYUIComponentInstance"},
        {"version", 1},
        {"componentId", instance.componentId},
        {"sourceRevision", instance.sourceRevision},
        {"overrides", std::move(overrides)},
    };
    return pretty ? encoded.dump(4) : encoded.dump();
}

bool LayoutComponentLibrary::deserializeInstance(
    const std::string& jsonText, LayoutComponentInstance& outInstance,
    std::string* error) {
    try {
        const json encoded = json::parse(jsonText);
        if (!encoded.is_object()
            || encoded.value("format", std::string{})
                != "AYUIComponentInstance"
            || encoded.value("version", 0) != 1) {
            return fail(error, "Not a supported AYUI component instance");
        }
        LayoutComponentInstance decoded;
        decoded.componentId = encoded.value("componentId", std::string{});
        decoded.sourceRevision = encoded.value("sourceRevision", std::string{});
        if (!validateId(decoded.componentId, error)) return false;
        if (!encoded.contains("overrides") || !encoded["overrides"].is_array()) {
            return fail(error, "Component instance requires an overrides array");
        }
        if (encoded["overrides"].size() > 512u) {
            return fail(error, "Component instance contains too many overrides");
        }
        for (const json& entry : encoded["overrides"]) {
            if (!entry.is_object() || !entry.contains("path")
                || !entry["path"].is_string() || !entry.contains("value")) {
                return fail(error, "Component override is malformed");
            }
            const std::string path = entry["path"].get<std::string>();
            try {
                (void)json::json_pointer(path);
            } catch (const std::exception&) {
                return fail(error, "Component override path is not a JSON Pointer");
            }
            decoded.overrides.push_back({path, entry["value"].dump()});
        }
        outInstance = std::move(decoded);
        return true;
    } catch (const std::exception& ex) {
        return fail(error, std::string("Invalid component instance JSON: ")
                           + ex.what());
    }
}

} // namespace ayt::ui
