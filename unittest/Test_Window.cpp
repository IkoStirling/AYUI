#include "AYTest.h"
#include "AYWindow.h"
#include "AYMockRenderer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Window)

TEST_CASE(window_initial_state) {
    Window window;
    CHECK(window.getTitle() == L"Window");
    CHECK(window.isMovable() == true);
    CHECK(window.isResizable() == false);
    CHECK(window.isClosable() == true);
    CHECK(window.isModal() == false);
}

TEST_CASE(window_set_title) {
    Window window;
    window.setTitle(L"My Window");
    CHECK(window.getTitle() == L"My Window");
}

TEST_CASE(window_title_bar_height) {
    Window window;
    CHECK(window.getTitleBarHeight() == 28.0f);  // default

    window.setTitleBarHeight(40.0f);
    CHECK(window.getTitleBarHeight() == 40.0f);
}

TEST_CASE(window_movable_flag) {
    Window window;
    window.setMovable(false);
    CHECK(window.isMovable() == false);

    window.setMovable(true);
    CHECK(window.isMovable() == true);
}

TEST_CASE(window_resizable_flag) {
    Window window;
    window.setResizable(true);
    CHECK(window.isResizable() == true);

    window.setResizable(false);
    CHECK(window.isResizable() == false);
}

TEST_CASE(window_hit_test_title_bar) {
    Window window;
    window.setPosition(FVector2(100.0f, 100.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setTitleBarHeight(28.0f);

    // Click on title bar area
    Widget* hit = window.hitTest(FVector2(200.0f, 110.0f));
    CHECK(hit == &window);
}

TEST_CASE(window_hit_test_outside) {
    Window window;
    window.setPosition(FVector2(100.0f, 100.0f));
    window.setSize(FVector2(400.0f, 300.0f));

    // Click outside window
    Widget* hit = window.hitTest(FVector2(50.0f, 50.0f));
    CHECK(hit == nullptr);
}

TEST_CASE(window_hit_test_content_area) {
    Window window;
    window.setPosition(FVector2(100.0f, 100.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setTitleBarHeight(28.0f);

    // Click on content area (below title bar)
    Widget* hit = window.hitTest(FVector2(200.0f, 200.0f));
    CHECK(hit == &window);
}

TEST_CASE(window_on_close_callback) {
    Window window;
    bool closed = false;
    window.setOnClose([&]() { closed = true; });

    // Trigger close (would need actual close mechanism)
    CHECK(window.isClosable() == true);
}

TEST_CASE(window_min_size_enforced) {
    Window window;
    window.setMinSize(160.0f, 100.0f);
    window.setSize(FVector2(80.0f, 40.0f));
    CHECK(window.getWidth() == 160.0f);
    CHECK(window.getHeight() == 100.0f);
}

TEST_CASE(window_drag_moves_position) {
    Widget root;
    root.setSize(FVector2(800.0f, 600.0f));

    Window window;
    window.setPosition(FVector2(100.0f, 80.0f));
    window.setSize(FVector2(240.0f, 180.0f));
    root.addChildExternal(&window);

    UIMouseEvent down(FVector2(150.0f, 90.0f), 0);
    CHECK(window.onMouseButtonDown(down));
    CHECK(window.isDragging());
    CHECK(!window.isLayoutPositionManaged());

    UIMouseEvent move(FVector2(220.0f, 140.0f), 0);
    CHECK(window.onMouseMove(move));
    CHECK(window.getPosition().x == 170.0f);
    CHECK(window.getPosition().y == 130.0f);

    UIMouseEvent up(FVector2(220.0f, 140.0f), 0);
    CHECK(window.onMouseButtonUp(up));
    CHECK(!window.isDragging());
}

TEST_CASE(window_drag_clamped_to_parent_bounds) {
    Widget root;
    root.setSize(FVector2(400.0f, 300.0f));

    Window window;
    window.setMinSize(120.0f, 80.0f);
    window.setPosition(FVector2(20.0f, 20.0f));
    window.setSize(FVector2(200.0f, 150.0f));
    root.addChildExternal(&window);

    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 30.0f), 0)));
    CHECK(window.onMouseMove(UIMouseEvent(FVector2(500.0f, 400.0f), 0)));
    CHECK(window.getPosition().x == 200.0f);
    CHECK(window.getPosition().y == 150.0f);
}

TEST_CASE(window_drag_keeps_min_visible_when_larger_than_parent) {
    Widget root;
    root.setSize(FVector2(300.0f, 200.0f));

    Window window;
    window.setMinSize(160.0f, 120.0f);
    window.setPosition(FVector2(0.0f, 0.0f));
    window.setSize(FVector2(400.0f, 260.0f));
    root.addChildExternal(&window);

    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(20.0f, 10.0f), 0)));
    CHECK(window.onMouseMove(UIMouseEvent(FVector2(500.0f, 500.0f), 0)));

    CHECK(window.getPosition().x == 140.0f);
    CHECK(window.getPosition().y == 80.0f);
}

// =============================================================================
// Phase D (D1) — SE-corner resize tests (PR-1)
// =============================================================================
//
// Coverage:
//   - Resizable-off never enters the resize hit zone (hitTestResizeEdge == None).
//   - Resizable-on: SE band hit → onMouseButtonDown begins resize + state.
//   - Drag-down→up grows the size (mirror of title-bar drag pattern).
//   - minSize clamp: dragging into the SE corner below min keeps the size at
//     (>= minW, >= minH); setSize already does this.
//   - onResize fires EXACTLY ONCE on button-up after a real change, never
//     during the drag.
//   - The 3-dot grip is rendered when _resizable, and disappears when off.
// =============================================================================

TEST_CASE(window_resizable_off_no_grip_no_cursor) {
    Window window;
    window.setPosition(FVector2(0.0f, 0.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    // _resizable defaults to false.
    CHECK(window.isResizable() == false);
    // Hit the SE band — band is a 16x16 box at (maxX-16, maxY-16)..(maxX, maxY).
    const ResizeEdge edge = window.hitTestResizeEdge(FVector2(395.0f, 295.0f));
    CHECK(edge == ResizeEdge::None);
    // And a click there doesn't start a resize.
    const UIMouseEvent e(FVector2(395.0f, 295.0f), 0);
    CHECK_FALSE(window.onMouseButtonDown(e));
    CHECK_FALSE(window.isResizing());
}

TEST_CASE(window_resizable_on_hit_se_returns_self) {
    Window window;
    window.setPosition(FVector2(0.0f, 0.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);

    // SE band is (400-16, 300-16)..(400, 300) = (384..400, 284..300).
    CHECK(window.hitTestResizeEdge(FVector2(395.0f, 295.0f)) == ResizeEdge::BottomRight);

    const UIMouseEvent e(FVector2(395.0f, 295.0f), 0);
    CHECK(window.onMouseButtonDown(e));
    CHECK(window.isResizing());
    CHECK(window.getActiveResizeEdge() == ResizeEdge::BottomRight);
}

TEST_CASE(window_resize_drag_right_increases_size) {
    Window window;
    window.setPosition(FVector2(0.0f, 0.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);

    const FVector2 se(395.0f, 295.0f);
    CHECK(window.onMouseButtonDown(UIMouseEvent(se, 0)));
    // Drag SE+50/+40.
    CHECK(window.onMouseMove(UIMouseEvent(FVector2(445.0f, 335.0f), 0)));
    CHECK(window.getSize().x == 450.0f);
    CHECK(window.getSize().y == 340.0f);

    // Release — _isResizing clears.
    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(445.0f, 335.0f), 0)));
    CHECK_FALSE(window.isResizing());
    CHECK(window.getActiveResizeEdge() == ResizeEdge::None);
}

TEST_CASE(window_resize_drag_clamps_to_min) {
    Window window;
    window.setPosition(FVector2(0.0f, 0.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);
    window.setMinSize(200.0f, 150.0f);

    // Start at SE corner. Drag toward the inside (NW direction). setSize
    // already clamps to _minSize, so even a drag of -1000,-1000 caps at min.
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(395.0f, 295.0f), 0)));
    window.onMouseMove(UIMouseEvent(FVector2(100.0f, 100.0f), 0));
    CHECK(window.getSize().x >= 200.0f);
    CHECK(window.getSize().y >= 150.0f);
    CHECK(window.onMouseButtonUp(UIMouseEvent(FVector2(100.0f, 100.0f), 0)));
}

TEST_CASE(window_resize_fires_on_resize_callback_on_release_only) {
    Window window;
    window.setPosition(FVector2(0.0f, 0.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);

    int fireCount = 0;
    FVector2 lastOld;
    FVector2 lastNew;
    window.setOnResize([&](const FVector2& oldSize, const FVector2& newSize) {
        ++fireCount;
        lastOld = oldSize;
        lastNew = newSize;
    });

    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(395.0f, 295.0f), 0)));
    // Drag — no fire yet (Q4: drag-internal noise-free).
    window.onMouseMove(UIMouseEvent(FVector2(495.0f, 395.0f), 0));
    CHECK(fireCount == 0);
    window.onMouseMove(UIMouseEvent(FVector2(525.0f, 425.0f), 0));
    CHECK(fireCount == 0);

    // Release — fires once with the full delta from initial size.
    window.onMouseButtonUp(UIMouseEvent(FVector2(525.0f, 425.0f), 0));
    CHECK(fireCount == 1);
    CHECK(lastOld.x == 400.0f);
    CHECK(lastOld.y == 300.0f);
    CHECK(lastNew.x == 530.0f);
    CHECK(lastNew.y == 430.0f);

    // Click-without-drag does NOT fire. After the first drag, the SE band
    // moved with the new size (530×430) — the old (395, 295) is no longer
    // in the band, so the second click at the NEW SE corner (525, 425) is
    // the no-drag case.
    CHECK(window.onMouseButtonDown(UIMouseEvent(FVector2(525.0f, 425.0f), 0)));
    CHECK(fireCount == 1);
    // Release at the same point.
    window.onMouseButtonUp(UIMouseEvent(FVector2(525.0f, 425.0f), 0));
    CHECK(fireCount == 1);  // unchanged — size didn't move
}

TEST_CASE(window_resize_renders_grip_when_resizable) {
    Window window;
    window.setPosition(FVector2(0.0f, 0.0f));
    window.setSize(FVector2(400.0f, 300.0f));
    window.setResizable(true);

    MockRenderer renderer;
    window.render(renderer);

    // Count the 3 dots: each is a Rect with color near (0.5, 0.5, 0.55, 0.8)
    // and a 1.5×1.5 size (very small).
    int gripDots = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) continue;
        const FVector4 c = dc.color;
        if (std::abs(c.x - 0.5f) < 0.01f &&
            std::abs(c.y - 0.5f) < 0.01f &&
            std::abs(c.z - 0.55f) < 0.01f &&
            dc.bounds.maxX - dc.bounds.minX <= 2.0f &&
            dc.bounds.maxY - dc.bounds.minY <= 2.0f) {
            ++gripDots;
        }
    }
    CHECK(gripDots == 3);

    // Toggle off — grip should disappear (3 dots gone).
    window.setResizable(false);
    MockRenderer renderer2;
    window.render(renderer2);
    int gripDotsOff = 0;
    for (const auto& dc : renderer2.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) continue;
        const FVector4 c = dc.color;
        if (std::abs(c.x - 0.5f) < 0.01f &&
            std::abs(c.y - 0.5f) < 0.01f &&
            std::abs(c.z - 0.55f) < 0.01f) {
            ++gripDotsOff;
        }
    }
    CHECK(gripDotsOff == 0);
}

TEST_SUITE_END