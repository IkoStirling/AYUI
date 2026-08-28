#include "AYTest.h"
#include "AYUI/Spinner.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/MockRenderer.h"

#include <cmath>

using namespace ayt::ui;
using namespace ayt::math;

// UI-anim cut 2 — Spinner: four 4x4 rounded dots orbiting the widget
// centre on a 6px-radius circle, phase-driven by tick (0.9s/rev).
TEST_SUITE(AYUI_Spinner)

namespace {

struct Dot {
    float cx;
    float cy;
    float alpha;
};

// The four orbit dots are the only rounded rects (radius 2) in the
// spinner's draw list.
static std::vector<Dot> dots(const MockRenderer& r) {
    std::vector<Dot> out;
    for (const auto& dc : r.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) continue;
        if (fabsf(dc.floatParam1 - 2.0f) > 1e-4f) continue;
        out.push_back(Dot{
            (dc.bounds.minX + dc.bounds.maxX) * 0.5f,
            (dc.bounds.minY + dc.bounds.maxY) * 0.5f,
            dc.color.w});
    }
    return out;
}

} // namespace

TEST_CASE(spinner_initial_state) {
    Spinner s;
    CHECK_FLOAT_EQ(s.getWidth(), 24.0f, 1e-5f);
    CHECK_FLOAT_EQ(s.getHeight(), 24.0f, 1e-5f);
}

TEST_CASE(spinner_renders_4_dots) {
    Spinner s;
    s.setSize(FVector2(24.0f, 24.0f));
    s.setPosition(FVector2(0.0f, 0.0f));

    MockRenderer r;
    s.render(r);
    const auto d = dots(r);
    CHECK(d.size() == 4u);

    // All four sit on the 6px orbit around the centre (12, 12).
    int orbitMismatchCount = 0;
    for (const Dot& dot : d) {
        const float dist = std::sqrt((dot.cx - 12.0f) * (dot.cx - 12.0f) +
                                     (dot.cy - 12.0f) * (dot.cy - 12.0f));
        if (std::abs(dist - Spinner::kOrbitRadius) > 1e-3f) {
            ++orbitMismatchCount;
        }
    }
    CHECK(orbitMismatchCount == 0);

    // Alpha staircase {0.9, 0.55, 0.35, 0.2} — one of each.
    std::vector<float> alphas;
    for (const Dot& dot : d) alphas.push_back(dot.alpha);
    std::sort(alphas.begin(), alphas.end());
    CHECK_FLOAT_EQ(alphas[0], 0.2f, 1e-4f);
    CHECK_FLOAT_EQ(alphas[1], 0.35f, 1e-4f);
    CHECK_FLOAT_EQ(alphas[2], 0.55f, 1e-4f);
    CHECK_FLOAT_EQ(alphas[3], 0.9f, 1e-4f);
}

TEST_CASE(spinner_tick_advances_phase) {
    Spinner s;
    s.setSize(FVector2(24.0f, 24.0f));
    s.setPosition(FVector2(0.0f, 0.0f));

    MockRenderer r0;
    s.render(r0);
    const auto d0 = dots(r0);

    // Half a revolution (0.45s) later the dots have moved.
    s.tick(0.45f);
    MockRenderer r1;
    s.render(r1);
    const auto d1 = dots(r1);

    bool anyMoved = false;
    for (int i = 0; i < 4; ++i) {
        const float dx = d0[i].cx - d1[i].cx;
        const float dy = d0[i].cy - d1[i].cy;
        if (dx * dx + dy * dy > 1e-3f) anyMoved = true;
    }
    CHECK(anyMoved);
}

TEST_CASE(spinner_phase_wraps) {
    Spinner s;
    s.setSize(FVector2(24.0f, 24.0f));
    s.setPosition(FVector2(0.0f, 0.0f));

    MockRenderer r0;
    s.render(r0);
    const auto d0 = dots(r0);

    // Exactly one period → phase wraps to 0 → dots back in place.
    s.tick(Spinner::kPeriodSeconds);
    MockRenderer r1;
    s.render(r1);
    const auto d1 = dots(r1);

    bool allSame = true;
    for (int i = 0; i < 4; ++i) {
        if (fabsf(d0[i].cx - d1[i].cx) > 1e-3f ||
            fabsf(d0[i].cy - d1[i].cy) > 1e-3f) {
            allSame = false;
        }
    }
    CHECK(allSame);
}

TEST_CASE(spinner_factory_registered) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("Spinner"));
    Widget* w = factory.create("Spinner");
    CHECK_NOT_NULL(w);
    CHECK_NOT_NULL(dynamic_cast<Spinner*>(w));
    destroyWidgetTree(w);
}

TEST_SUITE_END
