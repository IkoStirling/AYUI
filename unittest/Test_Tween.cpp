#include "AYTest.h"
#include "AYUI/Tween.h"
#include "AYUI/Widget.h"        // math types (FVector4) via AYTween's backend include chain
#include "AYUI/MockRenderer.h"

using namespace ayt::ui;
using namespace ayt::math;

// UI animation lane (cut 1): the lightweight tween pipeline shared by
// Widget::animateOpacity and InteractiveWidget color transitions.
TEST_SUITE(AYUI_Tween)

// Lock: easeCurve must stay point-equal to MockRenderer's animation
// interpolation (AYMockRenderer.cpp updateAnimation switch). These are
// two independent copies of the same table — Widget-side tweens and
// renderer-side animation handles interpolate identically only while
// this test passes. Change either formula and this fails.
TEST_CASE(ease_curve_table_matches_mockrenderer) {
    const AnimationCurve curves[] = {
        AnimationCurve::EaseIn,
        AnimationCurve::EaseOut,
        AnimationCurve::EaseInOut,
        AnimationCurve::Spring,
        AnimationCurve::CubicBezier,
        AnimationCurve::Linear,
    };
    const float ts[] = { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f };

    MockRenderer r;
    int updateFailureCount = 0;
    int valueMismatchCount = 0;
    for (AnimationCurve c : curves) {
        for (float t : ts) {
            // Fresh handle per point; single-step update lands elapsed
            // exactly at t·duration (t < 1 so the tween is still running
            // and current is the interpolated value).
            IRenderBackend::AnimationHandle h =
                r.createAnimation(0.0f, 1.0f, 1.0f, c);
            if (!r.updateAnimation(h, t)) {
                ++updateFailureCount;
            }
            const float mock = r.getAnimationValue(h);
            // Spring uses math::sin vs std::sin — a ulp or two may differ.
            if (std::abs(mock - easeCurve(t, c)) > 1e-5f) {
                ++valueMismatchCount;
            }
        }
    }
    CHECK(updateFailureCount == 0);
    CHECK(valueMismatchCount == 0);
}

TEST_CASE(anim_state_advance_progresses) {
    AnimState<float> a;
    CHECK_FALSE(a.active);

    a.start(0.0f, 1.0f, 100.0f, AnimationCurve::EaseOut);
    CHECK(a.active);

    float t = -1.0f;
    CHECK(a.advance(0.0f, t));          // still running, t=0
    CHECK_FLOAT_EQ(t, 0.0f, 1e-5f);

    // EaseOut at t=0.5 → 0.75.
    CHECK(a.advance(0.05f, t));
    CHECK_FLOAT_EQ(t, 0.75f, 1e-4f);
    CHECK(a.active);

    // elapsed 0.1 == duration → completes (returns false, t=1).
    CHECK_FALSE(a.advance(0.05f, t));
    CHECK_FALSE(a.active);
    CHECK_FLOAT_EQ(t, 1.0f, 1e-5f);
}

TEST_CASE(anim_state_snaps_at_end) {
    AnimState<float> a;
    a.start(2.0f, 4.0f, 80.0f, AnimationCurve::Linear);
    float t = -1.0f;
    CHECK_FALSE(a.advance(1.0f, t));    // 1s >> 80ms → done
    CHECK_FALSE(a.active);
    CHECK_FLOAT_EQ(t, 1.0f, 1e-5f);
}

TEST_CASE(anim_state_zero_duration_snaps) {
    AnimState<float> a;
    a.start(0.0f, 1.0f, 0.0f, AnimationCurve::EaseOut);
    CHECK(a.active);                    // start does not special-case 0
    float t = -1.0f;
    CHECK_FALSE(a.advance(0.0f, t));    // duration <= 0 → immediate
    CHECK_FALSE(a.active);
    CHECK_FLOAT_EQ(t, 1.0f, 1e-5f);
}

TEST_CASE(anim_state_restart_midflight) {
    AnimState<float> a;
    a.start(0.0f, 1.0f, 100.0f, AnimationCurve::EaseOut);
    float t = -1.0f;
    CHECK(a.advance(0.03f, t));

    // Mid-flight retarget: start overwrites from the caller's current
    // value (the caller keeps the live value in its own field).
    a.start(0.5f, 1.0f, 50.0f, AnimationCurve::Linear);
    CHECK(a.advance(0.025f, t));        // 0.025/0.05 = 0.5 linear
    CHECK_FLOAT_EQ(t, 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(tweenLerp(a.from, a.to, t), 0.75f, 1e-5f);
}

// UI-anim cut 2: the same AnimState drives FVector2 (popup slide-ins).
TEST_CASE(anim_state_vec2_advance_progresses) {
    AnimState<FVector2> a;
    CHECK_FALSE(a.active);

    a.start(FVector2(0.0f, 0.0f), FVector2(10.0f, -6.0f), 100.0f,
            AnimationCurve::EaseOut);
    CHECK(a.active);

    float t = -1.0f;
    CHECK(a.advance(0.05f, t));          // half way, EaseOut → 0.75
    CHECK_FLOAT_EQ(t, 0.75f, 1e-4f);
    const FVector2 m = tweenLerp(a.from, a.to, t);
    CHECK_FLOAT_EQ(m.x, 7.5f, 1e-4f);
    CHECK_FLOAT_EQ(m.y, -4.5f, 1e-4f);

    CHECK_FALSE(a.advance(0.05f, t));    // done
    CHECK_FALSE(a.active);
    CHECK_FLOAT_EQ(t, 1.0f, 1e-5f);
}

TEST_CASE(anim_state_vec2_snaps_at_end) {
    AnimState<FVector2> a;
    a.start(FVector2(1.0f, 2.0f), FVector2(3.0f, 4.0f), 80.0f,
            AnimationCurve::Linear);
    float t = -1.0f;
    CHECK_FALSE(a.advance(1.0f, t));     // 1s >> 80ms → done
    CHECK_FALSE(a.active);
    CHECK_FLOAT_EQ(t, 1.0f, 1e-5f);
}

// Widget-level: animatePositionTo drives _position through tick.
TEST_CASE(widget_animate_position_to_progresses) {
    Widget w;
    w.setPosition(FVector2(100.0f, 200.0f));
    w.animatePositionTo(FVector2(100.0f, 192.0f), 160.0f,
                        AnimationCurve::EaseOut);
    CHECK(w.isPositionAnimating());

    w.tick(0.08f);                        // half of 160ms
    const float y = w.getPosition().y;
    CHECK(y > 192.0f && y < 200.0f);      // strictly between

    w.tick(0.09f);                        // remainder + completion frame
    CHECK_FALSE(w.isPositionAnimating());
    CHECK_FLOAT_EQ(w.getPosition().y, 192.0f, 1e-4f);
    CHECK_FLOAT_EQ(w.getPosition().x, 100.0f, 1e-4f);
}

TEST_CASE(widget_set_position_cancels_tween) {
    Widget w;
    w.animatePositionTo(FVector2(0.0f, 0.0f), 500.0f, AnimationCurve::Linear);
    CHECK(w.isPositionAnimating());
    w.setPosition(FVector2(42.0f, 42.0f));
    CHECK_FALSE(w.isPositionAnimating());
    CHECK_FLOAT_EQ(w.getPosition().x, 42.0f, 1e-5f);
    // tick must not resurrect the cancelled tween.
    w.tick(1.0f);
    CHECK_FLOAT_EQ(w.getPosition().x, 42.0f, 1e-5f);
}

TEST_CASE(widget_animate_position_to_zero_snaps) {
    Widget w;
    w.setPosition(FVector2(5.0f, 5.0f));
    w.animatePositionTo(FVector2(9.0f, 9.0f), 0.0f, AnimationCurve::EaseOut);
    CHECK_FALSE(w.isPositionAnimating());
    CHECK_FLOAT_EQ(w.getPosition().x, 9.0f, 1e-5f);
}

TEST_CASE(tween_lerp_float_and_vec4) {
    CHECK_FLOAT_EQ(tweenLerp(0.0f, 1.0f, 0.5f), 0.5f, 1e-6f);
    CHECK_FLOAT_EQ(tweenLerp(1.0f, 0.0f, 0.25f), 0.75f, 1e-6f);

    const FVector4 a(0.0f, 0.0f, 0.0f, 0.0f);
    const FVector4 b(1.0f, 1.0f, 1.0f, 1.0f);
    const FVector4 m = tweenLerp(a, b, 0.25f);
    CHECK_FLOAT_EQ(m.x, 0.25f, 1e-6f);
    CHECK_FLOAT_EQ(m.y, 0.25f, 1e-6f);
    CHECK_FLOAT_EQ(m.z, 0.25f, 1e-6f);
    CHECK_FLOAT_EQ(m.w, 0.25f, 1e-6f);
}

TEST_SUITE_END
