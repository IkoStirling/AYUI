#include "AYUI/UIFlow.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ayt::ui {
namespace {

using json = nlohmann::json;

void addDiagnostic(std::vector<UIFlowDiagnostic>* diagnostics,
                   UIFlowDiagnosticSeverity severity,
                   std::string path, std::string message)
{
    if (diagnostics == nullptr) return;
    diagnostics->push_back(
        UIFlowDiagnostic{severity, std::move(path), std::move(message)});
}

void addError(std::vector<UIFlowDiagnostic>* diagnostics,
              std::string path, std::string message)
{
    addDiagnostic(diagnostics, UIFlowDiagnosticSeverity::Error,
                  std::move(path), std::move(message));
}

bool hasErrors(const std::vector<UIFlowDiagnostic>* diagnostics)
{
    if (diagnostics == nullptr) return false;
    return std::any_of(diagnostics->begin(), diagnostics->end(),
        [](const UIFlowDiagnostic& diagnostic) {
            return diagnostic.severity == UIFlowDiagnosticSeverity::Error;
        });
}

std::string indexedPath(std::string_view section, std::size_t index)
{
    return std::string(section) + "[" + std::to_string(index) + "]";
}

bool readString(const json& object, const char* key, std::string& output,
                std::vector<UIFlowDiagnostic>* diagnostics,
                const std::string& path, bool required = false)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        if (required) addError(diagnostics, path + "." + key,
                               "Required string is missing.");
        return !required;
    }
    if (!found->is_string()) {
        addError(diagnostics, path + "." + key, "Expected a string.");
        return false;
    }
    output = found->get<std::string>();
    if (required && output.empty()) {
        addError(diagnostics, path + "." + key,
                 "Required string must not be empty.");
        return false;
    }
    return true;
}

bool readBool(const json& object, const char* key, bool& output,
              std::vector<UIFlowDiagnostic>* diagnostics,
              const std::string& path)
{
    const auto found = object.find(key);
    if (found == object.end()) return true;
    if (!found->is_boolean()) {
        addError(diagnostics, path + "." + key, "Expected a boolean.");
        return false;
    }
    output = found->get<bool>();
    return true;
}

template <typename T>
bool readInteger(const json& object, const char* key, T& output,
                 std::vector<UIFlowDiagnostic>* diagnostics,
                 const std::string& path)
{
    const auto found = object.find(key);
    if (found == object.end()) return true;
    if (!found->is_number_integer() && !found->is_number_unsigned()) {
        addError(diagnostics, path + "." + key, "Expected an integer.");
        return false;
    }
    bool inRange = false;
    if (found->is_number_unsigned()) {
        const std::uint64_t value = found->get<std::uint64_t>();
        inRange = value <= static_cast<std::uint64_t>(
            std::numeric_limits<T>::max());
        if (inRange) output = static_cast<T>(value);
    } else {
        const std::int64_t value = found->get<std::int64_t>();
        if constexpr (std::is_signed_v<T>) {
            inRange = value >= static_cast<std::int64_t>(
                std::numeric_limits<T>::min())
                && value <= static_cast<std::int64_t>(
                    std::numeric_limits<T>::max());
        } else {
            inRange = value >= 0
                && static_cast<std::uint64_t>(value)
                    <= static_cast<std::uint64_t>(
                        std::numeric_limits<T>::max());
        }
        if (inRange) output = static_cast<T>(value);
    }
    if (inRange) return true;
    addError(diagnostics, path + "." + key,
             "Integer is outside the supported range.");
    return false;
}

bool readStringArray(const json& object, const char* key,
                     std::vector<std::string>& output,
                     std::vector<UIFlowDiagnostic>* diagnostics,
                     const std::string& path)
{
    const auto found = object.find(key);
    if (found == object.end()) return true;
    if (!found->is_array()) {
        addError(diagnostics, path + "." + key, "Expected an array.");
        return false;
    }
    bool valid = true;
    for (std::size_t index = 0; index < found->size(); ++index) {
        if (!(*found)[index].is_string()) {
            addError(diagnostics,
                path + "." + key + "[" + std::to_string(index) + "]",
                "Expected a string.");
            valid = false;
            continue;
        }
        output.push_back((*found)[index].get<std::string>());
    }
    return valid;
}

bool valueFromJson(const json& source, UIFlowValue& output,
                   std::vector<UIFlowDiagnostic>* diagnostics,
                   const std::string& path)
{
    if (source.is_null()) {
        output = std::monostate{};
        return true;
    }
    if (source.is_boolean()) {
        output = source.get<bool>();
        return true;
    }
    if (source.is_number_integer() || source.is_number_unsigned()) {
        try {
            output = source.get<std::int64_t>();
            return true;
        } catch (const std::exception&) {
            addError(diagnostics, path,
                     "Integer is outside the supported signed 64-bit range.");
            return false;
        }
    }
    if (source.is_number_float()) {
        output = source.get<double>();
        return true;
    }
    if (source.is_string()) {
        output = source.get<std::string>();
        return true;
    }
    if (source.is_array()) {
        UIFlowValue::Array values;
        values.reserve(source.size());
        bool valid = true;
        for (std::size_t index = 0; index < source.size(); ++index) {
            UIFlowValue value;
            valid = valueFromJson(source[index], value, diagnostics,
                path + "[" + std::to_string(index) + "]") && valid;
            values.push_back(std::move(value));
        }
        output = std::move(values);
        return valid;
    }
    if (source.is_object()) {
        UIFlowValue::Object values;
        bool valid = true;
        for (auto it = source.begin(); it != source.end(); ++it) {
            UIFlowValue value;
            valid = valueFromJson(it.value(), value, diagnostics,
                path + "." + it.key()) && valid;
            values[it.key()] = std::move(value);
        }
        output = std::move(values);
        return valid;
    }
    addError(diagnostics, path, "Unsupported JSON value.");
    return false;
}

json valueToJson(const UIFlowValue& value)
{
    if (std::holds_alternative<std::monostate>(value.data)) return nullptr;
    if (const auto* item = std::get_if<bool>(&value.data)) return *item;
    if (const auto* item = std::get_if<std::int64_t>(&value.data)) return *item;
    if (const auto* item = std::get_if<double>(&value.data)) return *item;
    if (const auto* item = std::get_if<std::string>(&value.data)) return *item;
    if (const auto* values = std::get_if<UIFlowValue::Array>(&value.data)) {
        json result = json::array();
        for (const UIFlowValue& item : *values) {
            result.push_back(valueToJson(item));
        }
        return result;
    }
    json result = json::object();
    for (const auto& [key, item] : std::get<UIFlowValue::Object>(value.data)) {
        result[key] = valueToJson(item);
    }
    return result;
}

bool valueMatchesType(const UIFlowValue& value, UIFlowValueType type)
{
    if (std::holds_alternative<std::monostate>(value.data)) return true;
    switch (type) {
    case UIFlowValueType::Boolean:
        return std::holds_alternative<bool>(value.data);
    case UIFlowValueType::Integer:
        return std::holds_alternative<std::int64_t>(value.data);
    case UIFlowValueType::Number:
        return std::holds_alternative<std::int64_t>(value.data)
            || std::holds_alternative<double>(value.data);
    case UIFlowValueType::String:
    case UIFlowValueType::Entity:
    case UIFlowValueType::Asset:
        return std::holds_alternative<std::string>(value.data);
    }
    return false;
}

template <typename Enum>
bool parseNamedEnum(const json& object, const char* key, Enum& output,
                    const std::pair<std::string_view, Enum>* choices,
                    std::size_t choiceCount,
                    std::vector<UIFlowDiagnostic>* diagnostics,
                    const std::string& path)
{
    const auto found = object.find(key);
    if (found == object.end()) return true;
    if (!found->is_string()) {
        addError(diagnostics, path + "." + key, "Expected a string.");
        return false;
    }
    const std::string value = found->get<std::string>();
    for (std::size_t index = 0; index < choiceCount; ++index) {
        if (choices[index].first == value) {
            output = choices[index].second;
            return true;
        }
    }
    addError(diagnostics, path + "." + key,
             "Unknown enum value '" + value + "'.");
    return false;
}

constexpr std::pair<std::string_view, UIFlowScope> kScopeChoices[] = {
    {"application", UIFlowScope::Application},
    {"world", UIFlowScope::World},
    {"owner", UIFlowScope::Owner},
    {"transient", UIFlowScope::Transient},
};

constexpr std::pair<std::string_view, UIFlowInputPolicy> kInputChoices[] = {
    {"passThrough", UIFlowInputPolicy::PassThrough},
    {"consumeHandled", UIFlowInputPolicy::ConsumeHandled},
    {"blockLower", UIFlowInputPolicy::BlockLower},
};

constexpr std::pair<std::string_view, UIFlowInterruptPolicy>
kInterruptChoices[] = {
    {"queue", UIFlowInterruptPolicy::Queue},
    {"cancelPrevious", UIFlowInterruptPolicy::CancelPrevious},
    {"reversePrevious", UIFlowInterruptPolicy::ReversePrevious},
    {"ignoreIfRunning", UIFlowInterruptPolicy::IgnoreIfRunning},
    {"coalesce", UIFlowInterruptPolicy::Coalesce},
};

constexpr std::pair<std::string_view, UIFlowSlotOperation> kSlotChoices[] = {
    {"present", UIFlowSlotOperation::Present},
    {"hide", UIFlowSlotOperation::Hide},
};

constexpr std::pair<std::string_view, UIFlowValueType> kValueTypeChoices[] = {
    {"bool", UIFlowValueType::Boolean},
    {"integer", UIFlowValueType::Integer},
    {"number", UIFlowValueType::Number},
    {"string", UIFlowValueType::String},
    {"entity", UIFlowValueType::Entity},
    {"asset", UIFlowValueType::Asset},
};

bool parseFields(const json& object, const char* key,
                 std::vector<UIFlowFieldDefinition>& output,
                 std::vector<UIFlowDiagnostic>* diagnostics,
                 const std::string& path)
{
    const auto found = object.find(key);
    if (found == object.end()) return true;
    if (!found->is_array()) {
        addError(diagnostics, path + "." + key, "Expected an array.");
        return false;
    }
    bool valid = true;
    for (std::size_t index = 0; index < found->size(); ++index) {
        const std::string itemPath = path + "." + key + "["
            + std::to_string(index) + "]";
        const json& item = (*found)[index];
        if (!item.is_object()) {
            addError(diagnostics, itemPath, "Expected an object.");
            valid = false;
            continue;
        }
        UIFlowFieldDefinition field;
        valid = readString(item, "id", field.id, diagnostics, itemPath, true)
            && valid;
        valid = parseNamedEnum(item, "type", field.type, kValueTypeChoices,
            std::size(kValueTypeChoices), diagnostics, itemPath) && valid;
        valid = readBool(item, "required", field.required, diagnostics,
                         itemPath) && valid;
        if (const auto defaultValue = item.find("default");
            defaultValue != item.end()) {
            valid = valueFromJson(*defaultValue, field.defaultValue,
                diagnostics, itemPath + ".default") && valid;
        }
        output.push_back(std::move(field));
    }
    return valid;
}

template <typename T>
bool validateUniqueIds(const std::vector<T>& values, std::string_view section,
                       std::vector<UIFlowDiagnostic>* diagnostics)
{
    bool valid = true;
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::string path = indexedPath(section, index) + ".id";
        if (values[index].id.empty()) {
            addError(diagnostics, path, "ID must not be empty.");
            valid = false;
        } else if (!ids.insert(values[index].id).second) {
            addError(diagnostics, path,
                     "Duplicate ID '" + values[index].id + "'.");
            valid = false;
        }
    }
    return valid;
}

template <typename T>
const T* findById(const std::vector<T>& values, std::string_view id)
{
    const auto found = std::find_if(values.begin(), values.end(),
        [id](const T& value) { return value.id == id; });
    return found == values.end() ? nullptr : &*found;
}

bool validateFields(const std::vector<UIFlowFieldDefinition>& fields,
                    const std::string& path,
                    std::vector<UIFlowDiagnostic>* diagnostics)
{
    bool valid = true;
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const UIFlowFieldDefinition& field = fields[index];
        const std::string itemPath = path + "[" + std::to_string(index) + "]";
        if (field.id.empty()) {
            addError(diagnostics, itemPath + ".id", "ID must not be empty.");
            valid = false;
        } else if (!ids.insert(field.id).second) {
            addError(diagnostics, itemPath + ".id",
                     "Duplicate field ID '" + field.id + "'.");
            valid = false;
        }
        if (!valueMatchesType(field.defaultValue, field.type)) {
            addError(diagnostics, itemPath + ".default",
                     "Default value does not match field type.");
            valid = false;
        }
    }
    return valid;
}

const UIFlowStateDefinition* findState(const UIFlowRegionDefinition& region,
                                       std::string_view id)
{
    return findById(region.states, id);
}

bool validateStateHierarchy(const UIFlowRegionDefinition& region,
                            std::size_t regionIndex,
                            std::vector<UIFlowDiagnostic>* diagnostics)
{
    bool valid = true;
    std::unordered_map<std::string, std::string> parents;
    for (const UIFlowStateDefinition& state : region.states) {
        parents.emplace(state.id, state.parent);
    }
    for (std::size_t stateIndex = 0; stateIndex < region.states.size();
         ++stateIndex) {
        const UIFlowStateDefinition& state = region.states[stateIndex];
        if (state.parent.empty()) continue;
        std::unordered_set<std::string> visited;
        std::string current = state.id;
        while (!current.empty()) {
            if (!visited.insert(current).second) {
                addError(diagnostics,
                    indexedPath("regions", regionIndex) + ".states["
                        + std::to_string(stateIndex) + "].parent",
                    "State parent hierarchy contains a cycle.");
                valid = false;
                break;
            }
            const auto found = parents.find(current);
            current = found == parents.end() ? std::string{} : found->second;
        }
    }
    return valid;
}

void writeFields(json& target, const char* key,
                 const std::vector<UIFlowFieldDefinition>& fields)
{
    target[key] = json::array();
    for (const UIFlowFieldDefinition& field : fields) {
        json item = {
            {"id", field.id},
            {"type", uiFlowValueTypeName(field.type)},
            {"required", field.required},
        };
        if (!std::holds_alternative<std::monostate>(field.defaultValue.data)) {
            item["default"] = valueToJson(field.defaultValue);
        }
        target[key].push_back(std::move(item));
    }
}

} // namespace

const UIFlowLayerDefinition* UIFlowDocument::findLayer(
    std::string_view value) const
{
    return findById(layers, value);
}

const UIFlowSlotDefinition* UIFlowDocument::findSlot(
    std::string_view value) const
{
    return findById(slots, value);
}

const UIFlowScreenDefinition* UIFlowDocument::findScreen(
    std::string_view value) const
{
    return findById(screens, value);
}

const UIFlowContextDefinition* UIFlowDocument::findContext(
    std::string_view value) const
{
    return findById(contexts, value);
}

const UIFlowEntryDefinition* UIFlowDocument::findEntry(
    std::string_view value) const
{
    return findById(entries, value);
}

const UIFlowSignalDefinition* UIFlowDocument::findSignal(
    std::string_view value) const
{
    return findById(signals, value);
}

const UIFlowActionDefinition* UIFlowDocument::findAction(
    std::string_view value) const
{
    return findById(actions, value);
}

const UIFlowRegionDefinition* UIFlowDocument::findRegion(
    std::string_view value) const
{
    return findById(regions, value);
}

const UIFlowGraphDefinition* UIFlowDocument::findGraph(
    std::string_view value) const
{
    return findById(graphs, value);
}

const char* uiFlowScopeName(UIFlowScope value)
{
    switch (value) {
    case UIFlowScope::Application: return "application";
    case UIFlowScope::World: return "world";
    case UIFlowScope::Owner: return "owner";
    case UIFlowScope::Transient: return "transient";
    }
    return "transient";
}

const char* uiFlowInputPolicyName(UIFlowInputPolicy value)
{
    switch (value) {
    case UIFlowInputPolicy::PassThrough: return "passThrough";
    case UIFlowInputPolicy::ConsumeHandled: return "consumeHandled";
    case UIFlowInputPolicy::BlockLower: return "blockLower";
    }
    return "consumeHandled";
}

const char* uiFlowInterruptPolicyName(UIFlowInterruptPolicy value)
{
    switch (value) {
    case UIFlowInterruptPolicy::Queue: return "queue";
    case UIFlowInterruptPolicy::CancelPrevious: return "cancelPrevious";
    case UIFlowInterruptPolicy::ReversePrevious: return "reversePrevious";
    case UIFlowInterruptPolicy::IgnoreIfRunning: return "ignoreIfRunning";
    case UIFlowInterruptPolicy::Coalesce: return "coalesce";
    }
    return "queue";
}

const char* uiFlowSlotOperationName(UIFlowSlotOperation value)
{
    return value == UIFlowSlotOperation::Hide ? "hide" : "present";
}

const char* uiFlowValueTypeName(UIFlowValueType value)
{
    switch (value) {
    case UIFlowValueType::Boolean: return "bool";
    case UIFlowValueType::Integer: return "integer";
    case UIFlowValueType::Number: return "number";
    case UIFlowValueType::String: return "string";
    case UIFlowValueType::Entity: return "entity";
    case UIFlowValueType::Asset: return "asset";
    }
    return "string";
}

bool validateUIFlow(const UIFlowDocument& document,
                    std::vector<UIFlowDiagnostic>* diagnostics)
{
    if (diagnostics != nullptr) diagnostics->clear();
    bool valid = true;
    if (document.schemaVersion != kUIFlowSchemaVersion) {
        addError(diagnostics, "schemaVersion",
            "Unsupported UI Flow schemaVersion "
                + std::to_string(document.schemaVersion) + ".");
        valid = false;
    }
    if (document.id.empty()) {
        addError(diagnostics, "id", "Flow ID must not be empty.");
        valid = false;
    }

    valid = validateUniqueIds(document.layers, "layers", diagnostics) && valid;
    valid = validateUniqueIds(document.slots, "slots", diagnostics) && valid;
    valid = validateUniqueIds(document.screens, "screens", diagnostics) && valid;
    valid = validateUniqueIds(document.contexts, "contexts", diagnostics) && valid;
    valid = validateUniqueIds(document.entries, "entries", diagnostics) && valid;
    valid = validateUniqueIds(document.signals, "signals", diagnostics) && valid;
    valid = validateUniqueIds(document.actions, "actions", diagnostics) && valid;
    valid = validateUniqueIds(document.regions, "regions", diagnostics) && valid;
    valid = validateUniqueIds(document.transitions, "transitions", diagnostics) && valid;
    valid = validateUniqueIds(document.graphs, "graphs", diagnostics) && valid;

    if (!document.defaultEntry.empty()
        && document.findEntry(document.defaultEntry) == nullptr) {
        addError(diagnostics, "defaultEntry",
                 "Default entry does not reference an entry definition.");
        valid = false;
    }

    for (std::size_t index = 0; index < document.slots.size(); ++index) {
        const UIFlowSlotDefinition& slot = document.slots[index];
        const std::string path = indexedPath("slots", index);
        if (document.findLayer(slot.layer) == nullptr) {
            addError(diagnostics, path + ".layer",
                     "Slot references an unknown Layer.");
            valid = false;
        }
        if (slot.capacity == 0u) {
            addError(diagnostics, path + ".capacity",
                     "Slot capacity must be at least one.");
            valid = false;
        }
    }

    for (std::size_t index = 0; index < document.screens.size(); ++index) {
        const UIFlowScreenDefinition& screen = document.screens[index];
        const std::string path = indexedPath("screens", index);
        if (screen.layoutAsset.empty()) {
            addError(diagnostics, path + ".layout",
                     "Screen layout asset must not be empty.");
            valid = false;
        }
        if (document.findLayer(screen.layer) == nullptr) {
            addError(diagnostics, path + ".layer",
                     "Screen references an unknown Layer.");
            valid = false;
        }
        if (!screen.slot.empty()) {
            const UIFlowSlotDefinition* slot = document.findSlot(screen.slot);
            if (slot == nullptr) {
                addError(diagnostics, path + ".slot",
                         "Screen references an unknown Slot.");
                valid = false;
            } else if (slot->layer != screen.layer) {
                addError(diagnostics, path + ".slot",
                         "Screen and Slot must use the same Layer.");
                valid = false;
            }
        }
    }

    for (std::size_t contextIndex = 0;
         contextIndex < document.contexts.size(); ++contextIndex) {
        const UIFlowContextDefinition& context = document.contexts[contextIndex];
        std::unordered_set<std::string> assignedSlots;
        for (std::size_t slotIndex = 0; slotIndex < context.slots.size();
             ++slotIndex) {
            const UIFlowSlotAssignment& assignment = context.slots[slotIndex];
            const std::string path = indexedPath("contexts", contextIndex)
                + ".slots[" + std::to_string(slotIndex) + "]";
            const UIFlowSlotDefinition* slot = document.findSlot(assignment.slot);
            if (slot == nullptr) {
                addError(diagnostics, path + ".slot",
                         "Context references an unknown Slot.");
                valid = false;
            } else if (!assignedSlots.insert(assignment.slot).second) {
                addError(diagnostics, path + ".slot",
                         "Context assigns the same Slot more than once.");
                valid = false;
            }
            if (assignment.operation == UIFlowSlotOperation::Hide) {
                if (!assignment.screen.empty()) {
                    addError(diagnostics, path + ".screen",
                             "Hide assignments must not name a Screen.");
                    valid = false;
                }
                continue;
            }
            const UIFlowScreenDefinition* screen =
                document.findScreen(assignment.screen);
            if (screen == nullptr) {
                addError(diagnostics, path + ".screen",
                         "Context references an unknown Screen.");
                valid = false;
            } else if (slot != nullptr
                       && (screen->layer != slot->layer
                           || (!screen->slot.empty()
                               && screen->slot != slot->id))) {
                addError(diagnostics, path + ".screen",
                         "Context Screen is incompatible with the assigned Slot.");
                valid = false;
            }
        }
    }

    for (std::size_t index = 0; index < document.entries.size(); ++index) {
        const UIFlowEntryDefinition& entry = document.entries[index];
        const std::string path = indexedPath("entries", index);
        for (std::size_t contextIndex = 0;
             contextIndex < entry.contexts.size(); ++contextIndex) {
            if (document.findContext(entry.contexts[contextIndex]) == nullptr) {
                addError(diagnostics, path + ".contexts["
                    + std::to_string(contextIndex) + "]",
                    "Entry references an unknown Context.");
                valid = false;
            }
        }
        if (!entry.actionGraph.empty()
            && document.findGraph(entry.actionGraph) == nullptr) {
            addError(diagnostics, path + ".actionGraph",
                     "Entry references an unknown Graph.");
            valid = false;
        }
    }

    for (std::size_t index = 0; index < document.signals.size(); ++index) {
        valid = validateFields(document.signals[index].payload,
            indexedPath("signals", index) + ".payload", diagnostics) && valid;
    }
    for (std::size_t index = 0; index < document.actions.size(); ++index) {
        valid = validateFields(document.actions[index].inputs,
            indexedPath("actions", index) + ".inputs", diagnostics) && valid;
    }

    for (std::size_t regionIndex = 0;
         regionIndex < document.regions.size(); ++regionIndex) {
        const UIFlowRegionDefinition& region = document.regions[regionIndex];
        const std::string path = indexedPath("regions", regionIndex);
        valid = validateUniqueIds(region.states, path + ".states", diagnostics)
            && valid;
        if (findState(region, region.initialState) == nullptr) {
            addError(diagnostics, path + ".initialState",
                     "Region initialState does not reference one of its States.");
            valid = false;
        }
        for (std::size_t stateIndex = 0; stateIndex < region.states.size();
             ++stateIndex) {
            const UIFlowStateDefinition& state = region.states[stateIndex];
            const std::string statePath = path + ".states["
                + std::to_string(stateIndex) + "]";
            if (!state.parent.empty() && findState(region, state.parent) == nullptr) {
                addError(diagnostics, statePath + ".parent",
                         "State parent does not exist in this Region.");
                valid = false;
            }
            if (!state.initialChild.empty()) {
                const UIFlowStateDefinition* child =
                    findState(region, state.initialChild);
                if (child == nullptr || child->parent != state.id) {
                    addError(diagnostics, statePath + ".initialChild",
                             "initialChild must name a direct child State.");
                    valid = false;
                }
            }
            for (std::size_t contextIndex = 0;
                 contextIndex < state.contexts.size(); ++contextIndex) {
                if (document.findContext(state.contexts[contextIndex]) == nullptr) {
                    addError(diagnostics, statePath + ".contexts["
                        + std::to_string(contextIndex) + "]",
                        "State references an unknown Context.");
                    valid = false;
                }
            }
            if (!state.enterGraph.empty()
                && document.findGraph(state.enterGraph) == nullptr) {
                addError(diagnostics, statePath + ".enterGraph",
                         "State references an unknown enter Graph.");
                valid = false;
            }
            if (!state.exitGraph.empty()
                && document.findGraph(state.exitGraph) == nullptr) {
                addError(diagnostics, statePath + ".exitGraph",
                         "State references an unknown exit Graph.");
                valid = false;
            }
        }
        valid = validateStateHierarchy(region, regionIndex, diagnostics) && valid;
    }

    for (std::size_t index = 0; index < document.transitions.size(); ++index) {
        const UIFlowTransitionDefinition& transition = document.transitions[index];
        const std::string path = indexedPath("transitions", index);
        const UIFlowRegionDefinition* region =
            document.findRegion(transition.region);
        if (region == nullptr) {
            addError(diagnostics, path + ".region",
                     "Transition references an unknown Region.");
            valid = false;
            continue;
        }
        if (transition.fromState != "*"
            && findState(*region, transition.fromState) == nullptr) {
            addError(diagnostics, path + ".from",
                     "Transition source State does not exist in its Region.");
            valid = false;
        }
        if (findState(*region, transition.toState) == nullptr) {
            addError(diagnostics, path + ".to",
                     "Transition target State does not exist in its Region.");
            valid = false;
        }
        if (document.findSignal(transition.triggerSignal) == nullptr) {
            addError(diagnostics, path + ".trigger",
                     "Transition trigger does not reference a declared Signal.");
            valid = false;
        }
        if (!transition.actionGraph.empty()
            && document.findGraph(transition.actionGraph) == nullptr) {
            addError(diagnostics, path + ".actionGraph",
                     "Transition references an unknown Graph.");
            valid = false;
        }
    }

    for (std::size_t graphIndex = 0;
         graphIndex < document.graphs.size(); ++graphIndex) {
        const UIFlowGraphDefinition& graph = document.graphs[graphIndex];
        const std::string path = indexedPath("graphs", graphIndex);
        valid = validateUniqueIds(graph.nodes, path + ".nodes", diagnostics)
            && valid;
        std::unordered_set<std::string> nodeIds;
        for (std::size_t nodeIndex = 0; nodeIndex < graph.nodes.size();
             ++nodeIndex) {
            nodeIds.insert(graph.nodes[nodeIndex].id);
            if (graph.nodes[nodeIndex].type.empty()) {
                addError(diagnostics, path + ".nodes["
                    + std::to_string(nodeIndex) + "].type",
                    "Node type must not be empty.");
                valid = false;
            }
        }
        for (std::size_t linkIndex = 0; linkIndex < graph.links.size();
             ++linkIndex) {
            const UIFlowLinkDefinition& link = graph.links[linkIndex];
            const std::string linkPath = path + ".links["
                + std::to_string(linkIndex) + "]";
            if (nodeIds.find(link.fromNode) == nodeIds.end()) {
                addError(diagnostics, linkPath + ".fromNode",
                         "Link source Node does not exist.");
                valid = false;
            }
            if (nodeIds.find(link.toNode) == nodeIds.end()) {
                addError(diagnostics, linkPath + ".toNode",
                         "Link target Node does not exist.");
                valid = false;
            }
            if (link.fromPin.empty() || link.toPin.empty()) {
                addError(diagnostics, linkPath,
                         "Link pin names must not be empty.");
                valid = false;
            }
        }
    }

    return valid;
}

bool UIFlowSerializer::deserialize(
    std::string_view jsonText, UIFlowDocument& document,
    std::vector<UIFlowDiagnostic>* diagnostics)
{
    if (diagnostics != nullptr) diagnostics->clear();
    document = {};
    json root;
    try {
        root = json::parse(jsonText.begin(), jsonText.end());
    } catch (const std::exception& exception) {
        addError(diagnostics, "$", std::string("Invalid JSON: ") + exception.what());
        return false;
    }
    if (!root.is_object()) {
        addError(diagnostics, "$", "UI Flow root must be an object.");
        return false;
    }

    bool parsed = true;
    if (!root.contains("schemaVersion")) {
        addError(diagnostics, "schemaVersion",
                 "Required integer is missing.");
        parsed = false;
    } else {
        parsed = readInteger(root, "schemaVersion", document.schemaVersion,
                             diagnostics, "$") && parsed;
    }
    parsed = readString(root, "id", document.id, diagnostics, "$", true)
        && parsed;
    parsed = readString(root, "defaultEntry", document.defaultEntry,
                        diagnostics, "$") && parsed;

    auto parseObjectArray = [&](const char* key, auto&& visitor) {
        const auto found = root.find(key);
        if (found == root.end()) return;
        if (!found->is_array()) {
            addError(diagnostics, key, "Expected an array.");
            parsed = false;
            return;
        }
        for (std::size_t index = 0; index < found->size(); ++index) {
            if (!(*found)[index].is_object()) {
                addError(diagnostics, indexedPath(key, index),
                         "Expected an object.");
                parsed = false;
                continue;
            }
            visitor((*found)[index], index, indexedPath(key, index));
        }
    };

    parseObjectArray("layers", [&](const json& item, std::size_t,
                                    const std::string& path) {
        UIFlowLayerDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        parsed = readInteger(item, "order", value.order, diagnostics, path)
            && parsed;
        parsed = parseNamedEnum(item, "input", value.inputPolicy,
            kInputChoices, std::size(kInputChoices), diagnostics, path) && parsed;
        parsed = readBool(item, "blocksLowerInput", value.blocksLowerInput,
                          diagnostics, path) && parsed;
        parsed = readInteger(item, "maxActiveScreens", value.maxActiveScreens,
                             diagnostics, path) && parsed;
        document.layers.push_back(std::move(value));
    });

    parseObjectArray("slots", [&](const json& item, std::size_t,
                                   const std::string& path) {
        UIFlowSlotDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        parsed = readString(item, "layer", value.layer, diagnostics, path, true)
            && parsed;
        parsed = readInteger(item, "capacity", value.capacity, diagnostics, path)
            && parsed;
        parsed = readBool(item, "restorePrevious", value.restorePrevious,
                          diagnostics, path) && parsed;
        document.slots.push_back(std::move(value));
    });

    parseObjectArray("screens", [&](const json& item, std::size_t,
                                     const std::string& path) {
        UIFlowScreenDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        parsed = readString(item, "layout", value.layoutAsset, diagnostics,
                            path, true) && parsed;
        parsed = readString(item, "layer", value.layer, diagnostics, path, true)
            && parsed;
        parsed = readString(item, "slot", value.slot, diagnostics, path)
            && parsed;
        parsed = parseNamedEnum(item, "scope", value.scope, kScopeChoices,
            std::size(kScopeChoices), diagnostics, path) && parsed;
        parsed = readString(item, "enterAnimation", value.enterAnimation,
                            diagnostics, path) && parsed;
        parsed = readString(item, "exitAnimation", value.exitAnimation,
                            diagnostics, path) && parsed;
        if (const auto properties = item.find("parameters");
            properties != item.end()) {
            if (!properties->is_object()) {
                addError(diagnostics, path + ".parameters", "Expected an object.");
                parsed = false;
            } else {
                for (auto it = properties->begin(); it != properties->end(); ++it) {
                    UIFlowValue property;
                    parsed = valueFromJson(it.value(), property, diagnostics,
                        path + ".parameters." + it.key()) && parsed;
                    value.parameters[it.key()] = std::move(property);
                }
            }
        }
        document.screens.push_back(std::move(value));
    });

    parseObjectArray("contexts", [&](const json& item, std::size_t,
                                      const std::string& path) {
        UIFlowContextDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        parsed = readInteger(item, "priority", value.priority, diagnostics, path)
            && parsed;
        const auto slots = item.find("slots");
        if (slots != item.end()) {
            if (!slots->is_array()) {
                addError(diagnostics, path + ".slots", "Expected an array.");
                parsed = false;
            } else {
                for (std::size_t index = 0; index < slots->size(); ++index) {
                    const std::string slotPath = path + ".slots["
                        + std::to_string(index) + "]";
                    if (!(*slots)[index].is_object()) {
                        addError(diagnostics, slotPath, "Expected an object.");
                        parsed = false;
                        continue;
                    }
                    UIFlowSlotAssignment assignment;
                    parsed = readString((*slots)[index], "slot", assignment.slot,
                        diagnostics, slotPath, true) && parsed;
                    parsed = parseNamedEnum((*slots)[index], "operation",
                        assignment.operation, kSlotChoices,
                        std::size(kSlotChoices), diagnostics, slotPath) && parsed;
                    parsed = readString((*slots)[index], "screen", assignment.screen,
                        diagnostics, slotPath) && parsed;
                    value.slots.push_back(std::move(assignment));
                }
            }
        }
        document.contexts.push_back(std::move(value));
    });

    parseObjectArray("entries", [&](const json& item, std::size_t,
                                     const std::string& path) {
        UIFlowEntryDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        parsed = readStringArray(item, "contexts", value.contexts, diagnostics,
                                 path) && parsed;
        parsed = readString(item, "actionGraph", value.actionGraph, diagnostics,
                            path) && parsed;
        document.entries.push_back(std::move(value));
    });

    parseObjectArray("signals", [&](const json& item, std::size_t,
                                     const std::string& path) {
        UIFlowSignalDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        parsed = parseFields(item, "payload", value.payload, diagnostics, path)
            && parsed;
        document.signals.push_back(std::move(value));
    });

    parseObjectArray("actions", [&](const json& item, std::size_t,
                                     const std::string& path) {
        UIFlowActionDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        parsed = parseFields(item, "inputs", value.inputs, diagnostics, path)
            && parsed;
        document.actions.push_back(std::move(value));
    });

    parseObjectArray("regions", [&](const json& item, std::size_t,
                                     const std::string& path) {
        UIFlowRegionDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        parsed = readString(item, "initialState", value.initialState,
                            diagnostics, path, true) && parsed;
        const auto states = item.find("states");
        if (states == item.end() || !states->is_array()) {
            addError(diagnostics, path + ".states", "Expected a States array.");
            parsed = false;
        } else {
            for (std::size_t index = 0; index < states->size(); ++index) {
                const std::string statePath = path + ".states["
                    + std::to_string(index) + "]";
                if (!(*states)[index].is_object()) {
                    addError(diagnostics, statePath, "Expected an object.");
                    parsed = false;
                    continue;
                }
                UIFlowStateDefinition state;
                parsed = readString((*states)[index], "id", state.id,
                    diagnostics, statePath, true) && parsed;
                parsed = readString((*states)[index], "parent", state.parent,
                    diagnostics, statePath) && parsed;
                parsed = readString((*states)[index], "initialChild",
                    state.initialChild, diagnostics, statePath) && parsed;
                parsed = readStringArray((*states)[index], "contexts",
                    state.contexts, diagnostics, statePath) && parsed;
                parsed = readString((*states)[index], "enterGraph",
                    state.enterGraph, diagnostics, statePath) && parsed;
                parsed = readString((*states)[index], "exitGraph",
                    state.exitGraph, diagnostics, statePath) && parsed;
                value.states.push_back(std::move(state));
            }
        }
        document.regions.push_back(std::move(value));
    });

    parseObjectArray("transitions", [&](const json& item, std::size_t,
                                         const std::string& path) {
        UIFlowTransitionDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        parsed = readString(item, "region", value.region, diagnostics, path, true)
            && parsed;
        parsed = readString(item, "from", value.fromState, diagnostics, path, true)
            && parsed;
        parsed = readString(item, "to", value.toState, diagnostics, path, true)
            && parsed;
        parsed = readString(item, "trigger", value.triggerSignal, diagnostics,
                            path, true) && parsed;
        parsed = readString(item, "guard", value.guardExpression, diagnostics,
                            path) && parsed;
        parsed = readString(item, "actionGraph", value.actionGraph, diagnostics,
                            path) && parsed;
        parsed = readInteger(item, "priority", value.priority, diagnostics, path)
            && parsed;
        parsed = parseNamedEnum(item, "interrupt", value.interruptPolicy,
            kInterruptChoices, std::size(kInterruptChoices), diagnostics, path)
            && parsed;
        document.transitions.push_back(std::move(value));
    });

    parseObjectArray("graphs", [&](const json& item, std::size_t,
                                    const std::string& path) {
        UIFlowGraphDefinition value;
        parsed = readString(item, "id", value.id, diagnostics, path, true)
            && parsed;
        const auto nodes = item.find("nodes");
        if (nodes != item.end()) {
            if (!nodes->is_array()) {
                addError(diagnostics, path + ".nodes", "Expected an array.");
                parsed = false;
            } else {
                for (std::size_t index = 0; index < nodes->size(); ++index) {
                    const std::string nodePath = path + ".nodes["
                        + std::to_string(index) + "]";
                    if (!(*nodes)[index].is_object()) {
                        addError(diagnostics, nodePath, "Expected an object.");
                        parsed = false;
                        continue;
                    }
                    UIFlowNodeDefinition node;
                    parsed = readString((*nodes)[index], "id", node.id,
                        diagnostics, nodePath, true) && parsed;
                    parsed = readString((*nodes)[index], "type", node.type,
                        diagnostics, nodePath, true) && parsed;
                    const auto properties = (*nodes)[index].find("properties");
                    if (properties != (*nodes)[index].end()) {
                        if (!properties->is_object()) {
                            addError(diagnostics, nodePath + ".properties",
                                     "Expected an object.");
                            parsed = false;
                        } else {
                            for (auto it = properties->begin();
                                 it != properties->end(); ++it) {
                                UIFlowValue property;
                                parsed = valueFromJson(it.value(), property,
                                    diagnostics, nodePath + ".properties."
                                        + it.key()) && parsed;
                                node.properties[it.key()] = std::move(property);
                            }
                        }
                    }
                    value.nodes.push_back(std::move(node));
                }
            }
        }
        const auto links = item.find("links");
        if (links != item.end()) {
            if (!links->is_array()) {
                addError(diagnostics, path + ".links", "Expected an array.");
                parsed = false;
            } else {
                for (std::size_t index = 0; index < links->size(); ++index) {
                    const std::string linkPath = path + ".links["
                        + std::to_string(index) + "]";
                    if (!(*links)[index].is_object()) {
                        addError(diagnostics, linkPath, "Expected an object.");
                        parsed = false;
                        continue;
                    }
                    UIFlowLinkDefinition link;
                    parsed = readString((*links)[index], "fromNode", link.fromNode,
                        diagnostics, linkPath, true) && parsed;
                    parsed = readString((*links)[index], "fromPin", link.fromPin,
                        diagnostics, linkPath, true) && parsed;
                    parsed = readString((*links)[index], "toNode", link.toNode,
                        diagnostics, linkPath, true) && parsed;
                    parsed = readString((*links)[index], "toPin", link.toPin,
                        diagnostics, linkPath, true) && parsed;
                    value.links.push_back(std::move(link));
                }
            }
        }
        document.graphs.push_back(std::move(value));
    });

    if (!parsed || hasErrors(diagnostics)) return false;
    std::vector<UIFlowDiagnostic> validation;
    const bool valid = validateUIFlow(document, &validation);
    if (diagnostics != nullptr) {
        diagnostics->insert(diagnostics->end(), validation.begin(), validation.end());
    }
    return valid;
}

bool UIFlowSerializer::serialize(
    const UIFlowDocument& document, std::string& jsonText,
    std::vector<UIFlowDiagnostic>* diagnostics, bool pretty)
{
    if (!validateUIFlow(document, diagnostics)) return false;

    json root = {
        {"schemaVersion", document.schemaVersion},
        {"id", document.id},
        {"defaultEntry", document.defaultEntry},
    };
    root["layers"] = json::array();
    for (const UIFlowLayerDefinition& value : document.layers) {
        root["layers"].push_back({
            {"id", value.id},
            {"order", value.order},
            {"input", uiFlowInputPolicyName(value.inputPolicy)},
            {"blocksLowerInput", value.blocksLowerInput},
            {"maxActiveScreens", value.maxActiveScreens},
        });
    }
    root["slots"] = json::array();
    for (const UIFlowSlotDefinition& value : document.slots) {
        root["slots"].push_back({
            {"id", value.id},
            {"layer", value.layer},
            {"capacity", value.capacity},
            {"restorePrevious", value.restorePrevious},
        });
    }
    root["screens"] = json::array();
    for (const UIFlowScreenDefinition& value : document.screens) {
        json screen = {
            {"id", value.id},
            {"layout", value.layoutAsset},
            {"layer", value.layer},
            {"slot", value.slot},
            {"scope", uiFlowScopeName(value.scope)},
            {"enterAnimation", value.enterAnimation},
            {"exitAnimation", value.exitAnimation},
            {"parameters", json::object()},
        };
        for (const auto& [key, property] : value.parameters) {
            screen["parameters"][key] = valueToJson(property);
        }
        root["screens"].push_back(std::move(screen));
    }
    root["contexts"] = json::array();
    for (const UIFlowContextDefinition& value : document.contexts) {
        json context = {
            {"id", value.id},
            {"priority", value.priority},
            {"slots", json::array()},
        };
        for (const UIFlowSlotAssignment& assignment : value.slots) {
            context["slots"].push_back({
                {"slot", assignment.slot},
                {"operation", uiFlowSlotOperationName(assignment.operation)},
                {"screen", assignment.screen},
            });
        }
        root["contexts"].push_back(std::move(context));
    }
    root["entries"] = json::array();
    for (const UIFlowEntryDefinition& value : document.entries) {
        root["entries"].push_back({
            {"id", value.id},
            {"contexts", value.contexts},
            {"actionGraph", value.actionGraph},
        });
    }
    root["signals"] = json::array();
    for (const UIFlowSignalDefinition& value : document.signals) {
        json signal = {{"id", value.id}};
        writeFields(signal, "payload", value.payload);
        root["signals"].push_back(std::move(signal));
    }
    root["actions"] = json::array();
    for (const UIFlowActionDefinition& value : document.actions) {
        json action = {{"id", value.id}};
        writeFields(action, "inputs", value.inputs);
        root["actions"].push_back(std::move(action));
    }
    root["regions"] = json::array();
    for (const UIFlowRegionDefinition& value : document.regions) {
        json region = {
            {"id", value.id},
            {"initialState", value.initialState},
            {"states", json::array()},
        };
        for (const UIFlowStateDefinition& state : value.states) {
            region["states"].push_back({
                {"id", state.id},
                {"parent", state.parent},
                {"initialChild", state.initialChild},
                {"contexts", state.contexts},
                {"enterGraph", state.enterGraph},
                {"exitGraph", state.exitGraph},
            });
        }
        root["regions"].push_back(std::move(region));
    }
    root["transitions"] = json::array();
    for (const UIFlowTransitionDefinition& value : document.transitions) {
        root["transitions"].push_back({
            {"id", value.id},
            {"region", value.region},
            {"from", value.fromState},
            {"to", value.toState},
            {"trigger", value.triggerSignal},
            {"guard", value.guardExpression},
            {"actionGraph", value.actionGraph},
            {"priority", value.priority},
            {"interrupt", uiFlowInterruptPolicyName(value.interruptPolicy)},
        });
    }
    root["graphs"] = json::array();
    for (const UIFlowGraphDefinition& value : document.graphs) {
        json graph = {
            {"id", value.id},
            {"nodes", json::array()},
            {"links", json::array()},
        };
        for (const UIFlowNodeDefinition& node : value.nodes) {
            json encoded = {
                {"id", node.id},
                {"type", node.type},
                {"properties", json::object()},
            };
            for (const auto& [key, property] : node.properties) {
                encoded["properties"][key] = valueToJson(property);
            }
            graph["nodes"].push_back(std::move(encoded));
        }
        for (const UIFlowLinkDefinition& link : value.links) {
            graph["links"].push_back({
                {"fromNode", link.fromNode},
                {"fromPin", link.fromPin},
                {"toNode", link.toNode},
                {"toPin", link.toPin},
            });
        }
        root["graphs"].push_back(std::move(graph));
    }

    jsonText = root.dump(pretty ? 2 : -1);
    return true;
}

} // namespace ayt::ui
