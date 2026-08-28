#include "AYTest.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/GridPanel.h"
#include "AYUI/TextLabel.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/TabControl.h"
#include "AYUI/TabStrip.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"

#include <nlohmann/json.hpp>

using namespace ayt::ui;

namespace {

class TabContentLifetimeProbe final : public Widget {
public:
    explicit TabContentLifetimeProbe(int& destroyCount)
        : _destroyCount(destroyCount) {}
    ~TabContentLifetimeProbe() override { ++_destroyCount; }

private:
    int& _destroyCount;
};

} // namespace

TEST_SUITE(AYUI_SerializerCompleteness)

TEST_CASE(serializer_grid_round_trip_preserves_attachments) {
    const char* payload = R"({
        "type":"GridPanel",
        "rowCount":2,
        "columnCount":3,
        "rowDefs":[
            {"policy":"Fixed","value":32},
            {"policy":"Stretch","value":2}
        ],
        "columnDefs":[
            {"policy":"Fixed","value":80},
            {"policy":"Stretch","value":1},
            {"policy":"Stretch","value":3}
        ],
        "padding":{"left":1,"top":2,"right":3,"bottom":4},
        "spacing":{"horizontal":5,"vertical":6},
        "cells":[{
            "row":0,"col":1,"rowSpan":2,"colSpan":2,
            "hAlign":"Right","vAlign":"Bottom",
            "content":{"type":"TextLabel","id":"grid-body","text":"Body"}
        }]
    })";

    Widget* root = WidgetSerializer::deserialize(payload);
    GridPanel* grid = dynamic_cast<GridPanel*>(root);
    CHECK(grid != nullptr);
    if (grid != nullptr) {
        CHECK(grid->getChildren().size() == 1);
        CHECK(grid->getRowCount() == 2);
        CHECK(grid->getColumnCount() == 3);
        CHECK(grid->getRowDef(0).policy == GridPanel::SizePolicy::Fixed);
        CHECK_FLOAT_EQ(grid->getRowDef(0).value, 32.0f, 1e-5f);
        CHECK_FLOAT_EQ(grid->getColumnDef(2).value, 3.0f, 1e-5f);
        const GridPanel::CellInfo* cell = grid->findCell(0, 1);
        CHECK(cell != nullptr);
        if (cell != nullptr) {
            CHECK(cell->widget != nullptr);
            CHECK(cell->rowSpan == 2);
            CHECK(cell->colSpan == 2);
            CHECK(cell->hAlign == GridPanel::HAlign::Right);
            CHECK(cell->vAlign == GridPanel::VAlign::Bottom);
        }

        const auto encoded = nlohmann::json::parse(
            WidgetSerializer::serializeWidget(grid));
        CHECK(encoded.contains("cells"));
        CHECK(!encoded.contains("children"));
        CHECK(encoded["cells"].size() == 1);

        Widget* restoredRoot = WidgetSerializer::deserialize(encoded.dump());
        GridPanel* restored = dynamic_cast<GridPanel*>(restoredRoot);
        CHECK(restored != nullptr);
        if (restored != nullptr) {
            const GridPanel::CellInfo* restoredCell = restored->findCell(0, 1);
            CHECK(restored->getChildren().size() == 1);
            CHECK(restoredCell != nullptr);
            if (restoredCell != nullptr) {
                CHECK(restoredCell->rowSpan == 2);
                CHECK(restoredCell->colSpan == 2);
                CHECK(restoredCell->widget != nullptr);
                if (restoredCell->widget != nullptr) {
                    CHECK(restoredCell->widget->getId() == "grid-body");
                }
            }
        }
        destroyWidgetTree(restoredRoot);
    }
    destroyWidgetTree(root);
}

TEST_CASE(serializer_grid_legacy_children_do_not_duplicate) {
    Widget* root = WidgetSerializer::deserialize(R"({
        "type":"GridPanel","rowCount":1,"columnCount":2,
        "children":[
            {"type":"TextLabel","id":"left"},
            {"type":"TextLabel","id":"right"}
        ]
    })");
    GridPanel* grid = dynamic_cast<GridPanel*>(root);
    CHECK(grid != nullptr);
    if (grid != nullptr) {
        CHECK(grid->getChildren().size() == 2);
        CHECK(grid->getCell(0, 0) != nullptr);
        CHECK(grid->getCell(0, 1) != nullptr);
    }
    destroyWidgetTree(root);
}

TEST_CASE(serializer_modal_family_and_tab_strip_are_structured) {
    Widget* modalRoot = WidgetSerializer::deserialize(R"({
        "type":"Modal","dismissOnDimmerClick":false,
        "dimmer":{"scrimColor":{"r":0.1,"g":0.2,"b":0.3,"a":0.4}},
        "content":{"type":"TextLabel","id":"modal-content","text":"M"}
    })");
    Modal* modal = dynamic_cast<Modal*>(modalRoot);
    CHECK(modal != nullptr);
    if (modal != nullptr) {
        CHECK(!modal->isDismissOnDimmerClick());
        CHECK(modal->getContent() != nullptr);
        CHECK(modal->getContent()->getId() == "modal-content");
        CHECK(modal->getDimmer() != nullptr);
        const auto encoded = nlohmann::json::parse(
            WidgetSerializer::serializeWidget(modal));
        CHECK(encoded.contains("content"));
        CHECK(!encoded.contains("children"));
    }
    destroyWidgetTree(modalRoot);

    Widget* dialogRoot = WidgetSerializer::deserialize(R"({
        "type":"ModalDialog","acceptText":"Apply","rejectText":"Back",
        "bodyContent":{"type":"TextLabel","id":"dialog-body","text":"D"}
    })");
    ModalDialog* dialog = dynamic_cast<ModalDialog*>(dialogRoot);
    CHECK(dialog != nullptr);
    if (dialog != nullptr) {
        CHECK(dialog->getAcceptText() == L"Apply");
        CHECK(dialog->getRejectText() == L"Back");
        CHECK(dialog->getBodyContent() != nullptr);
        CHECK(dialog->getBodyContent()->getId() == "dialog-body");
        const auto encoded = nlohmann::json::parse(
            WidgetSerializer::serializeWidget(dialog));
        CHECK(encoded.contains("bodyContent"));
        CHECK(!encoded.contains("children"));
    }
    destroyWidgetTree(dialogRoot);

    Widget* stripRoot = WidgetSerializer::deserialize(R"({
        "type":"TabStrip","tabs":["One","Two","Three"],
        "selectedIndex":2,"tabHeight":31,"spacing":7,"indicatorTweenMs":0
    })");
    TabStrip* strip = dynamic_cast<TabStrip*>(stripRoot);
    CHECK(strip != nullptr);
    if (strip != nullptr) {
        CHECK(strip->getTabCount() == 3);
        CHECK(strip->getSelectedIndex() == 2);
        CHECK_FLOAT_EQ(strip->getTabHeight(), 31.0f, 1e-5f);
        CHECK_FLOAT_EQ(strip->getSpacing(), 7.0f, 1e-5f);
        const auto encoded = nlohmann::json::parse(
            WidgetSerializer::serializeWidget(strip));
        CHECK(encoded["tabs"].size() == 3);
        CHECK(!encoded.contains("children"));
    }
    destroyWidgetTree(stripRoot);
}

TEST_CASE(serializer_owned_tab_contents_survive_switch_and_destroy_once) {
    int destroyCount = 0;
    auto* tabs = new TabControl();
    tabs->addTabOwned(L"One", new TabContentLifetimeProbe(destroyCount));
    tabs->addTabOwned(L"Two", new TabContentLifetimeProbe(destroyCount));
    tabs->setSelectedIndex(1);

    CHECK(destroyCount == 0);
    destroyWidgetTree(tabs);
    CHECK(destroyCount == 2);
}

TEST_CASE(serializer_dock_area_round_trip_preserves_docked_and_floating_cards) {
    Widget* root = WidgetSerializer::deserialize(R"({
        "type":"DockArea","id":"dock",
        "slotWeights":{"Left":0.25,"Center":0.75},
        "slotMinSizes":{"Left":120},
        "cards":[{
            "type":"DockCard","id":"left-card","slot":"Left",
            "title":"Inspector","content":{"type":"TextLabel","id":"left-body"}
        }],
        "floating":[{
            "type":"DockCard","id":"float-card","title":"Float",
            "x":13,"y":17,"w":333,"h":222
        }]
    })");
    DockArea* dock = dynamic_cast<DockArea*>(root);
    CHECK(dock != nullptr);
    if (dock != nullptr) {
        CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
        CHECK_FLOAT_EQ(dock->getSlotWeight(DockArea::Slot::Left), 0.25f, 1e-5f);
        CHECK_FLOAT_EQ(dock->getSlotMinSize(DockArea::Slot::Left), 120.0f, 1e-5f);
        CHECK(dock->getOverlay() != nullptr);
        if (dock->getOverlay() != nullptr) {
            CHECK(dock->getOverlay()->getFloatingCardCount() == 1);
            DockCard* floating = dock->getOverlay()->getFloatingCard(0);
            CHECK(floating != nullptr);
            if (floating != nullptr) {
                CHECK_FLOAT_EQ(floating->getPosition().x, 13.0f, 1e-5f);
                CHECK_FLOAT_EQ(floating->getSize().x, 333.0f, 1e-5f);
            }
        }

        const auto encoded = nlohmann::json::parse(
            WidgetSerializer::serializeWidget(dock));
        CHECK(encoded["cards"].size() == 1);
        CHECK(encoded["floating"].size() == 1);
        CHECK(!encoded.contains("children"));

        Widget* restoredRoot = WidgetSerializer::deserialize(encoded.dump());
        DockArea* restored = dynamic_cast<DockArea*>(restoredRoot);
        CHECK(restored != nullptr);
        if (restored != nullptr) {
            CHECK(restored->getCardCount(DockArea::Slot::Left) == 1);
            CHECK(restored->getOverlay()->getFloatingCardCount() == 1);
        }
        destroyWidgetTree(restoredRoot);
    }
    destroyWidgetTree(root);
}

TEST_SUITE_END
