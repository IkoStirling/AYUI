#include "AYTest.h"
#include "AYListView.h"
#include "AYScrollBar.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
#include "AYStyle.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_ListView)

// C-5: default state — empty items, no selection, no vbar selectedIndex,
// single scrollbar pre-created.
TEST_CASE(listview_initial_state) {
    ListView lv;
    CHECK(lv.getItemCount() == 0u);
    CHECK(lv.getSelectedIndex() == -1);
    CHECK(lv.getVerticalScrollBar() != nullptr);
    CHECK_FLOAT_EQ(lv.getItemHeight(), 24.0f, 1e-5f);
}

// C-5: setItems + addItem + clearItems.
TEST_CASE(listview_data_management) {
    ListView lv;
    std::vector<std::wstring> items{L"alpha", L"beta", L"gamma"};
    lv.setItems(items);
    CHECK(lv.getItemCount() == 3u);
    CHECK(lv.getItem(0) == L"alpha");
    CHECK(lv.getItem(2) == L"gamma");

    lv.addItem(L"delta");
    CHECK(lv.getItemCount() == 4u);
    CHECK(lv.getItem(3) == L"delta");

    lv.clearItems();
    CHECK(lv.getItemCount() == 0u);
    CHECK(lv.getSelectedIndex() == -1);
}

// C-5: setSelectedIndex fires callback only on real change; clamp -1 /
// out-of-range to -1.
TEST_CASE(listview_set_selected_index_callback_idempotent) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    int changes = 0;
    lv.setOnSelectionChanged([&](int) { ++changes; });

    lv.setSelectedIndex(1);
    CHECK(lv.getSelectedIndex() == 1);
    CHECK(changes == 1);

    lv.setSelectedIndex(1);    // idempotent
    CHECK(changes == 1);

    lv.setSelectedIndex(99);   // clamps to -1
    CHECK(lv.getSelectedIndex() == -1);
    CHECK(changes == 2);

    lv.setSelectedIndex(-5);   // already -1 — no callback
    CHECK(lv.getSelectedIndex() == -1);
    CHECK(changes == 2);
}

// C-5: clicking a row selects it; setSelectedIndex while iterating rows
// toggles their _selected flag.
TEST_CASE(listview_row_click_selects) {
    ListView lv;
    lv.setItems({L"x", L"y", L"z"});
    lv.setSize(FVector2(160.0f, 200.0f));
    lv.setPosition(FVector2(0.0f, 0.0f));

    int selChanges = 0;
    lv.setOnSelectionChanged([&](int idx) {
        if (idx == 2) ++selChanges;
    });

    // Row at index 2 sits at y = 2*24 = 48 to 72 (relative to list origin).
    // World position depends on parent world — we use the list's world.
    const FVector2 world = lv.getWorldBounds().getMin();
    const FVector2 rowPos(world.x + 10.0f, world.y + 48.0f + 12.0f);
    lv.onMouseButtonUp(UIMouseEvent(rowPos, 0));
    CHECK(selChanges == 1);
    CHECK(lv.getSelectedIndex() == 2);
}

// C-5: factory + serializer round-trip preserves items + selected index +
// item height.
TEST_CASE(listview_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("ListView"));

    Widget* widget = factory.create("ListView");
    CHECK_NOT_NULL(widget);
    ListView* original = dynamic_cast<ListView*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("lv_files");
    original->setItems({L"readme.md", L"main.cpp", L"CMakeLists.txt"});
    original->setSelectedIndex(1);
    original->setItemHeight(28.0f);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"ListView\"") != std::string::npos);
    CHECK(json.find("readme.md") != std::string::npos);
    CHECK(json.find("CMakeLists.txt") != std::string::npos);
    CHECK(json.find("\"selectedIndex\": 1") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    ListView* restoredLv = dynamic_cast<ListView*>(restored);
    CHECK_NOT_NULL(restoredLv);
    CHECK(restoredLv->getId() == "lv_files");
    CHECK(restoredLv->getItemCount() == 3u);
    CHECK(restoredLv->getItem(2) == L"CMakeLists.txt");
    CHECK(restoredLv->getSelectedIndex() == 1);
    CHECK_FLOAT_EQ(restoredLv->getItemHeight(), 28.0f, 1e-5f);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// C-5: render emits background + at least one row rect.
TEST_CASE(listview_render_emits_rows) {
    ListView lv;
    lv.setItems({L"row1", L"row2"});
    lv.setSize(FVector2(120.0f, 80.0f));
    lv.setPosition(FVector2(0.0f, 0.0f));

    MockRenderer renderer;
    lv.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    CHECK(rectCount >= 1);
}

// C-5: getSelectedItem returns the item text at _selectedIndex, or empty
// when -1.
TEST_CASE(listview_get_selected_item) {
    ListView lv;
    lv.setItems({L"first", L"second"});
    lv.setSelectedIndex(1);
    CHECK(lv.getSelectedItem() == L"second");

    lv.setSelectedIndex(-1);
    CHECK(lv.getSelectedItem() == L"");
}

TEST_SUITE_END
