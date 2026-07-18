#pragma once

// C-7 value-state contract shared by Slider / ProgressBar.
//
// v1 deliberately does NOT promote this to a polymorphic base. With only
// two consumers (Slider : InteractiveWidget, ProgressBar : LeafWidget)
// the polymorphism overhead (a virtual `getValue()` + `setValue()` pair,
// a `getMin()`/`getMax()` pair) would dwarf the saving. The contract
// below is duplicated by both widgets — if a third consumer appears
// (SpinBox, ScrollBar thumb, etc.), promote to AYValueWidget.h base.
//
// Contract:
//   float _min = 0.0f
//   float _max = 1.0f
//   float _value = 0.5f       // clamped to [_min, _max] on every write
//   std::function<void(float)> _onValueChanged   // fires after clamp, only on real change
//
// Widgets implementing this contract:
//   - Slider    (InteractiveWidget subclass — draggable)
//   - ProgressBar (LeafWidget subclass     — display-only)
//
// Public API shared verbatim:
//   setMin(float) / getMin()
//   setMax(float) / getMax()
//   setValue(float) / getValue()
//   setValueRange(float min, float max)   // resets _value to min if new range excludes current
//   setOnValueChanged(std::function<void(float)>)
//   float getNormalized() const           // (value - min) / (max - min), clamped to [0, 1]
//
// Slider also overrides onMouseButtonDown / onMouseMove / onMouseButtonUp
// to drive _value through drag input. ProgressBar does not.
//
// Why this file is header-only + comment-only: documents the contract for
// reviewers without committing to a polymorphic base.

namespace ayt::ui {

inline float clampValueToRange(float value, float minVal, float maxVal) {
    if (maxVal < minVal) maxVal = minVal;
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

} // namespace ayt::ui
