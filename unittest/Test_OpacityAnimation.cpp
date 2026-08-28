#include "AYTest.h"
#include "AYUI/Widget.h"
#include "AYUI/Menu.h"
#include "AYUI/UIManager.h"
#include "AYUI/MockRenderer.h"

#include <cmath>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Leaf widget that paints one opaque rect so opacity tests can observe
// the alpha that actually reaches the renderer.
struct PaintWidget : Widget {
    void onRender(IRenderBackend& renderer) override {
        renderer.drawRect(getWorldBounds(), FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    }
};

} // namespace

// PR-anim: the renderer opacity stack. Widget::render pushes per-node
// opacity; every color-emitting draw multiplies its alpha by the stack
// top. Default stack top is 1.0 so nothing shifts when no fade is active.
TEST_SUITE(AYUI_OpacityAnimation)

TEST_CASE(opacity_push_multiplies_alpha) {
    MockRenderer r;
    r.pushOpacity(0.5f);
    r.drawRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK(r.getDrawCalls().size() == 1u);
    CHECK_FLOAT_EQ(r.getDrawCalls()[0].color.w, 0.5f, 1e-5f);
    // RGB untouched — opacity fades, it does not tint.
    CHECK_FLOAT_EQ(r.getDrawCalls()[0].color.x, 1.0f, 1e-5f);
}

TEST_CASE(opacity_stack_multiplies_and_pops) {
    MockRenderer r;
    r.pushOpacity(0.5f);
    r.pushOpacity(0.5f);
    r.drawRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK_FLOAT_EQ(r.getDrawCalls()[0].color.w, 0.25f, 1e-5f);  // 0.5 × 0.5

    r.popOpacity();
    r.drawRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK_FLOAT_EQ(r.getDrawCalls()[1].color.w, 0.5f, 1e-5f);  // back to 0.5

    r.popOpacity();
    r.drawRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK_FLOAT_EQ(r.getDrawCalls()[2].color.w, 1.0f, 1e-5f);  // base frame
}

TEST_CASE(opacity_pop_never_empties_stack) {
    MockRenderer r;
    r.popOpacity();  // unbalanced pop on a bare renderer — safe no-op
    r.drawRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK_FLOAT_EQ(r.getDrawCalls()[0].color.w, 1.0f, 1e-5f);
}

TEST_CASE(opacity_clamps_out_of_range) {
    MockRenderer r;
    r.pushOpacity(2.0f);   // > 1 → clamp to 1 (no-op)
    r.drawRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK_FLOAT_EQ(r.getDrawCalls()[0].color.w, 1.0f, 1e-5f);

    r.popOpacity();
    r.pushOpacity(-1.0f);  // < 0 → clamp to 0 (fully transparent)
    r.drawRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK_FLOAT_EQ(r.getDrawCalls()[1].color.w, 0.0f, 1e-5f);
}

TEST_CASE(opacity_fades_every_draw_type) {
    MockRenderer r;
    r.pushOpacity(0.5f);
    r.drawRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f));          // flat
    r.drawText(FRectangle(0, 0, 10, 10), L"x", 12, FVector4(1.0f, 1.0f, 1.0f, 1.0f)); // text
    r.drawRoundedRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f), 2.0f);
    r.drawBorderRect(FRectangle(0, 0, 10, 10), FVector4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 2.0f);
    r.drawRectShadow(FRectangle(0, 0, 10, 10), IRenderBackend::ShadowStyle{
        FVector4(0.0f, 0.0f, 0.0f, 0.5f), FVector2(0, 2), 4.0f, 0.0f});
    // flat + text + rounded + border(8 rects via base impl) + shadow.
    CHECK(r.getDrawCalls().size() == 12u);
    int alphaMismatchCount = 0;
    for (size_t i = 0; i < r.getDrawCalls().size(); ++i) {
        // Every draw fades: alpha 1.0 × 0.5 = 0.5; the shadow is the
        // LAST call and its own alpha (0.5) multiplies on top → 0.25.
        const float expected = (i == r.getDrawCalls().size() - 1) ? 0.25f : 0.5f;
        if (std::abs(r.getDrawCalls()[i].color.w - expected) > 1e-5f) {
            ++alphaMismatchCount;
        }
    }
    CHECK(alphaMismatchCount == 0);
}

TEST_CASE(widget_default_opacity_is_noop) {
    PaintWidget w;
    w.setSize(FVector2(20.0f, 20.0f));
    MockRenderer r;
    w.render(r);
    CHECK(r.getDrawCalls().size() == 1u);
    CHECK_FLOAT_EQ(r.getDrawCalls()[0].color.w, 1.0f, 1e-5f);
}

TEST_CASE(widget_opacity_tree_multiplies) {
    // Parent fades the whole subtree; a child's own opacity multiplies
    // on top. 0.5 × 0.5 = 0.25 reaches the renderer.
    CompoundWidget parent;
    parent.setSize(FVector2(100.0f, 100.0f));
    PaintWidget child;
    child.setSize(FVector2(50.0f, 50.0f));
    parent.addChild(&child);

    parent.setOpacity(0.5f);
    child.setOpacity(0.5f);

    MockRenderer r;
    parent.render(r);
    CHECK(r.getDrawCalls().size() == 1u);
    CHECK_FLOAT_EQ(r.getDrawCalls()[0].color.w, 0.25f, 1e-5f);
}

TEST_CASE(widget_set_opacity_snap) {
    PaintWidget w;
    w.setSize(FVector2(20.0f, 20.0f));
    w.setOpacity(0.25f);
    MockRenderer r;
    w.render(r);
    CHECK_FLOAT_EQ(r.getDrawCalls()[0].color.w, 0.25f, 1e-5f);
}

TEST_CASE(animate_opacity_tween_progresses) {
    PaintWidget w;
    w.setSize(FVector2(20.0f, 20.0f));
    w.setOpacity(0.0f);
    w.animateOpacity(1.0f, 100.0f, AnimationCurve::EaseOut);  // 100 ms

    CHECK(w.isOpacityAnimating());

    w.tick(0.0f);
    CHECK_FLOAT_EQ(w.getOpacity(), 0.0f, 1e-5f);

    // EaseOut at t=0.5 → 1-(1-0.5)^2 = 0.75 → opacity 0.75.
    w.tick(0.05f);
    CHECK_FLOAT_EQ(w.getOpacity(), 0.75f, 1e-4f);
    CHECK(w.isOpacityAnimating());

    // Past the end → snaps to the target and stops.
    w.tick(0.1f);
    CHECK_FLOAT_EQ(w.getOpacity(), 1.0f, 1e-5f);
    CHECK_FALSE(w.isOpacityAnimating());
}

TEST_CASE(animate_opacity_zero_duration_snaps) {
    PaintWidget w;
    w.setSize(FVector2(20.0f, 20.0f));
    w.setOpacity(0.0f);
    w.animateOpacity(1.0f, 0.0f);  // duration ≤ 0 → immediate
    CHECK_FLOAT_EQ(w.getOpacity(), 1.0f, 1e-5f);
    CHECK_FALSE(w.isOpacityAnimating());
}

TEST_CASE(animate_opacity_spring_overshoots) {
    PaintWidget w;
    w.setSize(FVector2(20.0f, 20.0f));
    w.setOpacity(0.0f);
    w.animateOpacity(1.0f, 100.0f, AnimationCurve::Spring);

    // Spring eases AHEAD of linear on the rising half-cycle:
    // t + sin(t·2π)·0.1·(1-t) at t=0.25 = 0.25 + 0.075 = 0.325 > 0.25.
    w.tick(0.025f);
    CHECK(w.getOpacity() > 0.25f);
}

TEST_CASE(animate_opacity_target_clamped) {
    PaintWidget w;
    w.setSize(FVector2(20.0f, 20.0f));
    w.setOpacity(0.0f);
    w.animateOpacity(3.0f, 50.0f);  // out of range → clamp to 1
    w.tick(1.0f);
    CHECK_FLOAT_EQ(w.getOpacity(), 1.0f, 1e-5f);
}

TEST_CASE(menu_open_fades_in_via_ui_update) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    host.setSize(FVector2(800.0f, 600.0f));

    Menu* menu = new Menu();
    menu->addItem(L"Open");
    menu->open(&host, FVector2(100.0f, 80.0f));
    menu->setSize(FVector2(220.0f, 72.0f));

    // First frame: pop-in starts at ~0 opacity — the plate draw (the
    // rounded fill, floatParam1==3) must be nearly transparent.
    MockRenderer r0;
    menu->render(r0);
    {
        int plateCount = 0;
        int alphaMismatchCount = 0;
        for (const auto& dc : r0.getDrawCalls()) {
            if (dc.type == MockRenderer::DrawCall::Rect &&
                std::abs(dc.floatParam1 - 3.0f) < 1e-4f) {
                ++plateCount;
                if (dc.color.w >= 0.05f) {
                    ++alphaMismatchCount;
                }
            }
        }
        CHECK(plateCount > 0);
        CHECK(alphaMismatchCount == 0);
    }

    // UIManager::update drives the overlay cascade; the menu's base
    // Widget::tick advances the fade. Full duration → fully opaque.
    ui.update(0.14f);

    MockRenderer r1;
    menu->render(r1);
    {
        int plateCount = 0;
        int alphaMismatchCount = 0;
        for (const auto& dc : r1.getDrawCalls()) {
            if (dc.type == MockRenderer::DrawCall::Rect &&
                std::abs(dc.floatParam1 - 3.0f) < 1e-4f) {
                ++plateCount;
                if (std::abs(dc.color.w - 0.96f) > 1e-4f) {
                    ++alphaMismatchCount;
                }
            }
        }
        CHECK(plateCount > 0);
        CHECK(alphaMismatchCount == 0);
    }

    menu->close();
    ui.shutdown();
}

TEST_SUITE_END
