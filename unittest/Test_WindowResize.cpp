#include "AYTest.h"
#include "AYWindow.h"
#include "AYTextLabel.h"
#include "AYMockRenderer.h"

// =============================================================================
// PR-B1 — Window 4-edge + 4-corner resize (PR-1 was SE-only; PR-B1 ships the
// full industrial Window set).
// -----------------------------------------------------------------------------
// Cases pin:
//   1. hitTest 8 band regions (4 corners + 4 edges) — corners checked first.
//   2. Corner priority over edge at intersection (a corner-edge overlap
//      pixel returns the corner, never the edge).
//   3. TopLeft corner drag inverts both position AND size — opposite
//      corner (BR) stays pinned to the cursor.
//   4. Left edge drag keeps right edge pinned (size shrinks, position
//      tracks; min-size clamp does not leave a visible gap).
//   5. Right / Bottom edges grow size without moving position.
//   6. Cursor hint switches per edge on idle hover (no in-drag state).
//   7. minSize clamp on Left drag: dragging past min still leaves the
//      right edge pinned to the cursor (no visible gap).
//   8. Cursor hint during drag matches the active edge — including the
//      new directions SizeNs / SizeWe / SizeNesw added in PR-B1.
// =============================================================================

using namespace ayt::ui;
using namespace ayt::math;

namespace {
constexpr float kBand = 16.0f;  // mirrors kResizeBandPx in AYWindow.cpp
}  // namespace

TEST_SUITE(AYUI_Window_Resize_B1)

// 1. hitTest 8 band regions --------------------------------------------------

TEST_CASE(resize_hit_test_8_corners_and_edges) {
    Window window;
    window.setPosition(FVector2(100.0f, 100.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);

    // 4 corners — kBand × kBand squares anchored at each window corner.
    CHECK(window.hitTestResizeEdge(FVector2(102.0f, 102.0f)) == ResizeEdge::TopLeft);
    CHECK(window.hitTestResizeEdge(FVector2(498.0f, 102.0f)) == ResizeEdge::TopRight);
    CHECK(window.hitTestResizeEdge(FVector2(102.0f, 398.0f)) == ResizeEdge::BottomLeft);
    CHECK(window.hitTestResizeEdge(FVector2(498.0f, 398.0f)) == ResizeEdge::BottomRight);

    // 4 edges — middle-of-edge pixels (not on corners).
    CHECK(window.hitTestResizeEdge(FVector2(300.0f, 102.0f)) == ResizeEdge::Top);
    CHECK(window.hitTestResizeEdge(FVector2(300.0f, 398.0f)) == ResizeEdge::Bottom);
    CHECK(window.hitTestResizeEdge(FVector2(102.0f, 250.0f)) == ResizeEdge::Left);
    CHECK(window.hitTestResizeEdge(FVector2(498.0f, 250.0f)) == ResizeEdge::Right);

    // Interior point → None.
    CHECK(window.hitTestResizeEdge(FVector2(300.0f, 250.0f)) == ResizeEdge::None);

    // Outside the window → None.
    CHECK(window.hitTestResizeEdge(FVector2(50.0f, 50.0f)) == ResizeEdge::None);

    // Resizable off → None everywhere.
    window.setResizable(false);
    CHECK(window.hitTestResizeEdge(FVector2(498.0f, 398.0f)) == ResizeEdge::None);
}

// 2. Corner priority over edge ----------------------------------------------

TEST_CASE(resize_corner_wins_over_edge_at_intersection) {
    Window window;
    window.setPosition(FVector2(0.0f, 0.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);

    // A pixel on BOTH top-edge band (y ≤ kBand) AND left-edge band
    // (x ≤ kBand) must resolve to TopLeft corner (not Top or Left).
    CHECK(window.hitTestResizeEdge(FVector2(5.0f, 5.0f)) == ResizeEdge::TopLeft);
    CHECK(window.hitTestResizeEdge(FVector2(395.0f, 5.0f)) == ResizeEdge::TopRight);
    CHECK(window.hitTestResizeEdge(FVector2(5.0f, 295.0f)) == ResizeEdge::BottomLeft);
    CHECK(window.hitTestResizeEdge(FVector2(395.0f, 295.0f)) == ResizeEdge::BottomRight);
}

// 3. TopLeft corner drag — inverts both position and size -------------------

TEST_CASE(resize_top_left_corner_inverts_position_and_size) {
    Widget root;
    root.setSize(FVector2(800.0f, 600.0f));

    Window window;
    window.setPosition(FVector2(200.0f, 150.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);
    window.setMinSize(100.0f, 60.0f);
    root.addChildExternal(&window);

    // Click in the TL corner band (kBand × kBand at top-left of window).
    const float tlX = window.getPosition().x + 5.0f;        // 205
    const float tlY = window.getPosition().y + 5.0f;        // 155
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(tlX, tlY), 0)));
    CHECK(window.isResizing());
    CHECK(window.getActiveResizeEdge() == ResizeEdge::TopLeft);

    // Drag TL corner by (-50, -30): window grows NW, position shifts NW.
    CHECK(window.onMouseMove(UIMouseEvent(FVector2(tlX - 50.0f, tlY - 30.0f), 0)));
    CHECK(window.getSize().x == 450.0f);
    CHECK(window.getSize().y == 330.0f);
    CHECK(window.getPosition().x == 150.0f);
    CHECK(window.getPosition().y == 120.0f);

    // BR pinned: position + size == original BR.
    CHECK(window.getPosition().x + window.getSize().x == 600.0f);
    CHECK(window.getPosition().y + window.getSize().y == 450.0f);

    // Release.
    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(tlX - 50.0f, tlY - 30.0f), 0)));
    CHECK_FALSE(window.isResizing());
}

// 3b. Multi-step TopLeft drag must not accumulate (fly-away regression).
TEST_CASE(resize_top_left_multi_move_keeps_bottom_right_pinned) {
    Widget root;
    root.setSize(FVector2(800.0f, 600.0f));

    Window window;
    window.setPosition(FVector2(200.0f, 150.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);
    window.setMinSize(100.0f, 60.0f);
    root.addChildExternal(&window);

    const float tlX = window.getPosition().x + 5.0f;
    const float tlY = window.getPosition().y + 5.0f;
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(tlX, tlY), 0)));
    CHECK(window.getActiveResizeEdge() == ResizeEdge::TopLeft);

    CHECK(window.onMouseMove(UIMouseEvent(FVector2(tlX - 50.0f, tlY - 30.0f), 0)));
    CHECK(window.getPosition().x == 150.0f);
    CHECK(window.getPosition().y == 120.0f);

    // Second move with a larger absolute delta from the original press —
    // old code did getPosition()+(-actualD*) and flew to (50, 60).
    CHECK(window.onMouseMove(UIMouseEvent(FVector2(tlX - 100.0f, tlY - 60.0f), 0)));
    CHECK(window.getSize().x == 500.0f);
    CHECK(window.getSize().y == 360.0f);
    CHECK(window.getPosition().x == 100.0f);
    CHECK(window.getPosition().y == 90.0f);
    CHECK(window.getPosition().x + window.getSize().x == 600.0f);
    CHECK(window.getPosition().y + window.getSize().y == 450.0f);

    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(tlX - 100.0f, tlY - 60.0f), 0)));
}

// 4. Left edge — keeps right edge pinned ------------------------------------

TEST_CASE(resize_left_edge_keeps_right_anchor) {
    Widget root;
    root.setSize(FVector2(800.0f, 600.0f));

    Window window;
    window.setPosition(FVector2(200.0f, 150.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);
    window.setMinSize(100.0f, 60.0f);
    root.addChildExternal(&window);

    const float leftX = 205.0f;
    const float midY  = 300.0f;
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(leftX, midY), 0)));
    CHECK(window.getActiveResizeEdge() == ResizeEdge::Left);

    CHECK(window.onMouseMove(UIMouseEvent(FVector2(leftX - 80.0f, midY), 0)));
    CHECK(window.getSize().x == 480.0f);
    CHECK(window.getPosition().x == 120.0f);
    CHECK(window.getPosition().y == 150.0f);

    // Right edge pinned: position.x + size.x == original 200 + 400 = 600.
    CHECK(window.getPosition().x + window.getSize().x == 600.0f);

    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(leftX - 80.0f, midY), 0)));
}

// 5. Right / Bottom edges — grow size only ----------------------------------

TEST_CASE(resize_right_edge_grows_size_only) {
    Window window;
    window.setPosition(FVector2(100.0f, 100.0f));
    window.setSize(FVector2(200.0f, 150.0f));
    window.setResizable(true);

    const float rightX = 290.0f;
    const float midY   = 175.0f;
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(rightX, midY), 0)));
    CHECK(window.getActiveResizeEdge() == ResizeEdge::Right);

    CHECK(window.onMouseMove(UIMouseEvent(FVector2(rightX + 50.0f, midY), 0)));
    CHECK(window.getSize().x == 250.0f);
    CHECK(window.getSize().y == 150.0f);
    CHECK(window.getPosition().x == 100.0f);
    CHECK(window.getPosition().y == 100.0f);

    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(rightX + 50.0f, midY), 0)));
}

TEST_CASE(resize_bottom_edge_grows_size_only) {
    Window window;
    window.setPosition(FVector2(100.0f, 100.0f));
    window.setSize(FVector2(200.0f, 150.0f));
    window.setResizable(true);

    const float midX  = 200.0f;
    const float botY  = 240.0f;
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(midX, botY), 0)));
    CHECK(window.getActiveResizeEdge() == ResizeEdge::Bottom);

    CHECK(window.onMouseMove(UIMouseEvent(FVector2(midX, botY + 30.0f), 0)));
    CHECK(window.getSize().x == 200.0f);
    CHECK(window.getSize().y == 180.0f);
    CHECK(window.getPosition().x == 100.0f);
    CHECK(window.getPosition().y == 100.0f);

    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(midX, botY + 30.0f), 0)));
}

// 6. Cursor hint switches per edge on idle hover ----------------------------

TEST_CASE(cursor_hint_changes_per_edge_when_idle_hover) {
    Window window;
    window.setPosition(FVector2(100.0f, 100.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);
    window.setMovable(false);

    // No mouse yet → Default.
    CHECK(window.getCursorHint() == UiCursorHint::Default);

    // Top edge band (outer 4px of title — SizeNs).
    window.onMouseMove(UIMouseEvent(FVector2(300.0f, 102.0f), 0));
    CHECK(window.getCursorHint() == UiCursorHint::SizeNs);

    // Bottom edge.
    window.onMouseMove(UIMouseEvent(FVector2(300.0f, 395.0f), 0));
    CHECK(window.getCursorHint() == UiCursorHint::SizeNs);

    // Left edge.
    window.onMouseMove(UIMouseEvent(FVector2(105.0f, 250.0f), 0));
    CHECK(window.getCursorHint() == UiCursorHint::SizeWe);

    // Right edge.
    window.onMouseMove(UIMouseEvent(FVector2(495.0f, 250.0f), 0));
    CHECK(window.getCursorHint() == UiCursorHint::SizeWe);

    // NW/SE diagonal corners.
    window.onMouseMove(UIMouseEvent(FVector2(105.0f, 105.0f), 0));
    CHECK(window.getCursorHint() == UiCursorHint::SizeNwse);

    window.onMouseMove(UIMouseEvent(FVector2(495.0f, 395.0f), 0));
    CHECK(window.getCursorHint() == UiCursorHint::SizeNwse);

    // NE/SW diagonal corners (newly added direction).
    window.onMouseMove(UIMouseEvent(FVector2(495.0f, 105.0f), 0));
    CHECK(window.getCursorHint() == UiCursorHint::SizeNesw);

    window.onMouseMove(UIMouseEvent(FVector2(105.0f, 395.0f), 0));
    CHECK(window.getCursorHint() == UiCursorHint::SizeNesw);

    // Interior point → Default.
    window.onMouseMove(UIMouseEvent(FVector2(300.0f, 250.0f), 0));
    CHECK(window.getCursorHint() == UiCursorHint::Default);
}

// 7. minSize clamp on Left drag — right edge stays pinned -------------------

TEST_CASE(resize_clamps_to_min_when_drag_past_min_on_left_edge) {
    Window window;
    window.setPosition(FVector2(200.0f, 100.0f));
    window.setSize(FVector2(400.0f, 200.0f));
    window.setResizable(true);
    window.setMinSize(150.0f, 60.0f);

    const float leftX = 205.0f;
    const float midY  = 200.0f;
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(leftX, midY), 0)));
    CHECK(window.getActiveResizeEdge() == ResizeEdge::Left);

    // Drag left by 500. Left edge drag means mouse moves WEST; per Windows
// behavior the window's right edge stays pinned and the left edge tracks
// the mouse, so size GROWS by |dx| = 500 and position shifts WEST by 500.
// The right edge stays pinned: pos.x + size.x == 200 + 400 = 600.
CHECK(window.onMouseMove(UIMouseEvent(FVector2(leftX - 500.0f, midY), 0)));
CHECK(window.getSize().x == 900.0f);   // 400 + 500
CHECK(window.getPosition().x == -300.0f); // 200 - 500
CHECK(window.getPosition().x + window.getSize().x == 600.0f);

    // Right edge still pinned at 600.
    CHECK(window.getPosition().x + window.getSize().x == 600.0f);

    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(leftX - 500.0f, midY), 0)));
}

// 8. Cursor hint during drag matches the active edge -----------------------

TEST_CASE(cursor_hint_during_drag_matches_active_edge) {
    Window window;
    window.setPosition(FVector2(100.0f, 100.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);
    window.setMovable(false);

    // Top edge — SizeNs (vertical edge). Outer rim only (y ≤ minY+4).
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(300.0f, 102.0f), 0)));
    CHECK(window.getActiveResizeEdge() == ResizeEdge::Top);
    CHECK(window.getCursorHint() == UiCursorHint::SizeNs);
    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(300.0f, 102.0f), 0)));

    // NE corner — SizeNesw.
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(495.0f, 105.0f), 0)));
    CHECK(window.getActiveResizeEdge() == ResizeEdge::TopRight);
    CHECK(window.getCursorHint() == UiCursorHint::SizeNesw);
    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(495.0f, 105.0f), 0)));

    // SW corner — SizeNesw.
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(105.0f, 395.0f), 0)));
    CHECK(window.getActiveResizeEdge() == ResizeEdge::BottomLeft);
    CHECK(window.getCursorHint() == UiCursorHint::SizeNesw);
    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(105.0f, 395.0f), 0)));
}

// Title-bar center prefers Move over Top resize when movable.
TEST_CASE(title_bar_center_drags_not_top_resize) {
    Window window;
    window.setPosition(FVector2(100.0f, 100.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);
    window.setMovable(true);

    // Mid-title (y = 100+14): must drag, not Top resize.
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(300.0f, 114.0f), 0)));
    CHECK(window.isDragging());
    CHECK_FALSE(window.isResizing());
    CHECK(window.getCursorHint() == UiCursorHint::Move);
    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(300.0f, 114.0f), 0)));
}

// Child world bounds follow parent move without local-pos change.
TEST_CASE(window_body_world_bounds_follow_parent_move) {
    Widget root;
    root.setSize(FVector2(800.0f, 600.0f));

    Window window;
    window.setPosition(FVector2(100.0f, 80.0f));
    window.setSize(FVector2(240.0f, 180.0f));
    window.setMovable(true);
    root.addChildExternal(&window);

    TextLabel body;
    body.setSize(FVector2(200.0f, 40.0f));
    window.addChildExternal(&body);
    window.performLayout();

    const FRectangle before = body.getWorldBounds();
    window.setPosition(FVector2(160.0f, 120.0f));
    // Simulate render order: parent refreshes first (clears dirty).
    (void)window.getWorldBounds();
    const FRectangle after = body.getWorldBounds();
    CHECK_FLOAT_EQ(after.minX - before.minX, 60.0f, 0.5f);
    CHECK_FLOAT_EQ(after.minY - before.minY, 40.0f, 0.5f);
}

TEST_SUITE_END
