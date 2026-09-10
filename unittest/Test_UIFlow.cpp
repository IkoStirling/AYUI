#include "AYTest.h"
#include "AYUI/UIFlow.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace ayt::ui;

TEST_SUITE(AYUI_UIFlowContract)

TEST_CASE(flow_document_round_trips_parallel_regions_and_extensible_nodes)
{
    const char* source = R"json({
        "schemaVersion": 1,
        "id": "game_ui",
        "defaultEntry": "Boot",
        "layers": [
            {"id":"screen","order":0,"input":"consumeHandled","maxActiveScreens":1},
            {"id":"hud","order":100,"input":"passThrough"},
            {"id":"transition","order":1000,"input":"blockLower","blocksLowerInput":true}
        ],
        "slots": [
            {"id":"main","layer":"screen","capacity":1,"restorePrevious":true},
            {"id":"hud.primary","layer":"hud","capacity":1,"restorePrevious":true}
        ],
        "screens": [
            {
                "id":"main_menu",
                "layout":"ui/main_menu.ui.json",
                "layer":"screen",
                "slot":"main",
                "scope":"application",
                "enterAnimation":"enter",
                "exitAnimation":"exit",
                "parameters":{"title":"Aliyat","attempt":2,"dim":0.25,"enabled":true}
            },
            {
                "id":"gameplay_hud",
                "layout":"ui/gameplay_hud.ui.json",
                "layer":"hud",
                "slot":"hud.primary",
                "scope":"world"
            }
        ],
        "contexts": [
            {"id":"Boot","priority":0,"slots":[
                {"slot":"main","operation":"present","screen":"main_menu"}
            ]},
            {"id":"Gameplay","priority":0,"slots":[
                {"slot":"hud.primary","operation":"present","screen":"gameplay_hud"}
            ]}
        ],
        "entries": [
            {"id":"Boot","contexts":["Boot"],"actionGraph":"boot_graph"}
        ],
        "signals": [
            {"id":"ui.startGame","payload":[
                {"id":"world","type":"string","required":true},
                {"id":"fadeMs","type":"number","default":250.0}
            ]},
            {"id":"story.cutscene","payload":[]}
        ],
        "actions": [
            {"id":"world.load","inputs":[
                {"id":"world","type":"string","required":true}
            ]}
        ],
        "regions": [
            {"id":"application","initialState":"MainMenu","states":[
                {"id":"MainMenu","contexts":["Boot"]},
                {"id":"Loading"},
                {"id":"Gameplay","contexts":["Gameplay"]}
            ]},
            {"id":"story","initialState":"Idle","states":[
                {"id":"Idle"},
                {"id":"Cutscene"}
            ]}
        ],
        "transitions": [
            {
                "id":"start_game",
                "region":"application",
                "from":"MainMenu",
                "to":"Loading",
                "trigger":"ui.startGame",
                "guard":"payload.world != ''",
                "actionGraph":"load_world",
                "priority":10,
                "interrupt":"cancelPrevious"
            }
        ],
        "graphs": [
            {"id":"boot_graph","nodes":[
                {"id":"present","type":"ui.present","properties":{"screen":"main_menu"}}
            ],"links":[]},
            {"id":"load_world","nodes":[
                {"id":"fade","type":"ui.playAnimation","properties":{"clip":"exit"}},
                {"id":"load","type":"game.world.load","properties":{"world":"$payload.world"}}
            ],"links":[
                {"fromNode":"fade","fromPin":"completed","toNode":"load","toPin":"execute"}
            ]}
        ]
    })json";

    UIFlowDocument document;
    std::vector<UIFlowDiagnostic> diagnostics;
    CHECK(UIFlowSerializer::deserialize(source, document, &diagnostics));
    CHECK(diagnostics.empty());
    CHECK(document.id == "game_ui");
    CHECK(document.layers.size() == 3u);
    CHECK(document.regions.size() == 2u);
    CHECK(document.findScreen("main_menu") != nullptr);
    CHECK(document.findContext("Gameplay") != nullptr);
    CHECK(document.findGraph("load_world") != nullptr);
    CHECK(document.findGraph("load_world")->nodes[1].type == "game.world.load");
    CHECK(std::get<std::int64_t>(
        document.findScreen("main_menu")->parameters.at("attempt").data) == 2);

    std::string encoded;
    CHECK(UIFlowSerializer::serialize(document, encoded, &diagnostics, false));
    CHECK(diagnostics.empty());

    UIFlowDocument reloaded;
    CHECK(UIFlowSerializer::deserialize(encoded, reloaded, &diagnostics));
    CHECK(diagnostics.empty());
    CHECK(reloaded.defaultEntry == "Boot");
    CHECK(reloaded.findRegion("story") != nullptr);
    CHECK(reloaded.findGraph("load_world") != nullptr);
    CHECK(reloaded.findGraph("load_world")->nodes[1].type == "game.world.load");
    CHECK(reloaded.transitions[0].interruptPolicy
          == UIFlowInterruptPolicy::CancelPrevious);
}

TEST_CASE(flow_validation_reports_cross_reference_and_type_errors)
{
    UIFlowDocument document;
    document.id = "broken";
    document.defaultEntry = "missing_entry";
    document.layers.push_back(UIFlowLayerDefinition{"hud"});
    document.slots.push_back(UIFlowSlotDefinition{"hud.primary", "missing"});
    document.screens.push_back(UIFlowScreenDefinition{
        "hud", "", "hud", "hud.primary", UIFlowScope::World});
    document.contexts.push_back(UIFlowContextDefinition{
        "Gameplay", 0,
        {UIFlowSlotAssignment{"hud.primary",
                              UIFlowSlotOperation::Present,
                              "missing_screen"}}});
    UIFlowSignalDefinition signal;
    signal.id = "region.enter";
    signal.payload.push_back(UIFlowFieldDefinition{
        "source", UIFlowValueType::Entity, false, std::int64_t{42}});
    document.signals.push_back(std::move(signal));

    std::vector<UIFlowDiagnostic> diagnostics;
    CHECK_FALSE(validateUIFlow(document, &diagnostics));
    CHECK(diagnostics.size() >= 5u);
    CHECK(diagnostics[0].severity == UIFlowDiagnosticSeverity::Error);
}

TEST_CASE(flow_validation_rejects_immediate_hierarchy_cycles_and_bad_links)
{
    UIFlowDocument document;
    document.id = "cycles";
    UIFlowRegionDefinition region;
    region.id = "hud";
    region.initialState = "normal";
    region.states.push_back(UIFlowStateDefinition{"normal", "hidden"});
    region.states.push_back(UIFlowStateDefinition{"hidden", "normal"});
    document.regions.push_back(std::move(region));
    UIFlowGraphDefinition graph;
    graph.id = "bad_graph";
    graph.nodes.push_back(UIFlowNodeDefinition{"existing", "custom.node"});
    graph.links.push_back(UIFlowLinkDefinition{
        "missing", "completed", "existing", "execute"});
    document.graphs.push_back(std::move(graph));

    std::vector<UIFlowDiagnostic> diagnostics;
    CHECK_FALSE(validateUIFlow(document, &diagnostics));
    CHECK(diagnostics.size() == 3u);
}

TEST_CASE(flow_parser_round_trips_structured_extension_properties)
{
    UIFlowDocument document;
    std::vector<UIFlowDiagnostic> diagnostics;
    CHECK(UIFlowSerializer::deserialize(R"json({
        "schemaVersion":1,
        "id":"structured_property",
        "graphs":[{"id":"g","nodes":[{
            "id":"n","type":"plugin.node","properties":{
                "nested":{"x":1,"tags":["world",true,2.5]}
            }
        }]}]
    })json", document, &diagnostics));
    CHECK(diagnostics.empty());
    CHECK(std::holds_alternative<UIFlowValue::Object>(
        document.graphs[0].nodes[0].properties.at("nested").data));

    std::string encoded;
    CHECK(UIFlowSerializer::serialize(document, encoded, &diagnostics, false));
    UIFlowDocument reloaded;
    CHECK(UIFlowSerializer::deserialize(encoded, reloaded, &diagnostics));
    const UIFlowValue::Object& nested = std::get<UIFlowValue::Object>(
        reloaded.graphs[0].nodes[0].properties.at("nested").data);
    CHECK(std::get<std::int64_t>(nested.at("x").data) == 1);
    CHECK(std::get<UIFlowValue::Array>(nested.at("tags").data).size() == 3u);
}

TEST_CASE(flow_parser_requires_schema_version_and_checks_unsigned_ranges)
{
    UIFlowDocument document;
    std::vector<UIFlowDiagnostic> diagnostics;
    CHECK_FALSE(UIFlowSerializer::deserialize(
        R"json({"id":"missing_schema"})json", document, &diagnostics));
    CHECK(diagnostics.size() == 1u);
    CHECK(diagnostics[0].path == "schemaVersion");

    diagnostics.clear();
    CHECK_FALSE(UIFlowSerializer::deserialize(R"json({
        "schemaVersion":1,
        "id":"bad_capacity",
        "layers":[{"id":"hud"}],
        "slots":[{"id":"hud.primary","layer":"hud","capacity":-1}]
    })json", document, &diagnostics));
    CHECK(diagnostics.size() == 1u);
    CHECK(diagnostics[0].path == "slots[0].capacity");
}

TEST_SUITE_END
