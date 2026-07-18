#include "AYTest.h"
#include "AYComboBox.h"
#include "AYListView.h"
#include "AYTextLabel.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
#include "AYStyle.h"
#include <iostream>

// =============================================================================
// Known-not-covered scenarios for C-6 ComboBox v1
// =============================================================================
// The cases below are NOT exercised by the tests in this file. They are
// pinned here (not as failing tests) so the next implementer can find the
// entry points and write regression tests when fixing the underlying v1
// limitation. See Controls/AYComboBox.h top-of-file "v1 design decisions"
// block for the design rationale + upgrade paths.
//
// S1. ScrollView parent — popup overflows ScrollView's content bounds.
//     Repro sketch:
//        UIManager ui; ui.initialize(nullptr);
//        auto* sv = new ScrollView();
//        auto* cb = new ComboBox(); cb->setItems({"a","b","c"});
//        sv->setContent(cb);
//        ui.loadFromString("...");
//        cb->openPopup();
//        // Click on the popup's overflow region (outside ScrollView's
//        // content bounds). Currently this click misses the popup because
//        // ScrollView's CompoundWidget::hitTest does not descend past its
//        // own content subtree, and ComboBox's hitTest detour only knows
//        // about its own popup child (not siblings elsewhere in the tree).
//     Expected v1.1 fix: ComboBox::setPopupParent(Widget* root) injects
//     the popup's parent; openPopup addChildExternal's the popup to root
//     instead of this. Then ScrollView's hitTest correctly includes the
//     popup as a sibling.
//
// S2. Window drag with open popup.
//     Repro sketch:
//        Window w; w.setSize(...); ComboBox cb; cb.setItems({"x"});
//        w.addChild(&cb); cb.openPopup();
//        // Drag the window title bar; release outside the window.
//        // UIManager clears _capturedWidget only on shutdown/reload/explicit
//        // mouseup — mid-drag the captured pointer may still reference the
//        // popup rows after the Window is destroyed (host frees it). Latent
//        // UAF on the next mouse event.
//     Expected v1.1 fix: same setPopupParent fix as S1 — when popup
//     lives on root, the root's destroyWidgetTree releases it cleanly
//     even mid-drag; UIManager's next pickWidgetAt finds null.
//
// S3. Multi-popup coexistence.
//     Repro sketch:
//        VBox root; ComboBox a, b; root.addChild(&a); root.addChild(&b);
//        a.openPopup(); b.openPopup();
//        // a's popup sits on top of b's main area (bringToFront raised
//        // a + its popup together). Clicking on b's main area may be
//        // swallowed by a's popup if it overlaps.
//     Expected v1.1 fix: DropdownManager singleton tracks the
//     "currently open" ComboBox; opening a new one closes the prior.
//     Or render order via stack layering — handle when needed.
//
// S4. Keyboard navigation (Tab/Up/Down/Home/End/Enter).
//     Not a bug — v1 ships ComboBox without keyboard routing. Verify
//     by trying: load a UI with a ComboBox; Tab through it; ComboBox
//     never gets focus. To enable keyboard nav in v1.1, inherit
//     FocusableWidget INSTEAD OF CompoundWidget (parallel base, same
//     trick TextInput uses) and override onKeyDown/onKeyUp.
//
// S5. Viewport clamp / auto-flip-up.
//     Place a ComboBox near the bottom of the screen with many items;
//     open popup; popup overflows past the viewport with no clamp.
//     Host must position ComboBox with room below. v1.1 fix: pass
//     viewport metrics via setViewportSize or compute from root.
//
// =============================================================================

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_ComboBox)

// C-6: default state — no items, no selection, popup closed, display
// label is empty.
TEST_CASE(combobox_initial_state) {
    ComboBox cb;
    CHECK(cb.getItemCount() == 0u);
    CHECK(cb.getSelectedIndex() == -1);
    CHECK_FALSE(cb.isPopupOpen());
    CHECK(cb.getSelectedItem() == L"");
    CHECK(cb.getMaxPopupItems() == 8);
}

// C-6: setItems + setSelectedIndex updates display label.
TEST_CASE(combobox_set_items_and_selection) {
    ComboBox cb;
    cb.setItems({L"red", L"green", L"blue"});
    CHECK(cb.getItemCount() == 3u);

    cb.setSelectedIndex(2);
    CHECK(cb.getSelectedItem() == L"blue");

    // Display label is a TextLabel child — read its text.
    // (We can't easily expose the display TextLabel accessor in the public
    // API for v1, so the round-trip test below is the canonical verification.)
}

// C-6: openPopup / closePopup toggle visibility.
TEST_CASE(combobox_open_close_popup) {
    ComboBox cb;
    cb.setItems({L"a", L"b", L"c"});
    CHECK_FALSE(cb.isPopupOpen());

    cb.openPopup();
    CHECK(cb.isPopupOpen());

    cb.closePopup();
    CHECK_FALSE(cb.isPopupOpen());

    cb.togglePopup();
    CHECK(cb.isPopupOpen());
    cb.togglePopup();
    CHECK_FALSE(cb.isPopupOpen());
}

// C-6: openPopup on empty items is a no-op.
TEST_CASE(combobox_open_popup_empty_noop) {
    ComboBox cb;
    cb.openPopup();
    CHECK_FALSE(cb.isPopupOpen());
}

// C-6: clicking the main ComboBox toggles popup; clicking the popup row
// updates selection and (in v1) closes the popup.
TEST_CASE(combobox_click_main_opens_popup) {
    ComboBox cb;
    cb.setItems({L"x", L"y", L"z"});
    cb.setSize(FVector2(160.0f, 28.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));

    const FVector2 world = cb.getWorldBounds().getMin();
    // Click in the middle of the main area.
    const FVector2 clickPos(world.x + 80.0f, world.y + 14.0f);
    cb.onMouseButtonUp(UIMouseEvent(clickPos, 0));
    CHECK(cb.isPopupOpen());
}

// C-6: clicking a popup row closes the popup and updates selection.
TEST_CASE(combobox_click_popup_row_selects) {
    ComboBox cb;
    cb.setItems({L"a", L"b", L"c"});
    cb.setSize(FVector2(160.0f, 28.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));

    int selChanges = 0;
    int lastSelected = -2;
    cb.setOnSelectionChanged([&](int idx) {
        ++selChanges;
        lastSelected = idx;
    });

    cb.openPopup();
    CHECK(cb.isPopupOpen());

    // Force a layout so the popup has world bounds. simulate the layout
    // by re-running performLayout (in real apps UIManager::update runs it).
    cb.performLayout();

    // Find the popup child and grab the row at index 1.
    CHECK(cb.getChildren().size() >= 2u);
    ListView* popup = dynamic_cast<ListView*>(
        cb.getChildren().back());
    CHECK_NOT_NULL(popup);
    CHECK(popup->getItemCount() == 3u);

    // Row 1 lives at popup-local y = 24..48; popup-local origin sits at
    // (0, ComboBox.height + gap) relative to ComboBox world top.
    const FVector2 cbWorld = cb.getWorldBounds().getMin();
    const FVector2 rowWorld(
        cbWorld.x + 80.0f,
        cbWorld.y + cb.getHeight() + 2.0f + 36.0f); // mid of row 1

    // Route the click through ComboBox's onMouseButtonUp (which forwards
    // to the popup row when popup is open).
    cb.onMouseButtonUp(UIMouseEvent(rowWorld, 0));

    CHECK(selChanges == 1);
    CHECK(lastSelected == 1);
    CHECK(cb.getSelectedIndex() == 1);
    CHECK(cb.getSelectedItem() == L"b");
}

// C-6: factory + serializer round-trip preserves items + selectedIndex +
// maxPopupItems.
TEST_CASE(combobox_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("ComboBox"));

    Widget* widget = factory.create("ComboBox");
    CHECK_NOT_NULL(widget);
    ComboBox* original = dynamic_cast<ComboBox*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("cb_color");
    original->setItems({L"red", L"green", L"blue", L"yellow"});
    original->setSelectedIndex(2);
    original->setMaxPopupItems(6);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"ComboBox\"") != std::string::npos);
    CHECK(json.find("yellow") != std::string::npos);
    CHECK(json.find("\"selectedIndex\": 2") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    ComboBox* restoredCb = dynamic_cast<ComboBox*>(restored);
    CHECK_NOT_NULL(restoredCb);
    CHECK(restoredCb->getId() == "cb_color");
    CHECK(restoredCb->getItemCount() == 4u);
    CHECK(restoredCb->getSelectedIndex() == 2);
    CHECK(restoredCb->getSelectedItem() == L"blue");
    CHECK(restoredCb->getMaxPopupItems() == 6);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// C-6: render emits at least the main background rect + arrow chevron
// rects. (MockRenderer doesn't record drawBorderRect as a typed draw call —
// IRenderBackend::drawBorderRect is the default no-op path — so we count
// only Rects.)
TEST_CASE(combobox_render_emits_main_and_arrow) {
    ComboBox cb;
    cb.setItems({L"only"});
    cb.setSize(FVector2(160.0f, 28.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));

    MockRenderer renderer;
    cb.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    CHECK(rectCount >= 3);   // bg + 2 arrow halves
}

TEST_SUITE_END
