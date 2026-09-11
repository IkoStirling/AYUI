#pragma once

#include "AYUI/UIFlow.h"

#include <string>
#include <string_view>
#include <vector>

namespace ayt::ui {

enum class UIFlowGraphPinDirection : std::uint8_t {
    Input,
    Output,
};

enum class UIFlowGraphPinKind : std::uint8_t {
    Execution,
    Value,
};

struct UIFlowGraphPinTypeDefinition {
    std::string id;
    UIFlowGraphPinDirection direction = UIFlowGraphPinDirection::Input;
    UIFlowGraphPinKind kind = UIFlowGraphPinKind::Execution;
    UIFlowValueType valueType = UIFlowValueType::String;
};

// Authoring metadata only. Registering a type does not install its executor;
// AYApplication/game hosts retain ownership of Graph execution semantics.
struct UIFlowGraphNodeTypeDefinition {
    std::string type;
    std::string displayName;
    std::string category;
    std::vector<UIFlowGraphPinTypeDefinition> pins;
    std::vector<UIFlowFieldDefinition> properties;
};

class UIFlowGraphNodeRegistry {
public:
    bool registerType(UIFlowGraphNodeTypeDefinition definition,
                      std::string* error = nullptr);
    bool unregisterType(std::string_view type);
    void clear() noexcept { _types.clear(); }

    [[nodiscard]] const UIFlowGraphNodeTypeDefinition* find(
        std::string_view type) const noexcept;
    [[nodiscard]] const std::vector<UIFlowGraphNodeTypeDefinition>& types()
        const noexcept { return _types; }

private:
    std::vector<UIFlowGraphNodeTypeDefinition> _types;
};

// Strict optional validation layered on top of validateUIFlow(). The base
// wire contract continues to preserve unknown extension nodes; an authoring
// host opts into this registry when it knows the available node vocabulary.
bool validateUIFlowGraphNodes(
    const UIFlowDocument& document,
    const UIFlowGraphNodeRegistry& registry,
    std::vector<UIFlowDiagnostic>* diagnostics = nullptr);

} // namespace ayt::ui
