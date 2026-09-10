#pragma once

// Lightweight tween pipeline (UI animation lane, cut 1). Shared by
// Widget::animateOpacity and InteractiveWidget color transitions:
// a small AnimState<T> that a host starts (start) and a tick driver
// advances (advance). The *current* value lives with the caller
// (Widget::_opacity / InteractiveWidget::_color), not in the state,
// so a widget pays for the anim fields only while one is running.

#include "AYUI/Animation.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

// Easing table for widget tweens. Mirrors MockRenderer's
// AnimationData::update switch (AYMockRenderer.cpp) so a Widget-side
// tween and a renderer-side animation handle interpolate identically.
// Test_Tween pins both sides point-equal — do not change one without
// the other.
inline float easeCurve(float t, AnimationCurve curve)
{
    switch (curve) {
    case AnimationCurve::EaseIn:
        return t * t;
    case AnimationCurve::EaseOut:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case AnimationCurve::EaseInOut:
        return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    case AnimationCurve::Spring:
        // Simplified spring — same formula as MockRenderer.
        return t + std::sin(t * 6.28f) * 0.1f * (1.0f - t);
    case AnimationCurve::CubicBezier:
        return evaluateCubicBezier(t, CubicBezierParameters{});
    case AnimationCurve::Linear:
    default:
        return t;
    }
}

// Generic tween state for a value type T (float, math::FVector4, ...).
// Works with any T supporting + - * (scalar). T defaults to {} — for
// FVector4 that is the zero vector, which callers must replace via
// snap/start before first use.
template <typename T>
struct AnimState {
    T from{};
    T to{};
    float elapsed = 0.0f;
    float duration = 0.0f;                    // seconds
    AnimationCurve curve = AnimationCurve::EaseOut;
    AnimationImportance importance = AnimationImportance::Decorative;
    bool active = false;
    bool paused = false;

    // Starts a tween from `f` to `t` over ms milliseconds. Overwrites
    // any in-flight tween (from is the caller's current value).
    void start(const T& f, const T& t, float durationMs, AnimationCurve c,
               AnimationImportance animationImportance =
                   AnimationImportance::Decorative)
    {
        from = f;
        to = t;
        elapsed = 0.0f;
        duration = durationMs * 0.001f;       // ms -> s
        curve = c;
        importance = animationImportance;
        active = true;
        paused = false;
    }

    // Jumps to v and cancels the tween.
    void snap(const T& v)
    {
        from = v;
        to = v;
        elapsed = 0.0f;
        duration = 0.0f;
        active = false;
        paused = false;
    }

    void pause() { if (active) paused = true; }
    void resume() { paused = false; }

    // Advances by dt seconds. Returns true while still running (outT is
    // the eased 0..1 factor); on completion returns false with outT = 1.0.
    bool advance(float dt, float& outT)
    {
        if (!active) {
            outT = 1.0f;
            return false;
        }
        if (paused) {
            const float linear = duration > 0.0f
                ? std::min(1.0f, elapsed / duration) : 1.0f;
            outT = easeCurve(linear, curve);
            return true;
        }
        if (!AnimationSettings::get().shouldAnimate(importance)) {
            elapsed = duration;
        } else {
            elapsed += AnimationSettings::get().playbackDelta(dt, importance);
        }
        // Frame deltas such as 0.04 + 0.06 are not guaranteed to represent
        // the authored 0.1 seconds exactly. Treat a sub-microsecond remainder
        // as complete so callbacks fire on the intended boundary frame.
        if (duration <= 0.0f || elapsed >= duration - 1e-6f) {
            active = false;
            outT = 1.0f;
            return false;
        }
        outT = easeCurve(elapsed / duration, curve);
        return true;
    }
};

// Component-wise lerp for tween values. Works for float and FVector4
// (which has scalar multiplication).
template <typename T>
inline T tweenLerp(const T& a, const T& b, float t)
{
    return a + (b - a) * t;
}

} // namespace ayt::ui
