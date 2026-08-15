#pragma once

namespace ayt::ui {

// Uniform (L, T, R, B) padding/margin helper. Replaces ad-hoc FVector4 usages
// across Box/Button/Window so the Editor property panel can reflect a single
// well-typed struct.
struct Thickness {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr Thickness() = default;
    constexpr Thickness(float l, float t, float r, float b)
        : left(l), top(t), right(r), bottom(b) {}

    static constexpr Thickness uniform(float v) { return {v, v, v, v}; }
    static constexpr Thickness symmetric(float h, float v) { return {h, v, h, v}; }
};

} // namespace ayt::ui