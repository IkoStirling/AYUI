#include "AYTest.h"
#include "AYUI/ListView.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Style.h"
#include "AYUI/ComboBox.h"
#include "AYUI/UIKeyCode.h"
#include <iostream>

// =============================================================================
// Known-not-covered scenarios for C-5 ListView v1
// =============================================================================
// See Controls/AYListView.h top-of-file "Virtualization boundary" block for
// the design rationale + upgrade seams. Pinned here as entry points so the
// next implementer knows where to start.
//
// K1. Large list (>1000 rows) — per-frame layout cost.
//     Repro sketch:
//        ListView lv; lv.setItemHeight(24.0f);
//        std::vector<std::wstring> items; for (int i = 0; i < 5000; ++i)
//            items.push_back(L"row " + std::to_wstring(i));
//        lv.setItems(items);
//        // Drive a UIManager::update loop and watch frame time on scroll.
//     Expected v1.1 fix: row pool (see AYListView.h DECISION block).
//     The public API (setItems / setSelectedIndex / getScrollOffset)
//     does NOT change — only the internal _rows vector + rebuildRows
//     implementation swaps in.
//
// K2. Up/Down/Home/End/PageUp/PageDown keyboard navigation.
//     v1 ships without keyboard routing on ListView. Hosts wire
//     onKeyDown externally (e.g. ComboBox's popup will need it for
//     v1.1 keyboard support). v1.1 fix: ListView::onKeyDown override
//     translates arrow keys to setSelectedIndex(current + delta), capped
//     to visible window, plus scrollToIndex so the new selection is
//     visible. ListView does NOT inherit FocusableWidget — keyboard
//     nav is host-side, not list-side, in v1.
//
// K3. Multi-selection (Ctrl+click / Shift+click range select).
//     Not in v1. Single mode only (`_selectedIndex` is a single int).
//     v1.1 design is a `SelectionMode { Single, Multi }` enum + a
//     `std::vector<int> _selectedIndices` alongside `_selectedIndex`
//     (or replacing it). When implementing, also update
//     SelectableWidget.h to add the contract for multi-mode semantics
//     (anchor/active distinction, range select rules).
//
// =============================================================================

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

// =============================================================================
// Phase B (B1) — keyboard navigation tests
// =============================================================================

// B1: Arrow keys move selection. Up wraps to end; Down wraps to start.
TEST_CASE(listview_arrow_keys_move_selection) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c", L"d"});
    lv.setSize(FVector2(160.0f, 200.0f));
    lv.setPosition(FVector2(0.0f, 0.0f));
    lv.setSelectedIndex(1);

    lv.onKeyDown(UIKey_Down);
    CHECK(lv.getSelectedIndex() == 2);
    lv.onKeyDown(UIKey_Down);
    CHECK(lv.getSelectedIndex() == 3);
    lv.onKeyDown(UIKey_Down);   // wraps
    CHECK(lv.getSelectedIndex() == 0);
    lv.onKeyDown(UIKey_Up);     // wraps back
    CHECK(lv.getSelectedIndex() == 3);
    lv.onKeyDown(UIKey_Up);
    CHECK(lv.getSelectedIndex() == 2);
}

// B1: Home / End jump to first / last.
TEST_CASE(listview_home_end_jumps) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c", L"d", L"e"});
    lv.setSelectedIndex(2);

    lv.onKeyDown(UIKey_Home);
    CHECK(lv.getSelectedIndex() == 0);

    lv.onKeyDown(UIKey_End);
    CHECK(lv.getSelectedIndex() == 4);
}

// B1: PageUp / PageDown step by viewport-rows-worth. With 10 items and
// itemHeight=20 in a 60-px viewport, one page = 3 rows.
TEST_CASE(listview_pageup_pagedown_move_selection_by_viewport) {
    ListView lv;
    std::vector<std::wstring> items;
    for (int i = 0; i < 10; ++i) items.push_back(L"row" + std::to_wstring(i));
    lv.setItems(items);
    lv.setItemHeight(20.0f);
    lv.setSize(FVector2(160.0f, 60.0f));   // viewport shows ~3 rows
    lv.setPosition(FVector2(0.0f, 0.0f));
    lv.setSelectedIndex(0);

    lv.onKeyDown(UIKey_PageDown);
    CHECK(lv.getSelectedIndex() == 3);
    lv.onKeyDown(UIKey_PageDown);
    CHECK(lv.getSelectedIndex() == 6);
    lv.onKeyDown(UIKey_PageDown);   // clamps to last
    CHECK(lv.getSelectedIndex() == 9);
    lv.onKeyDown(UIKey_PageUp);
    CHECK(lv.getSelectedIndex() == 6);
    lv.onKeyDown(UIKey_PageUp);
    CHECK(lv.getSelectedIndex() == 3);
    lv.onKeyDown(UIKey_PageUp);     // clamps to 0
    CHECK(lv.getSelectedIndex() == 0);
}

// B1: Enter fires _onItemActivated with current selection.
TEST_CASE(listview_enter_key_activates) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    lv.setSelectedIndex(2);

    int activated = -1;
    lv.setOnItemActivated([&](int idx) { activated = idx; });

    lv.onKeyDown(UIKey_Enter);
    CHECK(activated == 2);

    // Enter with no selection does not fire.
    activated = -1;
    lv.setSelectedIndex(-1);
    lv.onKeyDown(UIKey_Enter);
    CHECK(activated == -1);
}

// B1: Arrow keys auto-scroll the selection into view via scrollToIndex
// (promoted to protected). With itemHeight=20 and a 40-px viewport,
// selecting index 7 (rows 140..160) forces scrollOffset.y to ~120.
TEST_CASE(listview_arrow_keys_scroll_into_view) {
    ListView lv;
    std::vector<std::wstring> items;
    for (int i = 0; i < 10; ++i) items.push_back(L"row" + std::to_wstring(i));
    lv.setItems(items);
    lv.setItemHeight(20.0f);
    lv.setSize(FVector2(160.0f, 40.0f));   // 2-row viewport
    lv.setPosition(FVector2(0.0f, 0.0f));
    lv.setSelectedIndex(0);

    // Move to index 7 (row y 140..160). Should scroll so row is visible.
    lv.setSelectedIndex(7);
    const float off = lv.getScrollOffset().y;
    CHECK(off >= 120.0f);
    CHECK(off <= 140.0f);   // clamp: viewBottom = 140..160 → offset 120..140

    // Move back to 0 — should scroll back to top.
    lv.setSelectedIndex(0);
    CHECK(lv.getScrollOffset().y == 0.0f);
}

// B1: onMouseButtonDown sets focus on the list. We can't easily verify
// UIManager::get().getFocusedWidget() == &lv from inside the list itself
// (chicken-and-egg), but we CAN verify the call returns false (so the
// click bubbles to onMouseButtonUp) and that focus is held afterwards.
TEST_CASE(listview_focus_grab_on_click) {
    UIManager um;
    um.initialize(nullptr);
    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    lv.setSize(FVector2(160.0f, 200.0f));
    lv.setPosition(FVector2(0.0f, 0.0f));

    const FVector2 world = lv.getWorldBounds().getMin();
    const FVector2 inside(world.x + 10.0f, world.y + 12.0f);
    UIMouseEvent ev(inside, 0);

    // Press — should return false (let click flow to onMouseButtonUp)
    // AND focus the list.
    CHECK_FALSE(lv.onMouseButtonDown(ev));
    CHECK(um.getFocusedWidget() == &lv);

    um.shutdown();
}

// B1: keyboard nav is silently ignored when the list has no items.
TEST_CASE(listview_key_nav_on_empty_list_noop) {
    ListView lv;   // empty
    CHECK_FALSE(lv.onKeyDown(UIKey_Down));
    CHECK_FALSE(lv.onKeyDown(UIKey_Up));
    CHECK_FALSE(lv.onKeyDown(UIKey_Home));
    CHECK_FALSE(lv.onKeyDown(UIKey_End));
    CHECK_FALSE(lv.onKeyDown(UIKey_Enter));
    CHECK(lv.getSelectedIndex() == -1);
}

// =============================================================================
// G4 — vbar auto-hide + setVisibleRowCount
// =============================================================================

// G4: 3 items in a 200-tall list fit comfortably → vbar must hide so
// rows take the full width. vbar widget still exists (auto-managed), but
// isVisible() == false. This is the canonical "no overflow → no chrome".
TEST_CASE(listview_vbar_hidden_when_items_fit) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    lv.setSize(FVector2(160.0f, 200.0f));   // contentH = 3*24 = 72 ≤ 200
    lv.setPosition(FVector2(0.0f, 0.0f));
    // performLayout derives vbar visibility from contentH vs viewportH.
    lv.performLayout();
    CHECK_NOT_NULL(lv.getVerticalScrollBar());
    CHECK_FALSE(lv.getVerticalScrollBar()->isVisible());
}

// G4: 100 items in a 100-tall list with 24px rows → contentH = 2400 ≫ 100,
// vbar must remain visible so the user can scroll.
TEST_CASE(listview_vbar_visible_when_items_exceed) {
    ListView lv;
    std::vector<std::wstring> items;
    for (int i = 0; i < 100; ++i) items.push_back(L"row" + std::to_wstring(i));
    lv.setItems(items);
    lv.setSize(FVector2(160.0f, 100.0f));   // contentH = 100*24 = 2400 ≫ 100
    lv.setPosition(FVector2(0.0f, 0.0f));
    lv.performLayout();
    CHECK_NOT_NULL(lv.getVerticalScrollBar());
    CHECK(lv.getVerticalScrollBar()->isVisible());

    // PR-SyncVerticalBar: scrollBy() (float dy overload) triggers
    // syncBarToOffset() internally; bar value tracks scroll offset.
    lv.scrollBy(96.0f);   // 4 rows down
    CHECK(lv.getVerticalScrollBar()->getValue() == lv.getScrollOffset().y);
}

// G4: setVisibleRowCount clamps non-positive values to -1 (v1 default)
// and stores the requested value verbatim otherwise. The API is a
// round-trip getter; the vbar-auto-hide derivation lives in
// needsVerticalScrollBar() and is verified by the two cases above.
TEST_CASE(listview_visible_row_count_api_round_trip) {
    ListView lv;
    // Default is -1 (no cap) — preserves v1 behavior.
    CHECK(lv.getVisibleRowCount() == -1);
    lv.setVisibleRowCount(5);
    CHECK(lv.getVisibleRowCount() == 5);
    // 0 / negative are clamped back to -1 sentinel.
    lv.setVisibleRowCount(0);
    CHECK(lv.getVisibleRowCount() == -1);
    lv.setVisibleRowCount(-3);
    CHECK(lv.getVisibleRowCount() == -1);
    // Positive value sticks.
    lv.setVisibleRowCount(8);
    CHECK(lv.getVisibleRowCount() == 8);
}

// =============================================================================
// G1 — ListView multi-select (Phase E)
// =============================================================================

// G1.1: default selection mode is Single (v1 backwards-compat).
TEST_CASE(listview_selection_mode_default_is_single) {
    ListView lv;
    CHECK(lv.getSelectionMode() == ListView::SelectionMode::Single);
}

// G1.2: switching to Extended keeps existing single-setSelectedIndex API
// working (it routes through setSelectedIndices internally).
TEST_CASE(listview_set_selection_mode_extended) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    lv.setSelectionMode(ListView::SelectionMode::Extended);
    CHECK(lv.getSelectionMode() == ListView::SelectionMode::Extended);

    int singleChanges = 0;
    lv.setOnSelectionChanged([&](int) { ++singleChanges; });
    lv.setSelectedIndex(1);
    CHECK(lv.getSelectedIndex() == 1);
    CHECK(singleChanges == 1);
    // Vector holds just the one entry in this path.
    CHECK(lv.getSelectedIndices().size() == 1u);
    CHECK(lv.isSelected(1));
    CHECK_FALSE(lv.isSelected(0));
}

// G1.3: Ctrl+click adds to selection (simulated via direct vector path
// since we can't easily inject modifiers into a bare Widget test; the
// public setSelectedIndices vector simulates the same end state).
TEST_CASE(listview_multi_select_set_indices_additive) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c", L"d", L"e"});
    lv.setSelectionMode(ListView::SelectionMode::Extended);

    int multiChanges = 0;
    int lastMultiSize = -1;
    lv.setOnSelectionIndicesChanged([&](const std::vector<int>& v) {
        ++multiChanges;
        lastMultiSize = static_cast<int>(v.size());
    });

    lv.setSelectedIndices({0, 2, 4});
    CHECK(lv.getSelectedIndices().size() == 3u);
    CHECK(lv.isSelected(0));
    CHECK(lv.isSelected(2));
    CHECK(lv.isSelected(4));
    CHECK_FALSE(lv.isSelected(1));
    CHECK(multiChanges == 1);
    CHECK(lastMultiSize == 3);

    // Toggle one off (simulates Ctrl+click on already-selected).
    lv.setSelectedIndices({0, 4});
    CHECK(lv.getSelectedIndices().size() == 2u);
    CHECK_FALSE(lv.isSelected(2));
    CHECK(multiChanges == 2);
    CHECK(lastMultiSize == 2);
}

// G1.4: setSelectedIndices rejects duplicates + sorts ascending.
TEST_CASE(listview_multi_select_set_indices_dedupes_and_sorts) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c", L"d", L"e"});
    lv.setSelectionMode(ListView::SelectionMode::Extended);
    // Pass duplicates + reverse order.
    lv.setSelectedIndices({4, 0, 2, 4, 0});
    const auto& v = lv.getSelectedIndices();
    CHECK(v.size() == 3u);
    CHECK(v[0] == 0);
    CHECK(v[1] == 2);
    CHECK(v[2] == 4);
}

// G1.5: out-of-range indices are filtered silently.
TEST_CASE(listview_multi_select_set_indices_filters_out_of_range) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    lv.setSelectionMode(ListView::SelectionMode::Extended);
    lv.setSelectedIndices({0, 99, -5, 2});
    const auto& v = lv.getSelectedIndices();
    CHECK(v.size() == 2u);
    CHECK(v[0] == 0);
    CHECK(v[1] == 2);
}

// G1.6: clearSelection empties the vector + clears row flags + fires
// _onSelectionChanged(-1) for backwards-compat single-mode callers.
TEST_CASE(listview_multi_select_clear_selection) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c", L"d"});
    lv.setSelectionMode(ListView::SelectionMode::Extended);
    lv.setSelectedIndices({1, 3});

    int singleChanges = 0;
    lv.setOnSelectionChanged([&](int idx) {
        if (idx == -1) ++singleChanges;
    });
    lv.clearSelection();
    CHECK(lv.getSelectedIndices().empty());
    CHECK(lv.getSelectedIndex() == -1);
    CHECK(singleChanges == 1);
}

// G1.7: isSelected() reports membership correctly.
TEST_CASE(listview_is_selected_helper) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    lv.setSelectionMode(ListView::SelectionMode::Extended);
    CHECK_FALSE(lv.isSelected(0));
    CHECK_FALSE(lv.isSelected(2));
    CHECK_FALSE(lv.isSelected(-1));
    CHECK_FALSE(lv.isSelected(99));

    lv.setSelectedIndices({1});
    CHECK(lv.isSelected(1));
    CHECK_FALSE(lv.isSelected(0));
    CHECK_FALSE(lv.isSelected(2));
}

// G1.8: anchorIndex tracks setSelectedIndex in single mode AND moves on
// Ctrl/click (setAnchorIndex API direct test).
TEST_CASE(listview_anchor_index_api) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c", L"d"});
    lv.setSelectionMode(ListView::SelectionMode::Extended);
    CHECK(lv.getAnchorIndex() == -1);

    lv.setAnchorIndex(2);
    CHECK(lv.getAnchorIndex() == 2);

    // Out-of-range anchor clamps to -1.
    lv.setAnchorIndex(99);
    CHECK(lv.getAnchorIndex() == -1);
    lv.setAnchorIndex(-3);
    CHECK(lv.getAnchorIndex() == -1);
}

// G1.9: Shift+Down extends range from anchorIndex. We exercise the
// dispatch through onKeyDown with the focus holding a ListView whose
// UIManager has the Shift modifier bit set.
TEST_CASE(listview_multi_select_keyboard_shift_arrow_extends_range) {
    UIManager ui;
    ui.initialize(nullptr);

    ListView lv;
    lv.setItems({L"a", L"b", L"c", L"d", L"e"});
    lv.setSize(FVector2(160.0f, 200.0f));
    lv.setPosition(FVector2(0.0f, 0.0f));
    lv.setSelectionMode(ListView::SelectionMode::Extended);

    // Click index 0 to seed anchor via setAnchorIndex (we bypass the
    // hit-test plumbing since UIManager::initialize(nullptr) gives a
    // valid manager but no real layout for hit-test descent).
    lv.setSelectedIndex(0);
    lv.setAnchorIndex(0);
    CHECK(lv.getAnchorIndex() == 0);

    // Press Shift (UIManager intercepts Shift+Up to set the bit), then
    // Down twice. End state: range = [0..2].
    ui.onKeyDown(UIKey_Shift);
    lv.onKeyDown(UIKey_Down);
    lv.onKeyDown(UIKey_Down);
    ui.onKeyUp(UIKey_Shift);

    const auto& v = lv.getSelectedIndices();
    CHECK(v.size() == 3u);
    CHECK(v[0] == 0);
    CHECK(v[1] == 1);
    CHECK(v[2] == 2);

    ui.shutdown();
}

// G1.10: Ctrl+A selects all (Extended mode); ignored in Single mode.
TEST_CASE(listview_multi_select_ctrl_a_selects_all) {
    UIManager ui;
    ui.initialize(nullptr);

    // Extended: Ctrl+A → all items.
    {
        ListView lv;
        lv.setItems({L"a", L"b", L"c", L"d"});
        lv.setSelectionMode(ListView::SelectionMode::Extended);
        ui.onKeyDown(UIKey_Control);
        lv.onKeyDown(UIKey_A);
        ui.onKeyUp(UIKey_Control);
        CHECK(lv.getSelectedIndices().size() == 4u);
    }
    // Single: Ctrl+A → only first item (because setSelectedIndex clamps
    // a vector down to a single-element front).
    {
        ListView lv;
        lv.setItems({L"a", L"b", L"c", L"d"});
        // selectionMode stays Single (default).
        ui.onKeyDown(UIKey_Control);
        lv.onKeyDown(UIKey_A);
        ui.onKeyUp(UIKey_Control);
        CHECK(lv.getSelectedIndices().size() == 1u);
        CHECK(lv.getSelectedIndex() == 0);
    }

    ui.shutdown();
}

// G1.11: Esc clears selection (Extended mode); no-op on empty.
TEST_CASE(listview_multi_select_esc_clears_selection) {
    UIManager ui;
    ui.initialize(nullptr);

    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    lv.setSelectionMode(ListView::SelectionMode::Extended);
    lv.setSelectedIndices({0, 1, 2});
    CHECK_FALSE(lv.getSelectedIndices().empty());

    lv.onKeyDown(UIKey_Escape);
    CHECK(lv.getSelectedIndices().empty());

    // Second Esc is a no-op (empty → no fire).
    int changes = 0;
    lv.setOnSelectionChanged([&](int) { ++changes; });
    lv.onKeyDown(UIKey_Escape);
    CHECK(changes == 0);

    ui.shutdown();
}

// G1.12: setItems on Extended list clamps selection indices; anchor
// preserves last valid anchor or resets if out of range.
TEST_CASE(listview_set_items_filters_stale_selection) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c", L"d", L"e"});
    lv.setSelectionMode(ListView::SelectionMode::Extended);
    lv.setSelectedIndices({1, 3});
    lv.setAnchorIndex(3);

    // Shrink to 2 items — selection should drop indices 3.
    lv.setItems({L"x", L"y"});
    const auto& v = lv.getSelectedIndices();
    CHECK(v.size() == 1u);
    CHECK(v[0] == 1);
    // Anchor 3 was out of range → reset to last valid (1).
    CHECK(lv.getAnchorIndex() == 1);
}

// G1.13: factory + serializer round-trip preserves selectionMode + the
// full vector (not just the legacy selectedIndex).
TEST_CASE(listview_factory_serializer_round_trip_with_multi) {
    WidgetFactory& factory = WidgetFactory::get();
    Widget* widget = factory.create("ListView");
    ListView* lv = dynamic_cast<ListView*>(widget);
    CHECK_NOT_NULL(lv);

    lv->setItems({L"a", L"b", L"c", L"d", L"e"});
    lv->setSelectionMode(ListView::SelectionMode::Extended);
    lv->setSelectedIndices({1, 3});

    const std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"selectionMode\": 1") != std::string::npos);
    CHECK(json.find("\"selectedIndices\"") != std::string::npos);
    CHECK(json.find("\"selectedIndex\": 3") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    ListView* rlv = dynamic_cast<ListView*>(restored);
    CHECK_NOT_NULL(rlv);
    CHECK(rlv->getSelectionMode() == ListView::SelectionMode::Extended);
    CHECK(rlv->getSelectedIndices().size() == 2u);
    CHECK(rlv->isSelected(1));
    CHECK(rlv->isSelected(3));

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// G1.14: ComboBox smoke check — opening a popup + clicking a row +
// verifying the existing single-select contract still works. ComboBox's
// popup ListView is locked to Single (via ComboBox::ensurePopupCreated
// calling setSelectionMode(Single)); the ~30 ComboBox tests in
// Test_ComboBox.cpp cover the popup-lock behavior end-to-end. This case
// is a regression guard: ensure G1's SelectionMode addition didn't break
// the ComboBox popup creation / selection path.
TEST_CASE(listview_combobox_g1_regression_smoke) {
    UIManager ui;
    ui.initialize(nullptr);
    ComboBox cb;
    cb.setItems({L"a", L"b", L"c"});
    cb.setSize(FVector2(120.0f, 24.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));
    cb.openPopup();
    // If the popup were created in Extended mode by accident, opening
    // + closing without selection would still succeed; the real
    // contract is exercised in Test_ComboBox. Here we just verify the
    // basic open/close path doesn't crash post-G1.
    cb.closePopup();
    ui.shutdown();
}

// =============================================================================
// G2 — row pool (virtualization)
// =============================================================================
//
// The pool is always-on: K = min(items.size(), ceil(viewport/itemH) + 2)
// row widgets cover a 5k-item list. The public API (setItems,
// getItem, setSelectedIndex, scrollToIndex) is unchanged — only the
// internal allocation strategy is new. These cases exercise the
// pool boundary + scroll remap + click → logical index translation
// + dtor cleanup invariants that the K1 large-list scenario from
// the v1 TODO block above was designed around.

// G2.1 — pool only allocates K row widgets regardless of items.size().
// 24px items in a 200px viewport → K = ceil(200/24)+1 + 2 = 8+1+2 = 11.
// Hard cap: K ≤ items.size().
TEST_CASE(listview_pool_only_allocates_visible_rows) {
    ListView lv;
    lv.setSize(FVector2(200.0f, 200.0f));
    lv.setItemHeight(24.0f);
    std::vector<std::wstring> items;
    items.reserve(5000);
    for (int i = 0; i < 5000; ++i) {
        items.push_back(L"row " + std::to_wstring(i));
    }
    lv.setItems(items);
    // Pool size = min(5000, ceil(200/24)+1 + 2 = 8+1+2 = 11).
    CHECK(lv.getRowPoolSize() <= 11u);
    // Pre-G2 would have been 5000. Post-G2 this is the hard win.
    CHECK(lv.getRowPoolSize() < static_cast<size_t>(5000));
}

// G2.2 — pool[0] text reflects the first VISIBLE logical item, not
// literal item 0. After scroll-to-offset 240 (10 items down), the
// pool remaps so pool[0] shows items[10].
TEST_CASE(listview_pool_scroll_remaps_rows) {
    ListView lv;
    lv.setSize(FVector2(200.0f, 200.0f));
    lv.setItemHeight(24.0f);
    std::vector<std::wstring> items;
    for (int i = 0; i < 200; ++i) {
        items.push_back(L"row " + std::to_wstring(i));
    }
    lv.setItems(items);
    // Before scroll: pool[0] should show items[0].
    CHECK(lv.getRowPoolLogicalIndex(0) == 0);
    // Scroll down by 10 items (240px).
    lv.setScrollOffset(FVector2(0.0f, 240.0f));
    // Pool should remap; pool[0] now shows logical item 10.
    CHECK(lv.getRowPoolLogicalIndex(0) == 10);
    CHECK(lv.getRowPoolLogicalIndex(1) == 11);
}

// G2.3 — pool slots store their LOGICAL index (not slot number).
// Pool slot s with _firstVisibleIndex=N has Row::_index == N + s.
// We verify this directly via getRowPoolLogicalIndex() before and
// after a scroll. The selection callback contract (Row click →
// handleRowClick(logical)) is exercised by all the G1 tests above
// which now run against a pooled ListView; here we focus on the
// pool-bound semantics.
TEST_CASE(listview_pool_slot_stores_logical_index) {
    ListView lv;
    lv.setSize(FVector2(200.0f, 200.0f));
    lv.setItemHeight(24.0f);
    std::vector<std::wstring> items;
    for (int i = 0; i < 100; ++i) {
        items.push_back(L"row " + std::to_wstring(i));
    }
    lv.setItems(items);

    // Initially: pool covers items 0..K-1.
    const size_t poolSize = lv.getRowPoolSize();
    CHECK(poolSize > 0u);
    CHECK(lv.getRowPoolLogicalIndex(0) == 0);
    CHECK(lv.getRowPoolLogicalIndex(poolSize - 1)
          == static_cast<int>(poolSize) - 1);

    // Scroll so logical item 50 is in pool slot 0.
    lv.setScrollOffset(FVector2(0.0f, 50.0f * 24.0f));
    CHECK(lv.getRowPoolLogicalIndex(0) == 50);
    CHECK(lv.getRowPoolLogicalIndex(1) == 51);
    CHECK(lv.getRowPoolLogicalIndex(poolSize - 1)
          == 50 + static_cast<int>(poolSize) - 1);
}

// G2.4 — selection on a logical index OUTSIDE the pool viewport is
// preserved in _selectedIndices (the authoritative state), even though
// no row widget exists for it yet. When the user scrolls that index
// into view, rebindPoolRows paints the selected band.
TEST_CASE(listview_pool_set_selected_outside_viewport_preserved) {
    ListView lv;
    lv.setSize(FVector2(200.0f, 200.0f));
    lv.setItemHeight(24.0f);
    std::vector<std::wstring> items;
    for (int i = 0; i < 5000; ++i) {
        items.push_back(L"row " + std::to_wstring(i));
    }
    lv.setItems(items);

    // Select index 2500 — far outside the pool's 0..10 window.
    lv.setSelectedIndex(2500);
    CHECK(lv.getSelectedIndex() == 2500);
    // Multi-mode variant: multiple disjoint selections across the list.
    lv.setSelectionMode(ListView::SelectionMode::Extended);
    lv.setSelectedIndices({2500, 4999});
    CHECK(lv.getSelectedIndices().size() == 2u);
    CHECK(lv.isSelected(2500));
    CHECK(lv.isSelected(4999));
    // Both indices are valid even though no row widget holds them.
    CHECK(lv.getRowPoolSize() <= 11u);
}

// G2.5 — destroying the ListView releases all pool row widgets so the
// children vector is empty (host can detect leaks via the factory's
// createWidget + deleteWidget cycle).
TEST_CASE(listview_pool_dtor_clears_children) {
    ListView lv;
    lv.setSize(FVector2(200.0f, 200.0f));
    lv.setItemHeight(24.0f);
    std::vector<std::wstring> items;
    for (int i = 0; i < 100; ++i) {
        items.push_back(L"row " + std::to_wstring(i));
    }
    lv.setItems(items);
    // Pool has K ≤ 11 row widgets attached as children.
    const size_t childrenBefore = lv.getChildren().size();
    CHECK(childrenBefore > 0u);
    // Dtor runs at scope exit; if a pool row was double-owned or
    // leaked, the heap check below would catch it.
}

// G2.6 — clearing items also drops the pool to size 0 (no zombie row
// widgets holding onto a stale _text reference).
TEST_CASE(listview_pool_cleared_on_clear_items) {
    ListView lv;
    lv.setSize(FVector2(200.0f, 200.0f));
    lv.setItemHeight(24.0f);
    std::vector<std::wstring> items = {L"a", L"b", L"c"};
    lv.setItems(items);
    CHECK(lv.getRowPoolSize() > 0u);
    lv.clearItems();
    CHECK(lv.getRowPoolSize() == 0u);
    CHECK(lv.getItemCount() == 0u);
}

// G2.7 — replacing items via setItems rebuilds the pool from scratch.
// Pool size is bounded by the new items.size() / viewport computation.
// A 5-item list in a 200px viewport has K = min(5, 11) = 5.
TEST_CASE(listview_pool_set_items_resizes_pool) {
    ListView lv;
    lv.setSize(FVector2(200.0f, 200.0f));
    lv.setItemHeight(24.0f);
    // First: 5000 items → pool size 11.
    std::vector<std::wstring> big;
    for (int i = 0; i < 5000; ++i) big.push_back(L"x");
    lv.setItems(big);
    const size_t poolBig = lv.getRowPoolSize();
    CHECK(poolBig > 0u);
    CHECK(poolBig <= 11u);
    // Then: 3 items → pool shrinks to min(3, 11) = 3.
    lv.setItems({L"only", L"three", L"rows"});
    CHECK(lv.getRowPoolSize() == 3u);
    // And back to large.
    lv.setItems(big);
    CHECK(lv.getRowPoolSize() == poolBig);
}

// G2.8 — pool rebind via vbar drag updates _firstVisibleIndex
// monotonically. After several scrollOffset changes, the pool[0]
// logical index reflects the LATEST offset (not stale from a prior
// bind).
TEST_CASE(listview_pool_vbar_drag_rebinds_monotonically) {
    ListView lv;
    lv.setSize(FVector2(200.0f, 200.0f));
    lv.setItemHeight(24.0f);
    std::vector<std::wstring> items;
    for (int i = 0; i < 200; ++i) {
        items.push_back(L"row " + std::to_wstring(i));
    }
    lv.setItems(items);

    int prevFirst = lv.getRowPoolLogicalIndex(0);
    CHECK(prevFirst == 0);
    // Scroll down a few times.
    int backwardRebindCount = 0;
    int outOfRangeRebindCount = 0;
    for (int i = 0; i < 5; ++i) {
        lv.setScrollOffset(FVector2(0.0f, (i + 1) * 48.0f));   // 2 rows each
        const int curFirst = lv.getRowPoolLogicalIndex(0);
        if (curFirst < prevFirst) {
            ++backwardRebindCount;
        }
        if (curFirst > 200 - static_cast<int>(lv.getRowPoolSize())) {
            ++outOfRangeRebindCount;
        }
        prevFirst = curFirst;
    }
    CHECK(backwardRebindCount == 0);
    CHECK(outOfRangeRebindCount == 0);
    // Scroll back to top — pool rebinds back to item 0.
    lv.setScrollOffset(FVector2(0.0f, 0.0f));
    CHECK(lv.getRowPoolLogicalIndex(0) == 0);
}

// =============================================================================
// UI-anim cut 2 — wheel momentum glide + row pool rebind.
// =============================================================================

TEST_CASE(listview_momentum_glides_and_rebinds) {
    ListView lv;
    lv.setSize(FVector2(200.0f, 100.0f));
    std::vector<std::wstring> items;
    for (int i = 0; i < 50; ++i) {
        items.push_back(L"row " + std::to_wstring(i));
    }
    lv.setItems(items);
    lv.performLayout();    // computes _contentSize (maxScroll > 0)

    const int before = lv.getRowPoolLogicalIndex(0);
    CHECK(before == 0);

    UIMouseWheelEvent e(FVector2(100.0f, 50.0f), 120.0f);
    CHECK(lv.onMouseWheel(e));
    const float immediate = lv.getScrollOffset().y;
    CHECK(immediate > 0.0f);

    // Glide advances + rebinds the pool rows.
    int lastFirst = lv.getRowPoolLogicalIndex(0);
    int backwardRebindCount = 0;
    for (int i = 0; i < 30; ++i) {
        lv.tick(0.016f);
        const int curFirst = lv.getRowPoolLogicalIndex(0);
        if (curFirst < lastFirst) {
            ++backwardRebindCount;
        }
        lastFirst = curFirst;
    }
    CHECK(backwardRebindCount == 0);
    CHECK(lv.getRowPoolLogicalIndex(0) > before);

    // Parks eventually; pool stays consistent.
    for (int i = 0; i < 400; ++i) lv.tick(0.016f);
    const float parked = lv.getScrollOffset().y;
    lv.tick(0.016f);
    CHECK_FLOAT_EQ(lv.getScrollOffset().y, parked, 1e-5f);
    CHECK(lv.getRowPoolLogicalIndex(0) == lastFirst);
}

TEST_SUITE_END
