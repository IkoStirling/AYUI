#include "AYTest.h"
#include "AYThickness.h"

using namespace ayt::ui;

TEST_SUITE(AYUI_Thickness)

TEST_CASE(thickness_default) {
    Thickness t;
    CHECK(t.left == 0.0f);
    CHECK(t.top == 0.0f);
    CHECK(t.right == 0.0f);
    CHECK(t.bottom == 0.0f);
}

TEST_CASE(thickness_explicit) {
    Thickness t(1.0f, 2.0f, 3.0f, 4.0f);
    CHECK(t.left == 1.0f);
    CHECK(t.top == 2.0f);
    CHECK(t.right == 3.0f);
    CHECK(t.bottom == 4.0f);
}

TEST_CASE(thickness_uniform) {
    constexpr Thickness t = Thickness::uniform(5.0f);
    CHECK(t.left == 5.0f);
    CHECK(t.top == 5.0f);
    CHECK(t.right == 5.0f);
    CHECK(t.bottom == 5.0f);
}

TEST_CASE(thickness_symmetric) {
    constexpr Thickness t = Thickness::symmetric(8.0f, 4.0f);
    CHECK(t.left == 8.0f);
    CHECK(t.top == 4.0f);
    CHECK(t.right == 8.0f);
    CHECK(t.bottom == 4.0f);
}

TEST_SUITE_END