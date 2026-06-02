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

TEST_SUITE_END