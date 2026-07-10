#include "AYTest.h"
#include "AYWindow.h"
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

TEST_SUITE_END