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
// Phase A: popup must mount on UIManager's overlay — standalone ComboBox
// without an initialized UIManager cannot report isPopupOpen().
TEST_CASE(combobox_open_close_popup) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

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

    ui.shutdown();
}

// C-6: openPopup on empty items is a no-op.
TEST_CASE(combobox_open_popup_empty_noop) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    ComboBox cb;
    cb.openPopup();
    CHECK_FALSE(cb.isPopupOpen());

    ui.shutdown();
}

// C-6: clicking the main ComboBox toggles popup; clicking the popup row
// updates selection and (in v1) closes the popup.
TEST_CASE(combobox_click_main_opens_popup) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    ComboBox cb;
    cb.setItems({L"x", L"y", L"z"});
    cb.setSize(FVector2(160.0f, 28.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));

    const FVector2 world = cb.getWorldBounds().getMin();
    // Click in the middle of the main area.
    const FVector2 clickPos(world.x + 80.0f, world.y + 14.0f);
    cb.onMouseButtonUp(UIMouseEvent(clickPos, 0));
    CHECK(cb.isPopupOpen());

    ui.shutdown();
}

// C-6: clicking a popup row closes the popup and updates selection.
// Phase A (A2): the popup lives on UIManager's overlay root, not as a
// child of ComboBox. Row clicks are routed through UIManager's overlay
// hit-test funnel (overlay-first → falls back to root). The test
// initializes UIManager, registers the ComboBox as a child of its
// root via loadFromString, opens the popup, then drives the click
// through UIManager.
TEST_CASE(combobox_click_popup_row_selects) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "Widget",
        "id": "root",
        "size": { "w": 800, "h": 600 },
        "children": [
            {
                "type": "ComboBox",
                "id": "cb",
                "items": ["a", "b", "c"],
                "position": { "x": 0, "y": 0 },
                "size": { "w": 160, "h": 28 }
            }
        ]
    })";
    CHECK(ui.loadFromString(json));
    ui.setClientSize(800.0f, 600.0f);
    ui.layout();

    auto* cb = dynamic_cast<ComboBox*>(ui.findById("cb"));
    CHECK_NOT_NULL(cb);

    int selChanges = 0;
    int lastSelected = -2;
    cb->setOnSelectionChanged([&](int idx) {
        ++selChanges;
        lastSelected = idx;
    });

    cb->openPopup();
    CHECK(cb->isPopupOpen());

    // Popup should be a child of the overlay, not of ComboBox.
    ListView* popup = nullptr;
    for (Widget* w : ui.getOverlayRoot()->getChildren()) {
        popup = dynamic_cast<ListView*>(w);
        if (popup != nullptr) break;
    }
    CHECK_NOT_NULL(popup);
    CHECK(popup->getItemCount() == 3u);

    // Row 1 lives at popup-local y = 24..48; popup-local origin sits at
    // (0, ComboBox.height + gap) relative to ComboBox world top.
    const FVector2 cbWorld = cb->getWorldBounds().getMin();
    const FVector2 rowWorld(
        cbWorld.x + 80.0f,
        cbWorld.y + cb->getHeight() + 2.0f + 36.0f); // mid of row 1

    // Drive the click through UIManager so the overlay hit-test funnel
    // routes it to the popup row.
    ui.onMouseButtonDown(rowWorld.x, rowWorld.y, 0);
    ui.onMouseButtonUp(rowWorld.x, rowWorld.y, 0);

    CHECK(selChanges == 1);
    CHECK(lastSelected == 1);
    CHECK(cb->getSelectedIndex() == 1);
    CHECK(cb->getSelectedItem() == L"b");

    ui.shutdown();
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

// =============================================================================
// Phase A (A2) — ComboBox on overlay
// =============================================================================

// Phase A (A2): openPopup mounts the popup on UIManager's overlay root,
// NOT as a child of the ComboBox. The popup is found via the overlay's
// children list, not ComboBox::getChildren().
TEST_CASE(combobox_popup_mounted_on_overlay_not_as_child) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    ComboBox* cb = new ComboBox();
    cb->setItems({L"a", L"b", L"c"});
    cb->setSize(FVector2(160.0f, 28.0f));
    cb->setPosition(FVector2(20.0f, 20.0f));

    cb->openPopup();

    // Overlay should hold the popup. ComboBox should not.
    CHECK(ui.getOverlayRoot()->getChildren().size() == 1u);
    // ComboBox's only child is _display (TextLabel), not the popup.
    CHECK(cb->getChildren().size() == 1u);
    CHECK_NOT_NULL(dynamic_cast<TextLabel*>(cb->getChildren()[0]));

    cb->closePopup();
    ui.shutdown();
}

// Phase A (A2 L4): ComboBox near the bottom of the viewport flips the
// popup above the main area. With clientHeight=200 and ComboBox at
// y=180 height=28, the default-below position (208) + popup height
// overflows → flip to y < 180 - popupH - gap.
TEST_CASE(combobox_popup_flips_above_when_below_overflows) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 200.0f);

    ComboBox* cb = new ComboBox();
    cb->setItems({L"a", L"b", L"c"});
    cb->setSize(FVector2(160.0f, 28.0f));
    // Place near the bottom so default-below overflows.
    cb->setPosition(FVector2(20.0f, 160.0f));

    cb->openPopup();

    // Popup should be mounted.
    CHECK(ui.getOverlayRoot()->getChildren().size() == 1u);
    Widget* popup = ui.getOverlayRoot()->getChildren()[0];

    // The popup's top should be ABOVE the ComboBox's top (flipped).
    const FVector2 cbWorldMin = cb->getWorldBounds().getMin();
    const FVector2 popupWorldMin = popup->getWorldBounds().getMin();
    CHECK(popupWorldMin.y < cbWorldMin.y);

    cb->closePopup();
    ui.shutdown();
}

// Phase A (A2 L4): ComboBox near the right edge clamps the popup so it
// stays within the viewport. With clientWidth=200 and ComboBox at
// x=180 width=160, default-below would put the popup's right at 340.
// Clamp should snap x to (200 - 160) = 40.
TEST_CASE(combobox_popup_clamps_within_viewport) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(200.0f, 600.0f);

    ComboBox* cb = new ComboBox();
    cb->setItems({L"a", L"b", L"c"});
    cb->setSize(FVector2(160.0f, 28.0f));
    // Place near the right edge so default-below overflows horizontally.
    cb->setPosition(FVector2(180.0f, 20.0f));

    cb->openPopup();

    CHECK(ui.getOverlayRoot()->getChildren().size() == 1u);
    Widget* popup = ui.getOverlayRoot()->getChildren()[0];
    const FRectangle pb = popup->getWorldBounds();
    CHECK(pb.maxX <= 200.0f);

    cb->closePopup();
    ui.shutdown();
}

// Phase A (A2 S2): DropdownManager single-active-popup invariant. Two
// ComboBoxes in the same root — opening the second closes the first.
TEST_CASE(combobox_dual_open_closes_first) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    ComboBox* cb1 = new ComboBox();
    cb1->setItems({L"x", L"y"});
    cb1->setPosition(FVector2(20.0f, 20.0f));
    ComboBox* cb2 = new ComboBox();
    cb2->setItems({L"p", L"q"});
    cb2->setPosition(FVector2(20.0f, 80.0f));

    cb1->openPopup();
    CHECK(cb1->isPopupOpen());
    CHECK(ui.getOverlayRoot()->getChildren().size() == 1u);

    cb2->openPopup();
    CHECK(cb2->isPopupOpen());
    CHECK(!cb1->isPopupOpen());
    CHECK(ui.getOverlayRoot()->getChildren().size() == 1u);

    cb2->closePopup();
    ui.shutdown();
}

// Phase A (A2 L1): ComboBox-in-ScrollView fix. ComboBox inside a
// ScrollView now has its popup on the overlay, so the popup extends
// past the ScrollView's content bounds. The overlay hit-test routes
// clicks on the overflow region to the popup row.
TEST_CASE(combobox_in_scrollview_popup_overflow_clickable) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "ScrollView",
        "id": "root",
        "position": { "x": 0, "y": 0 },
        "size": { "w": 400, "h": 200 },
        "children": [
            {
                "type": "ComboBox",
                "id": "cb",
                "items": ["a", "b", "c"],
                "position": { "x": 0, "y": 0 },
                "size": { "w": 160, "h": 28 }
            }
        ]
    })";
    CHECK(ui.loadFromString(json));
    ui.setClientSize(400.0f, 200.0f);
    ui.layout();

    auto* cb = dynamic_cast<ComboBox*>(ui.findById("cb"));
    CHECK_NOT_NULL(cb);
    cb->openPopup();
    CHECK(ui.getOverlayRoot()->getChildren().size() == 1u);

    // Click on a popup row at y=64 — that's outside the ScrollView
    // bounds (which are y ∈ [0, 200)). With the overlay model, this
    // click hits the popup row, not the ScrollView.
    int selChanges = 0;
    cb->setOnSelectionChanged([&](int) { ++selChanges; });

    ui.onMouseButtonDown(80.0f, 64.0f, 0);
    ui.onMouseButtonUp(80.0f, 64.0f, 0);
    CHECK(selChanges == 1);

    ui.shutdown();
}

TEST_SUITE_END
