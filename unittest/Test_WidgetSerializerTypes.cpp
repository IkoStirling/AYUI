// AYUI-Audit-2026-08-26: WidgetSerializer parameterized round-trip.
//
// Every registered built-in must survive deserialize -> serialize ->
// deserialize without changing its concrete type. This
// file parametrizes the same round-trip (deserialize -> serialize -> re-
// parse -> assert type matches) over every registered type so coverage
// is reported in one place.
//
// Pragmatic approach: for each widget type, build a *minimal* JSON
// payload that the deserializer accepts (a no-op `{}` is fine when the
// type accepts empty defaults). For types whose JSON contract requires
// nested payloads (DockArea cards[] + slot weights, TabControl tabs[]
// recursive content, GridPanel row/column + children, Window minSize,
// etc.) we provide the smallest valid payload needed.
#include "AYTest.h"
#include "fixtures/WidgetRoundTrip.h"
#include <nlohmann/json.hpp>
#include "AYUI/WidgetSerializer.h"
#include "AYUI/WidgetFactory.h"
#include <cstdio>
#include <string>

using namespace ayt::ui;

TEST_SUITE(AYUI_WidgetSerializer_AllTypes)

namespace {

// AYUI-Audit-2026-08-26: minimal payload per widget type. Each entry is
// the smallest JSON that the deserializer's matching `if`/`else if`
// branch will accept without throwing. Anything beyond the type key is
// optional defaults that round-trip trivially.
struct TypePayload {
    const char* typeName;
    const char* json;
};

static constexpr TypePayload kPayloads[] = {
    // Leaf widgets — empty payload is enough.
    {"Widget",         R"({"type":"Widget"})"},
    {"Button",         R"({"type":"Button","text":"B"})"},
    {"TextLabel",      R"({"type":"TextLabel","text":"L"})"},
    {"CheckBox",       R"({"type":"CheckBox","text":"C","checked":true})"},
    {"RadioButton",    R"({"type":"RadioButton","text":"R","checked":false,"groupId":0})"},
    {"Slider",         R"({"type":"Slider","min":0,"max":10,"value":3})"},
    {"ProgressBar",    R"({"type":"ProgressBar","min":0,"max":100,"value":50})"},
    {"Spinner",        R"({"type":"Spinner"})"},
    {"TextInput",      R"({"type":"TextInput","text":"input","password":false,"readOnly":false,"maxLength":64,"hAlign":0})"},
    {"TextArea",       R"({"type":"TextArea","text":"area","readOnly":false,"maxLength":1024,"lineHeight":1.2})"},
    {"Tooltip",        R"({"type":"Tooltip","text":"tip","hoverDelay":0.5})"},
    {"Separator",      R"({"type":"Separator","orientation":"horizontal","thickness":1.0,"inset":2.0})"},
    {"ToolBarSeparator", R"({"type":"ToolBarSeparator","orientation":"horizontal","thickness":1.0,"inset":2.0})"},
    {"MenuItem",       R"({"type":"MenuItem","text":"Item","shortcut":"Ctrl+I","hasSubmenu":false})"},
    {"Image",          R"({"type":"Image"})"},
    {"VBox",           R"({"type":"VBox","spacing":3,"gravity":"BottomRight"})"},
    {"HBox",           R"({"type":"HBox","spacing":4,"padding":{"left":1,"top":2,"right":3,"bottom":4}})"},
    {"SplitterHandle", R"({"type":"SplitterHandle"})"},
    {"ColorPicker", R"({"type":"ColorPicker"})"},
    // Containers that accept no payload.
    {"Panel",          R"({"type":"Panel","borderEnabled":true})"},
    {"ScrollView",     R"({"type":"ScrollView"})"},
    // Window: needs title. minSize object is optional but we ship a
    // full one to make sure the object form is parsed cleanly.
    {"Window",         R"({"type":"Window","title":"Win","titleBarHeight":24,"movable":true,"resizable":true,"minSize":{"w":120,"h":80}})"},
    // ScrollBar: needs orientation string.
    {"ScrollBar",      R"({"type":"ScrollBar","orientation":"vertical"})"},
    // ComboBox: items[] + selectedIndex + maxPopupItems.
    {"ComboBox",       R"({"type":"ComboBox","items":["a","b","c"],"selectedIndex":1,"maxPopupItems":5})"},
    // ListView: items[] + selectedIndex + selectionMode + selectedIndices + itemHeight.
    {"ListView",       R"({"type":"ListView","items":["x","y"],"selectedIndex":0,"selectionMode":0,"selectedIndices":[0],"itemHeight":24.0})"},
    {"TileView",       R"({"type":"TileView","items":["x","y"],"selectedIndex":0,"selectionMode":0,"selectedIndices":[0],"tileSize":{"w":104.0,"h":150.0},"tileSpacing":8.0,"infoStripHeight":16.0,"cornerMarkerSize":12.0,"thumbnailAspectRatio":1.0,"labelHeight":28.0})"},
    // Menu: minimal.
    {"Menu",           R"({"type":"Menu","open":false})"},
    // MenuBar: needs menus[] but accepts empty list.
    {"MenuBar",        R"({"type":"MenuBar","menus":[]})"},
    // ToolBar: accepts no payload (itemCount is read-only).
    {"ToolBar",        R"({"type":"ToolBar","itemCount":0})"},
    // StatusBar: needs panels[] — empty list works.
    {"StatusBar",      R"({"type":"StatusBar","panels":[]})"},
    // TreeNode: label/icon/hasChildren/expanded/depth.
    {"TreeNode",       R"({"type":"TreeNode","label":"root","icon":"","hasChildren":true,"expanded":false,"depth":0})"},
    // TreeView: tree[] + selectedIndex + itemHeight.
    {"TreeView",       R"({"type":"TreeView","tree":[{"label":"root","icon":"","hasChildren":false,"expanded":false,"parentIndex":-1}],"selectedIndex":0,"itemHeight":24.0})"},
    // RichText: defaultColor + defaultFontSize + wrapWidth + runs[].
    {"RichText",       R"({"type":"RichText","defaultColor":{"r":1,"g":1,"b":1,"a":1},"defaultFontSize":14,"wrapWidth":200.0,"runs":[{"text":"hi","color":{"r":1,"g":1,"b":1,"a":1},"fontSize":14}]})"},
    {"TabControl",     R"({"type":"TabControl","tabs":[{"label":"A","content":{"type":"TextLabel","text":"body"}}],"selectedIndex":0,"headerHeight":26})"},
    {"GridPanel",      R"({"type":"GridPanel","rowCount":1,"columnCount":1,"cells":[{"row":0,"col":0,"content":{"type":"Button","text":"cell"}}]})"},
    {"DockCard",       R"({"type":"DockCard","id":"card","title":"Card","content":{"type":"TextLabel","text":"body"}})"},
    {"DockOverlay",    R"({"type":"DockOverlay","floating":[{"type":"DockCard","id":"float","x":4,"y":5,"w":200,"h":120}]})"},
    {"DockArea",       R"({"type":"DockArea","slotWeights":{"Left":0.2,"Center":0.8},"cards":[{"type":"DockCard","id":"dock","slot":"Center"}],"floating":[{"type":"DockCard","id":"float","x":8,"y":9,"w":210,"h":140}]})"},
    {"Dimmer",         R"({"type":"Dimmer","scrimColor":{"r":0.1,"g":0.2,"b":0.3,"a":0.4}})"},
    {"Modal",          R"({"type":"Modal","dismissOnDimmerClick":false,"content":{"type":"TextLabel","text":"modal"}})"},
    {"ModalDialog",    R"({"type":"ModalDialog","acceptText":"Apply","rejectText":"Back","bodyContent":{"type":"TextLabel","text":"dialog"}})"},
    {"TabStrip",       R"({"type":"TabStrip","tabs":["One","Two"],"selectedIndex":1,"tabHeight":30,"spacing":6,"indicatorTweenMs":0})"},
};

} // namespace

// AYUI-Audit-2026-08-26: parameterized loop. For every entry in
// kPayloads: (1) deserialize the JSON,
// (2) assert the result is non-null AND its type tag matches,
// (3) serialize the result back to JSON,
// (4) re-parse the serialized JSON and assert it round-trips to a
// non-null widget of the same type.
//
// Aggregate successful inputs; report the specific type only on failure.
TEST_CASE(serializer_round_trip_all_registered_types) {
    int covered = 0;
    int passed  = 0;
    int deserializeFailureCount = 0;
    int typeMismatchCount = 0;
    int roundTripFailureCount = 0;
    for (const TypePayload& entry : kPayloads) {
        ++covered;
        // (1) deserialize.
        ayt::ui::test::WidgetTree source(WidgetSerializer::deserialize(entry.json));
        Widget* widget = source.get();
        if (widget == nullptr) {
            // Factory must at least recognize the type. A null return
            // here means the JSON path returned null, which is a real
            // failure (not "type unknown").
            ++deserializeFailureCount;
            continue;
        }
        // (2) type check via re-serialize — the serializer writes
        // `type` based on the actual subclass; if deserialize() ever
        // fell back to a bare Widget, the re-serialized type would be
        // "Widget" instead of `entry.typeName`.
        ayt::ui::test::WidgetRoundTrip roundTrip(widget, ayt::ui::test::SerializationForm::Widget);
        const std::string& serialized = roundTrip.serialized;
        const auto serializedJson =
            nlohmann::json::parse(serialized, nullptr, false);
        const bool typeOk =
            !serializedJson.is_discarded() &&
            serializedJson.value("type", std::string()) == entry.typeName;
        if (!typeOk) {
            ++typeMismatchCount;
            const std::string actualType = serializedJson.is_discarded()
                ? "<invalid-json>"
                : serializedJson.value("type", std::string("<missing>"));
            std::printf("         serializer type mismatch: expected %s, got %s\n",
                        entry.typeName, actualType.c_str());
        }
        // (3) serialize and (4) re-deserialize.
        Widget* roundTripped = roundTrip.restored.get();
        if (roundTripped != nullptr) {
            ++passed;
            const auto restoredJson = nlohmann::json::parse(
                WidgetSerializer::serializeWidget(roundTripped), nullptr, false);
            if (restoredJson.is_discarded() || restoredJson.value("type", std::string()) != entry.typeName) {
                ++roundTripFailureCount;
                std::printf("         restored type mismatch: %s\n", entry.typeName);
            }
        } else {
            ++roundTripFailureCount;
        }
    }
    // Pin the current factory surface: the table covers all 42 built-ins.
    CHECK(covered == 42);
    CHECK(deserializeFailureCount == 0);
    CHECK(typeMismatchCount == 0);
    CHECK(roundTripFailureCount == 0);
    CHECK(passed == covered);
}

TEST_CASE(serializer_unknown_type_falls_back_to_widget) {
    // AYUI-Audit-2026-08-26: regression check — when a JSON type
    // doesn't match any factory creator, deserialize() falls back to
    // a plain Widget (so the loader can still recover) rather than
    // returning null. Verify that path here.
    const std::string payload = R"({"type":"NoSuchTypeXYZ","id":"orphan"})";
    Widget* widget = WidgetSerializer::deserialize(payload);
    CHECK(widget != nullptr);
    delete widget;
}

TEST_CASE(serializer_null_json_returns_null) {
    // AYUI-Audit-2026-08-26: defensive check — empty input must
    // return null rather than crash or return an uninitialized widget.
    Widget* widget = WidgetSerializer::deserialize("");
    CHECK(widget == nullptr);
}

TEST_CASE(serializer_factory_recognizes_every_payload_type) {
    // AYUI-Audit-2026-08-26: a tighter assertion — for every type in
    // kPayloads the factory MUST have a creator. If a future refactor
    // removes a creator, this case fails fast (instead of waiting for
    // the round-trip case above to fail with a confusing null widget).
    WidgetFactory& factory = WidgetFactory::get();
    int missingCreatorCount = 0;
    for (const TypePayload& entry : kPayloads) {
        if (!factory.isRegistered(entry.typeName)) {
            ++missingCreatorCount;
            std::printf("         missing factory type: %s\n", entry.typeName);
        }
    }
    CHECK(missingCreatorCount == 0);
}

TEST_SUITE_END
