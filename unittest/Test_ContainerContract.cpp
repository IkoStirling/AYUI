#include "AYTest.h"
#include "AYUI/Widget.h"
#include "AYUI/ScrollableWidget.h"
#include "AYUI/PopupAnchor.h"
#include <fstream>
#include <string>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_ContainerContract)

// 1.1 — default getClientRect() returns getWorldBounds()
TEST_CASE(container_contract_default_client_rect_equals_world_bounds) {
    CompoundWidget* w = new CompoundWidget();
    w->setPosition(FVector2(10.0f, 20.0f));
    w->setSize(FVector2(100.0f, 50.0f));
    w->performLayout();
    const FRectangle wb = w->getWorldBounds();
    const FRectangle cr = w->getClientRect();
    CHECK_FLOAT_EQ(wb.minX, cr.minX, 1e-5f);
    CHECK_FLOAT_EQ(wb.minY, cr.minY, 1e-5f);
    CHECK_FLOAT_EQ(wb.maxX, cr.maxX, 1e-5f);
    CHECK_FLOAT_EQ(wb.maxY, cr.maxY, 1e-5f);
    delete w;
}

// 1.1 — gate: hit outside clientRect still returns self if inside
// world bounds (chrome fallback). hit inside clientRect descends.
TEST_CASE(compound_descend_hit_test_clipped_gates_descend_by_client_rect) {
    // Root at (0,0) with size 100x100 = world bounds [0,0,100,100].
    // clientRect also [0,0,100,100] by default.
    // Child at (5,5) with size 10x10 → world bounds [5,5,15,15].
    CompoundWidget* root = new CompoundWidget();
    root->setSize(FVector2(100.0f, 100.0f));

    Widget* child = new Widget();
    child->setPosition(FVector2(5.0f, 5.0f));
    child->setSize(FVector2(10.0f, 10.0f));
    root->addChild(child);
    root->performLayout();

    // Hit at (50, 50) — inside client, on no specific child → root.
    Widget* hit1 = compoundDescendHitTestClipped(
        root, root->getClientRect(), FVector2(50.0f, 50.0f));
    CHECK(hit1 == root);

    // Hit at (10, 10) — inside client, on child.
    Widget* hit2 = compoundDescendHitTestClipped(
        root, root->getClientRect(), FVector2(10.0f, 10.0f));
    CHECK(hit2 == child);

    // Hit at (200, 200) — outside both client and world → null.
    Widget* hit3 = compoundDescendHitTestClipped(
        root, root->getClientRect(), FVector2(200.0f, 200.0f));
    CHECK_NULL(hit3);

    delete child;
    delete root;
}

// 1.1 — the contract comment block is present in AYWidget.h
TEST_CASE(container_contract_documented_in_widget_h) {
    // Tests run from the build directory, not the source root. Use the
    // AYUI_SOURCE_DIR macro passed by CMake (see unittest/CMakeLists.txt).
#ifndef AYUI_SOURCE_DIR
#error "AYUI_SOURCE_DIR must be defined by the unittest CMakeLists.txt"
#endif
    const std::string srcPath =
        std::string(AYUI_SOURCE_DIR) + "/include/AYUI/Widget.h";
    std::ifstream f(srcPath);
    CHECK_NOT_NULL((void*)(f.is_open() ? &f : nullptr));
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    CHECK(content.find("Container contract for clip + offset + hitTest")
          != std::string::npos);
    CHECK(content.find("compoundDescendHitTestClipped")
          != std::string::npos);
}

// 1.2 — ScrollableWidget::clampScrollOffset pure function
TEST_CASE(clamp_scroll_offset_pure_function_clamps_negatives_and_max) {
    const FVector2 vp(100.0f, 100.0f);
    const FVector2 content(500.0f, 800.0f);  // maxOffset = (400, 700)
    // Negative clamps to 0.
    const FVector2 r1 = ScrollableWidget::clampScrollOffset(
        FVector2(-5.0f, -10.0f), vp, content);
    CHECK_FLOAT_EQ(r1.x, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(r1.y, 0.0f, 1e-5f);
    // Over-max clamps to max.
    const FVector2 r2 = ScrollableWidget::clampScrollOffset(
        FVector2(9999.0f, 9999.0f), vp, content);
    CHECK_FLOAT_EQ(r2.x, 400.0f, 1e-5f);
    CHECK_FLOAT_EQ(r2.y, 700.0f, 1e-5f);
    // Equal viewport/content: no scrollable area, target clamps to 0.
    // (clampScrollOffset shares ScrollableWidget::getMaxScrollOffset's
    // convention — no sentinel viewport guard, unlike popupAnchorPlacement
    // which guards viewport.{x,y} > 0.0f explicitly.)
    const FVector2 r3 = ScrollableWidget::clampScrollOffset(
        FVector2(10.0f, 20.0f), FVector2(0.0f, 0.0f), FVector2(0.0f, 0.0f));
    CHECK_FLOAT_EQ(r3.x, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(r3.y, 0.0f, 1e-5f);
}

// 1.4 — popupAnchorPlacement x-clamp (Tooltip latent-bug fix)
TEST_CASE(popup_anchor_clamps_x_when_target_near_left_edge) {
    const FVector2 below(10.0f, 100.0f);
    const FVector2 above(10.0f, -300.0f);  // above doesn't fit anyway
    const FVector2 size(900.0f, 50.0f);     // wider than viewport
    const FVector2 viewport(800.0f, 600.0f);
    const FVector2 r = popupAnchorPlacement(below, above, size, viewport);
    // x clamped to max(0, 800-900) → 0 (popup wider than viewport).
    CHECK_FLOAT_EQ(r.x, 0.0f, 1e-5f);
    // y stays at below.y since above doesn't fit (above.y < 0).
    CHECK_FLOAT_EQ(r.y, 100.0f, 1e-5f);
}

TEST_CASE(popup_anchor_flip_above_when_bottom_overflows) {
    const FVector2 below(50.0f, 580.0f);   // bottom is 580+50=630 > 600
    const FVector2 above(50.0f, 200.0f);   // above fits
    const FVector2 size(100.0f, 50.0f);
    const FVector2 viewport(800.0f, 600.0f);
    const FVector2 r = popupAnchorPlacement(below, above, size, viewport);
    CHECK_FLOAT_EQ(r.x, 50.0f, 1e-5f);
    CHECK_FLOAT_EQ(r.y, 200.0f, 1e-5f);
}

TEST_CASE(popup_anchor_sentinel_viewport_does_not_clamp) {
    const FVector2 r = popupAnchorPlacement(
        FVector2(10.0f, 20.0f), FVector2(10.0f, -30.0f),
        FVector2(500.0f, 50.0f), FVector2(0.0f, 0.0f));
    CHECK_FLOAT_EQ(r.x, 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(r.y, 20.0f, 1e-5f);
}

TEST_SUITE_END
