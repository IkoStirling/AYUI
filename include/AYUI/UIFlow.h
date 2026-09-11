#pragma once

#include "AYUI/Version.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ayt::ui {

inline constexpr std::uint32_t kUIFlowSchemaVersion = 1u;

// Lifetime is deliberately independent from Widget ownership. Runtime hosts
// use it to decide which mounted documents survive World/owner transitions.
enum class UIFlowScope : std::uint8_t {
    Application,
    World,
    Owner,
    Transient,
};

// Logical presentation policy. This is unrelated to Widget LayerCachePolicy
// and IRenderBackend::LayerHandle, which are pixel-cache primitives.
enum class UIFlowInputPolicy : std::uint8_t {
    PassThrough,
    ConsumeHandled,
    BlockLower,
};

enum class UIFlowInterruptPolicy : std::uint8_t {
    Queue,
    CancelPrevious,
    ReversePrevious,
    IgnoreIfRunning,
    Coalesce,
};

enum class UIFlowSlotOperation : std::uint8_t {
    Present,
    Hide,
};

enum class UIFlowValueType : std::uint8_t {
    Boolean,
    Integer,
    Number,
    String,
    Entity,
    Asset,
};

// Extension values deliberately preserve recursive JSON data. Host-defined
// graph nodes can therefore round-trip configuration without AYUI knowing the
// node schema. Typed signal/action defaults are still validated as scalars.
struct UIFlowValue {
    using Array = std::vector<UIFlowValue>;
    using Object = std::map<std::string, UIFlowValue>;
    using Storage = std::variant<
        std::monostate, bool, std::int64_t, double, std::string, Array, Object>;

    UIFlowValue() = default;
    UIFlowValue(std::monostate value) : data(value) {}
    UIFlowValue(bool value) : data(value) {}
    UIFlowValue(std::int64_t value) : data(value) {}
    UIFlowValue(double value) : data(value) {}
    UIFlowValue(std::string value) : data(std::move(value)) {}
    UIFlowValue(const char* value) : data(std::string(value)) {}
    UIFlowValue(Array value) : data(std::move(value)) {}
    UIFlowValue(Object value) : data(std::move(value)) {}

    Storage data;
};

struct UIFlowFieldDefinition {
    std::string id;
    UIFlowValueType type = UIFlowValueType::String;
    bool required = false;
    UIFlowValue defaultValue;
};

// Signals are observations consumed by a Flow. Actions are capabilities that
// a host may execute. Both are declared so headless validation and authoring
// do not need to instantiate game code.
struct UIFlowSignalDefinition {
    std::string id;
    std::vector<UIFlowFieldDefinition> payload;
};

struct UIFlowActionDefinition {
    std::string id;
    std::vector<UIFlowFieldDefinition> inputs;
};

struct UIFlowLayerDefinition {
    std::string id;
    std::int32_t order = 0;
    UIFlowInputPolicy inputPolicy = UIFlowInputPolicy::ConsumeHandled;
    bool blocksLowerInput = false;
    // Zero means unbounded. A runtime may still impose a safety limit.
    std::uint32_t maxActiveScreens = 0u;
};

// A Slot is an exclusive replacement channel inside one logical Layer. It is
// separate from a Layer so temporary Contexts can override and later restore
// one part of the presentation without rebuilding unrelated Screens.
struct UIFlowSlotDefinition {
    std::string id;
    std::string layer;
    std::uint32_t capacity = 1u;
    bool restorePrevious = true;
};

// Maps a semantic handler authored in a reusable Widget layout to a declared
// Flow Signal. The current command-event contract carries no dynamic Widget
// value; Signals with required payload fields must provide defaults before
// they can be used here.
struct UIFlowScreenEventBinding {
    std::string handler;
    std::string signal;

    friend bool operator==(const UIFlowScreenEventBinding&,
                           const UIFlowScreenEventBinding&) = default;
};

struct UIFlowScreenDefinition {
    std::string id;
    std::string layoutAsset;
    std::string layer;
    std::string slot;
    UIFlowScope scope = UIFlowScope::Transient;
    std::string enterAnimation;
    std::string exitAnimation;
    std::map<std::string, UIFlowValue> parameters;
    std::vector<UIFlowScreenEventBinding> events;
};

struct UIFlowSlotAssignment {
    std::string slot;
    UIFlowSlotOperation operation = UIFlowSlotOperation::Present;
    std::string screen;
};

struct UIFlowContextDefinition {
    std::string id;
    std::int32_t priority = 0;
    std::vector<UIFlowSlotAssignment> slots;
};

// Entry points select initial contexts and may run an asynchronous action
// graph. A project chooses one entry before any Scene exists.
struct UIFlowEntryDefinition {
    std::string id;
    std::vector<std::string> contexts;
    std::string actionGraph;
};

// States are stored flat for stable IDs. parent/initialChild provide optional
// hierarchy without making the JSON representation recursively fragile.
struct UIFlowStateDefinition {
    std::string id;
    std::string parent;
    std::string initialChild;
    std::vector<std::string> contexts;
    std::string enterGraph;
    std::string exitGraph;
};

// Multiple regions run in parallel; each region owns an independent current
// state. This avoids a Cartesian product of HUD, story and modal states.
struct UIFlowRegionDefinition {
    std::string id;
    std::string initialState;
    std::vector<UIFlowStateDefinition> states;
};

struct UIFlowTransitionDefinition {
    std::string id;
    std::string region;
    std::string fromState;
    std::string toState;
    std::string triggerSignal;
    std::string guardExpression;
    std::string actionGraph;
    std::int32_t priority = 0;
    UIFlowInterruptPolicy interruptPolicy = UIFlowInterruptPolicy::Queue;
};

// Node type IDs remain strings so AYApplication and game modules can register
// capabilities without adding their semantics to AYUI. Unknown node types can
// round-trip through this model with arbitrary JSON properties and links.
struct UIFlowNodeDefinition {
    std::string id;
    std::string type;
    std::map<std::string, UIFlowValue> properties;
};

struct UIFlowLinkDefinition {
    std::string fromNode;
    std::string fromPin;
    std::string toNode;
    std::string toPin;
};

struct UIFlowGraphDefinition {
    std::string id;
    std::vector<UIFlowNodeDefinition> nodes;
    std::vector<UIFlowLinkDefinition> links;
};

struct UIFlowDocument {
    std::uint32_t schemaVersion = kUIFlowSchemaVersion;
    std::string id;
    std::string defaultEntry;
    std::vector<UIFlowLayerDefinition> layers;
    std::vector<UIFlowSlotDefinition> slots;
    std::vector<UIFlowScreenDefinition> screens;
    std::vector<UIFlowContextDefinition> contexts;
    std::vector<UIFlowEntryDefinition> entries;
    std::vector<UIFlowSignalDefinition> signals;
    std::vector<UIFlowActionDefinition> actions;
    std::vector<UIFlowRegionDefinition> regions;
    std::vector<UIFlowTransitionDefinition> transitions;
    std::vector<UIFlowGraphDefinition> graphs;

    const UIFlowLayerDefinition* findLayer(std::string_view value) const;
    const UIFlowSlotDefinition* findSlot(std::string_view value) const;
    const UIFlowScreenDefinition* findScreen(std::string_view value) const;
    const UIFlowContextDefinition* findContext(std::string_view value) const;
    const UIFlowEntryDefinition* findEntry(std::string_view value) const;
    const UIFlowSignalDefinition* findSignal(std::string_view value) const;
    const UIFlowActionDefinition* findAction(std::string_view value) const;
    const UIFlowRegionDefinition* findRegion(std::string_view value) const;
    const UIFlowGraphDefinition* findGraph(std::string_view value) const;
};

enum class UIFlowDiagnosticSeverity : std::uint8_t {
    Warning,
    Error,
};

struct UIFlowDiagnostic {
    UIFlowDiagnosticSeverity severity = UIFlowDiagnosticSeverity::Error;
    std::string path;
    std::string message;
};

// Returns true when no Error diagnostic is produced. Validation is public so
// generated/migrated documents can use exactly the same contract as parsed
// .uiflow.json assets.
bool validateUIFlow(const UIFlowDocument& document,
                    std::vector<UIFlowDiagnostic>* diagnostics = nullptr);

class UIFlowSerializer {
public:
    static bool deserialize(std::string_view jsonText,
                            UIFlowDocument& document,
                            std::vector<UIFlowDiagnostic>* diagnostics = nullptr);
    static bool serialize(const UIFlowDocument& document,
                          std::string& jsonText,
                          std::vector<UIFlowDiagnostic>* diagnostics = nullptr,
                          bool pretty = true);
};

const char* uiFlowScopeName(UIFlowScope value);
const char* uiFlowInputPolicyName(UIFlowInputPolicy value);
const char* uiFlowInterruptPolicyName(UIFlowInterruptPolicy value);
const char* uiFlowSlotOperationName(UIFlowSlotOperation value);
const char* uiFlowValueTypeName(UIFlowValueType value);

} // namespace ayt::ui
