#include "AYUI/UIFlowGraphNodeRegistry.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace ayt::ui {
namespace {

bool isNull(const UIFlowValue& value)
{
    return std::holds_alternative<std::monostate>(value.data);
}

bool matches(const UIFlowValue& value, UIFlowValueType type)
{
    if (isNull(value)) return true;
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

void addError(std::vector<UIFlowDiagnostic>* diagnostics,
              std::string path, std::string message)
{
    if (diagnostics != nullptr) diagnostics->push_back({
        UIFlowDiagnosticSeverity::Error, std::move(path), std::move(message)});
}

} // namespace

const UIFlowGraphPinTypeDefinition* findUIFlowGraphPin(
    const UIFlowGraphNodeTypeDefinition& type,
    std::string_view pinId,
    UIFlowGraphPinDirection direction) noexcept
{
    const auto found = std::find_if(type.pins.begin(), type.pins.end(),
        [&](const UIFlowGraphPinTypeDefinition& pin) {
            return pin.id == pinId && pin.direction == direction;
        });
    return found == type.pins.end() ? nullptr : &*found;
}

bool areUIFlowGraphPinsCompatible(
    const UIFlowGraphPinTypeDefinition& source,
    const UIFlowGraphPinTypeDefinition& target) noexcept
{
    if (source.direction != UIFlowGraphPinDirection::Output
        || target.direction != UIFlowGraphPinDirection::Input) return false;
    if (source.kind != target.kind) return false;
    if (source.kind == UIFlowGraphPinKind::Execution) return true;
    if (source.valueType == target.valueType) return true;
    return source.valueType == UIFlowValueType::Integer
        && target.valueType == UIFlowValueType::Number;
}

bool UIFlowGraphNodeRegistry::registerType(
    UIFlowGraphNodeTypeDefinition definition, std::string* error)
{
    if (definition.type.empty()) {
        if (error != nullptr) *error = "Graph node type must not be empty.";
        return false;
    }
    if (find(definition.type) != nullptr) {
        if (error != nullptr) *error = "Graph node type is already registered.";
        return false;
    }
    std::unordered_set<std::string> pins;
    for (const UIFlowGraphPinTypeDefinition& pin : definition.pins) {
        const std::string key = std::to_string(
            static_cast<unsigned>(pin.direction)) + ":" + pin.id;
        if (pin.id.empty() || !pins.insert(key).second) {
            if (error != nullptr) {
                *error = "Graph node pins require unique non-empty IDs per direction.";
            }
            return false;
        }
    }
    std::unordered_set<std::string> properties;
    for (const UIFlowFieldDefinition& property : definition.properties) {
        if (property.id.empty() || !properties.insert(property.id).second
            || !matches(property.defaultValue, property.type)) {
            if (error != nullptr) {
                *error = "Graph node properties require unique typed IDs.";
            }
            return false;
        }
    }
    if (definition.displayName.empty()) definition.displayName = definition.type;
    _types.push_back(std::move(definition));
    if (error != nullptr) error->clear();
    return true;
}

bool UIFlowGraphNodeRegistry::unregisterType(std::string_view type)
{
    const auto found = std::find_if(_types.begin(), _types.end(),
        [type](const UIFlowGraphNodeTypeDefinition& value) {
            return value.type == type;
        });
    if (found == _types.end()) return false;
    _types.erase(found);
    return true;
}

const UIFlowGraphNodeTypeDefinition* UIFlowGraphNodeRegistry::find(
    std::string_view type) const noexcept
{
    const auto found = std::find_if(_types.begin(), _types.end(),
        [type](const UIFlowGraphNodeTypeDefinition& value) {
            return value.type == type;
        });
    return found == _types.end() ? nullptr : &*found;
}

bool validateUIFlowGraphNodes(
    const UIFlowDocument& document,
    const UIFlowGraphNodeRegistry& registry,
    std::vector<UIFlowDiagnostic>* diagnostics)
{
    if (diagnostics != nullptr) diagnostics->clear();
    bool valid = true;
    for (std::size_t graphIndex = 0; graphIndex < document.graphs.size();
         ++graphIndex) {
        const UIFlowGraphDefinition& graph = document.graphs[graphIndex];
        const std::string graphPath = "graphs[" + std::to_string(graphIndex) + "]";
        for (std::size_t nodeIndex = 0; nodeIndex < graph.nodes.size();
             ++nodeIndex) {
            const UIFlowNodeDefinition& node = graph.nodes[nodeIndex];
            const std::string nodePath = graphPath + ".nodes["
                + std::to_string(nodeIndex) + "]";
            const UIFlowGraphNodeTypeDefinition* type = registry.find(node.type);
            if (type == nullptr) {
                addError(diagnostics, nodePath + ".type",
                         "Graph node type is not registered in this host.");
                valid = false;
                continue;
            }
            for (const UIFlowFieldDefinition& property : type->properties) {
                const auto found = node.properties.find(property.id);
                if (found == node.properties.end()) {
                    if (property.required && isNull(property.defaultValue)) {
                        addError(diagnostics, nodePath + ".properties."
                            + property.id, "Required node property is missing.");
                        valid = false;
                    }
                    continue;
                }
                if (!matches(found->second, property.type)) {
                    addError(diagnostics, nodePath + ".properties."
                        + property.id, "Node property has the wrong value type.");
                    valid = false;
                }
            }
        }
        for (std::size_t linkIndex = 0; linkIndex < graph.links.size();
             ++linkIndex) {
            const UIFlowLinkDefinition& link = graph.links[linkIndex];
            const std::string linkPath = graphPath + ".links["
                + std::to_string(linkIndex) + "]";
            const auto sourceNode = std::find_if(
                graph.nodes.begin(), graph.nodes.end(), [&](const auto& value) {
                    return value.id == link.fromNode;
                });
            const auto targetNode = std::find_if(
                graph.nodes.begin(), graph.nodes.end(), [&](const auto& value) {
                    return value.id == link.toNode;
                });
            if (sourceNode == graph.nodes.end()
                || targetNode == graph.nodes.end()) continue;
            const UIFlowGraphNodeTypeDefinition* sourceType =
                registry.find(sourceNode->type);
            const UIFlowGraphNodeTypeDefinition* targetType =
                registry.find(targetNode->type);
            if (sourceType == nullptr || targetType == nullptr) continue;
            const UIFlowGraphPinTypeDefinition* sourcePin = findUIFlowGraphPin(
                *sourceType, link.fromPin, UIFlowGraphPinDirection::Output);
            const UIFlowGraphPinTypeDefinition* targetPin = findUIFlowGraphPin(
                *targetType, link.toPin, UIFlowGraphPinDirection::Input);
            if (sourcePin == nullptr) {
                addError(diagnostics, linkPath + ".fromPin",
                         "Link source pin is not an output of its node type.");
                valid = false;
            }
            if (targetPin == nullptr) {
                addError(diagnostics, linkPath + ".toPin",
                         "Link target pin is not an input of its node type.");
                valid = false;
            }
            if (sourcePin != nullptr && targetPin != nullptr
                && !areUIFlowGraphPinsCompatible(*sourcePin, *targetPin)) {
                addError(diagnostics, linkPath,
                         "Link connects incompatible Graph pin types.");
                valid = false;
            }
        }
    }
    return valid;
}

} // namespace ayt::ui
