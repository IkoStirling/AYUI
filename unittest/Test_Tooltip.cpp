#include "AYTest.h"
#include "AYUI/Tooltip.h"
#include "AYUI/Button.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Tooltip)

// Tooltip attached to a target via Tooltip::attachTo; initially hidden.
TEST_CASE(tooltip_initial_state) {
    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    CHECK_NOT_NULL(tip);
    CHECK(tip->getTarget() == &btn);
    CHECK_FALSE(tip->isShowing());
    CHECK_FALSE(tip->isHovered());
    // P3: tip is host-owned (no UIManager → attachTo returned early
    // before registering). Free it directly.
    destroyWidgetTree(tip);
}

// Hovering the target for >= hoverDelay makes the tooltip visible.
TEST_CASE(tooltip_appears_on_hover_delay) {
    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.5f);
    tip->setText(L"Hover text");

    const FVector2 mousePos(60.0f, 60.0f);
    const FVector2 viewport(800.0f, 600.0f);

    // Tick at 0.4s (less than delay) -> still hidden
    tip->tick(0.4f, mousePos, viewport);
    CHECK_FALSE(tip->isShowing());

    // Tick again at 0.4s (cumulative >= 0.5s) -> visible
    tip->tick(0.4f, mousePos, viewport);
    CHECK(tip->isShowing());

    destroyWidgetTree(tip);
}

TEST_CASE(tooltip_cjk_text_uses_full_width_advance) {
    Tooltip latin;
    latin.setText(L"abcde");
    latin.performLayout();

    Tooltip chinese;
    chinese.setText(L"下移当前层");
    chinese.performLayout();

    CHECK(chinese.getSize().x > latin.getSize().x + 10.0f);
}

// Leaving the target hides the tooltip (after one tick).
TEST_CASE(tooltip_hides_on_mouse_leave) {
    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.1f);
    tip->setText(L"Hover text");

    tip->tick(0.2f, FVector2(60.0f, 60.0f), FVector2(800.0f, 600.0f));
    CHECK(tip->isShowing());

    // Move outside.
    tip->tick(0.1f, FVector2(900.0f, 900.0f), FVector2(800.0f, 600.0f));
    CHECK_FALSE(tip->isShowing());

    destroyWidgetTree(tip);
}

// Phase A (A2): the tooltip lives on UIManager's overlay root, NOT as a
// child of the target. P3 update: the tooltip is mounted via
// addChildExternal (NOT addChild) so the overlay does NOT free it on
// shutdown — the caller (the host that called attachTo) owns the
// lifetime. The target can be torn down independently without UAF on
// `_target`, and the host can choose when to destroy the tip (typically
// via `tip->detach(); destroyWidgetTree(tip);` before shutting down UIManager).
TEST_CASE(tooltip_owned_via_destroy_widget_tree) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Button* btn = new Button();
    btn->setSize(FVector2(100.0f, 32.0f));

    Tooltip* tip = Tooltip::attachTo(btn);
    CHECK_NOT_NULL(tip);

    // The tooltip should be a child of the overlay, not of `btn`.
    bool foundOnOverlay = false;
    for (Widget* w : ui.getOverlayRoot()->getChildren()) {
        if (w == tip) { foundOnOverlay = true; break; }
    }
    CHECK(foundOnOverlay);
    CHECK(btn->getChildren().empty());

    // Tear down `btn` — overlay still holds the tip, no UAF on the target.
    destroyWidgetTree(btn);

    // Host frees the tip (overlay does NOT free it for us).
    tip->detach();
    destroyWidgetTree(tip);

    ui.shutdown();
}

TEST_CASE(tooltip_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("Tooltip"));

    Widget* widget = factory.create("Tooltip");
    CHECK_NOT_NULL(widget);
    Tooltip* original = dynamic_cast<Tooltip*>(widget);
    CHECK_NOT_NULL(original);
    original->setText(L"Press to save");
    original->setHoverDelay(0.7f);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"Tooltip\"") != std::string::npos);
    CHECK(json.find("Press to save") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    Tooltip* restoredTip = dynamic_cast<Tooltip*>(restored);
    CHECK_NOT_NULL(restoredTip);
    CHECK(restoredTip->getText() == L"Press to save");
    CHECK_FLOAT_EQ(restoredTip->getHoverDelay(), 0.7f, 1e-5f);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// Phase A (A2): Tooltip::attachTo mounts the tip on the overlay, not as a
// child of the target. P3 update: tip is externally-owned — host must
// detach + delete it; ui.shutdown() will not free it.
TEST_CASE(tooltip_attach_to_mounts_on_overlay) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    CHECK_NOT_NULL(tip);

    bool foundOnOverlay = false;
    for (Widget* w : ui.getOverlayRoot()->getChildren()) {
        if (w == tip) { foundOnOverlay = true; break; }
    }
    CHECK(foundOnOverlay);
    CHECK(btn.getChildren().empty());

    tip->detach();
    destroyWidgetTree(tip);
    ui.shutdown();
}

// Phase A (A2): Tooltip::tick viewport fallback. If the caller passes
// (0,0) the tip pulls live metrics from UIManager::getClientSize().
// This is the production case — hosts that wire Tooltip::tick to a
// per-frame delta shouldn't need to know the viewport size.
TEST_CASE(tooltip_tick_viewport_fallback_to_uimanager) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 100.0f);   // tiny height to force flip

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 60.0f));   // near bottom
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.1f);
    tip->setText(L"Fallback test");

    // Pass viewport=(0,0) → tick should pull from UIManager (800x100).
    // Anchor is at y ∈ [60, 92); default-below would put tip at y=98,
    // which overflows the 100-tall viewport — flip kicks in.
    tip->tick(0.2f, FVector2(80.0f, 70.0f), FVector2(0.0f, 0.0f));
    CHECK(tip->isShowing());

    const FRectangle tb = tip->getWorldBounds();
    // After flip, tip should be ABOVE the anchor (anchor minY=60).
    CHECK(tb.maxY <= 60.0f);

    tip->detach();
    destroyWidgetTree(tip);
    ui.shutdown();
}

// ============================================================================
// PR-C1 — passive hover-timer driver: UIManager::update(dt) ticks every
// attached Tooltip automatically, host doesn't call tip->tick() directly.
// ============================================================================

// PR-C1.1 — UIManager drives tick without any manual tip->tick call.
TEST_CASE(tooltip_uimanager_drives_tick_no_manual_call) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();
    // Tooltip is overlay-mounted; doesn't need to live under _root,
    // but a host that uses a rootless Button is fine for tick purposes
    // since the test asserts target-bounds.contains(mousePos).

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.3f);
    tip->setText(L"Passive hover test");

    // Move the cursor over the button; the driver uses the latest
    // onMouseMove position from _hasLastMouse/_lastMouseX/_lastMouseY.
    ui.onMouseMove(60.0f, 60.0f);

    // One big update(dt) past the delay → tooltip should appear.
    // We assert >= delay so the test isn't sensitive to slight dt rounding.
    ui.update(0.4f);

    CHECK(tip->isShowing());

    tip->detach();
    destroyWidgetTree(tip);
    ui.shutdown();
}

// PR-C1.2 — leaving the target hides the tip on the next update().
TEST_CASE(tooltip_uimanager_drives_hide_on_mouse_leave) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.3f);
    tip->setText(L"Leave hides");

    ui.onMouseMove(60.0f, 60.0f);
    ui.update(0.2f);   // 0.2 < 0.3 → still hidden
    CHECK_FALSE(tip->isShowing());
    ui.update(0.2f);   // cumulative 0.4 >= 0.3 → shown
    CHECK(tip->isShowing());

    // Move outside target bounds.
    ui.onMouseMove(900.0f, 900.0f);
    ui.update(0.1f);
    CHECK_FALSE(tip->isShowing());

    tip->detach();
    destroyWidgetTree(tip);
    ui.shutdown();
}

// PR-C1.3 — hover-still accumulates: cursor parked inside the target
// across multiple frames still triggers the tooltip (Windows convention).
// Bonus check: with no onMouseMove seen yet (tryGet state before the
// first move), update(1.0f) must NOT show the tooltip because
// getMousePos() returns (0,0) and (0,0) is outside the button bounds.
TEST_CASE(tooltip_hover_still_accumulates_across_frames) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.3f);
    tip->setText(L"Still");

    // One onMouseMove, then several update() frames with the cursor
    // parked. Total dt sums to 0.3 ≥ delay.
    ui.onMouseMove(60.0f, 60.0f);
    for (int i = 0; i < 3; ++i) {
        ui.update(0.1f);
    }
    CHECK(tip->isShowing());

    tip->detach();
    destroyWidgetTree(tip);
    ui.shutdown();
}

// PR-C1.3b — no mouse state ⇒ no false hover. _hasLastMouse guards
// getMousePos() so (0,0) can't trigger a tip on a target whose bounds
// happen to contain the origin (none in our tests, but the contract
// matters for hosts that attach a tooltip to a (0,0) anchor).
TEST_CASE(tooltip_no_mouse_pos_does_not_show) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    // Anchor at origin (0,0); without the _hasLastMouse guard,
    // getMousePos() returning (0,0) would falsely trigger hover.
    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(0.0f, 0.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.1f);
    tip->setText(L"No mouse yet");

    // No onMouseMove → _hasLastMouse == false.
    CHECK_FALSE(ui.hasMousePos());
    ui.update(1.0f);
    CHECK_FALSE(tip->isShowing());

    tip->detach();
    destroyWidgetTree(tip);
    ui.shutdown();
}

// PR-C1.4 — detach removes from the driver list; further update() calls
// don't deref the (now-cleared) tooltip. Also: a tooltip still alive on
// the overlay after detach should not show up again on a subsequent
// mouse-move.
TEST_CASE(tooltip_detach_unregisters_from_uimanager) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.1f);
    tip->setText(L"Will detach");

    ui.onMouseMove(60.0f, 60.0f);
    ui.update(0.2f);
    CHECK(tip->isShowing());

    tip->detach();
    CHECK(tip->getTarget() == nullptr);
    CHECK_FALSE(tip->isShowing());

    // Move back over the (now detached) target's bounds. The tooltip
    // must not appear again because it was unregistered from the
    // driver. (`btn` still exists; the bounds are unchanged — only the
    // tooltip's reference to the target was cleared.)
    ui.onMouseMove(60.0f, 60.0f);
    ui.update(1.0f);
    CHECK_FALSE(tip->isShowing());

    // Manual destroy after detach: ~Tooltip's unregisterTooltip is a
    // no-op (already detached) but shouldn't crash.
    destroyWidgetTree(tip);

    ui.shutdown();
}

// PR-C1.5 — overlay teardown (closePopup OR shutdown) clears the driver
// list. Subsequent update() must not crash even if the tooltip was
// never explicitly detached. P3 update: tooltip is now externally-owned,
// so closePopup(destroy=true) only detaches — the host must `delete` the
// tip itself (matches the public header contract "caller owns lifetime").
TEST_CASE(tooltip_overlay_unmount_unregisters_from_uimanager_list) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.1f);

    // Confirm tip is on the overlay (sanity).
    bool foundOnOverlay = false;
    for (Widget* w : ui.getOverlayRoot()->getChildren()) {
        if (w == tip) { foundOnOverlay = true; break; }
    }
    CHECK(foundOnOverlay);

    // closePopup removes the tip from the overlay AND unregisters from
    // the driver list. The externally-owned flag suppresses destroyWidgetTree
    // from `delete`-ing the tip — the host frees it below.
    ui.closePopup(tip, /*destroy=*/true);

    // Tip should be gone from the overlay now.
    bool stillOnOverlay = false;
    for (Widget* w : ui.getOverlayRoot()->getChildren()) {
        if (w == tip) { stillOnOverlay = true; break; }
    }
    CHECK_FALSE(stillOnOverlay);

    // Subsequent updates must not crash — the driver list was cleared
    // in closePopup's Tooltip unregister branch.
    ui.onMouseMove(60.0f, 60.0f);
    ui.update(1.0f);

    // Host frees the tip (P3 contract).
    destroyWidgetTree(tip);

    ui.shutdown();
}

// Tooltip must NOT fire when its target is setVisible(false), even if
// mouse coords still geometrically intersect the target's stale world
// bounds. Catches the Gallery symptom where hovering a TextInput on
// the Input page fired the Capabilities C1 tooltip — page_capabilities
// is setVisible(false) by showPage(), but the target pointer remains
// registered in UIManager::_tooltips, so any layout where hidden
// geometry overlaps the live mouse position would have triggered.
TEST_CASE(tooltip_does_not_fire_when_target_invisible) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.2f);
    tip->setText(L"Should not show");

    // Park the mouse inside the target's bounds, then hide the target.
    // Without the visibility gate, the hover timer would accumulate and
    // show the tip.
    ui.onMouseMove(60.0f, 60.0f);
    btn.setVisible(false);
    ui.update(0.3f);   // cumulative 0.3 >= 0.2 — would show if no gate
    CHECK_FALSE(tip->isShowing());

    // Re-showing the target should re-arm the timer (treat as fresh
    // hover intent: user just "discovered" the target).
    btn.setVisible(true);
    ui.update(0.3f);
    CHECK(tip->isShowing());

    tip->detach();
    destroyWidgetTree(tip);
    ui.shutdown();
}

TEST_SUITE_END
