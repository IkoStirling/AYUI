#include "AYUI/ProgressBar.h"
#include "AYUI/ValueWidget.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Style.h"
#include "AYMath/MathUtils.h"

namespace ayt::ui {

ProgressBar::ProgressBar() {
    setSize(math::FVector2(200.0f, 16.0f));
}

ProgressBar::~ProgressBar() = default;

void ProgressBar::setMin(float v) {
    if (v > _max - kMinMaxEpsilon) {
        v = _max - kMinMaxEpsilon;
    }
    if (fabsf(v - _min) < kMinMaxEpsilon) {
        return;
    }
    _min = v;
    setValue(_value);
}

void ProgressBar::setMax(float v) {
    if (v < _min + kMinMaxEpsilon) {
        v = _min + kMinMaxEpsilon;
    }
    if (fabsf(v - _max) < kMinMaxEpsilon) {
        return;
    }
    _max = v;
    setValue(_value);
}

void ProgressBar::setValueRange(float minVal, float maxVal) {
    if (maxVal < minVal + kMinMaxEpsilon) {
        maxVal = minVal + kMinMaxEpsilon;
    }
    _min = minVal;
    _max = maxVal;
    if (_value < _min) {
        setValue(_min);
    } else if (_value > _max) {
        setValue(_max);
    }
}

void ProgressBar::setValue(float v) {
    const float clamped = clampValueToRange(v, _min, _max);
    if (fabsf(clamped - _value) < kMinMaxEpsilon) {
        return;
    }
    _value = clamped;
    if (_onValueChanged) {
        _onValueChanged(_value);
    }
}

float ProgressBar::getNormalized() const {
    return clampValueToRange((_value - _min) / (_max - _min), 0.0f, 1.0f);
}

math::FRectangle ProgressBar::getBarRect() const {
    math::FRectangle bounds = getWorldBounds();
    const float cy = (bounds.minY + bounds.maxY) * 0.5f;
    return math::FRectangle(
        bounds.minX,
        cy - kBarHeight * 0.5f,
        bounds.maxX,
        cy + kBarHeight * 0.5f);
}

math::FRectangle ProgressBar::getFilledRect() const {
    math::FRectangle bar = getBarRect();
    const float nx = getNormalized();
    bar.maxX = bar.minX + nx * (bar.maxX - bar.minX);
    return bar;
}

void ProgressBar::tick(float dt) {
    // Chain the base cascade (opacity/position tweens), then advance the
    // indeterminate scan phase when enabled.
    Widget::tick(dt);
    if (_indeterminate) {
        _scanPhase += dt / 1.6f;   // one full sweep per 1.6s
        if (_scanPhase > 1.0f) _scanPhase -= 1.0f;
    }
}

void ProgressBar::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    const math::FRectangle bar = getBarRect();
    const math::FRectangle filled = getFilledRect();

    // resolveStyle() pattern — same theme hooks as the rest of AYUI.
    const ResolvedStyle style = resolveStyle(getStyleId());
    math::FVector4 unfilledBg = math::FVector4(0.18f, 0.18f, 0.20f, 1.0f);
    math::FVector4 unfilledBorder = math::FVector4(0.4f, 0.4f, 0.45f, 1.0f);
    float unfilledBorderWidth = 1.0f;
    if (style.hasStyle) {
        unfilledBg = style.backgroundColor;
        unfilledBorder = style.borderColor;
        unfilledBorderWidth = style.borderWidth;
    }

    // B3: rounded fills match the 2px rounded border (bar keeps its
    // rounded cap at the fill edge, which reads as a progress bar).
    renderer.drawRoundedRect(bar, unfilledBg, 2.0f);

    // UI-anim cut 2: indeterminate mode — a 30%-wide accent segment scans
    // left→right across the track, clipped to the bar. The value-fill path
    // below stays byte-identical when disabled.
    if (_indeterminate) {
        const float barW = bar.maxX - bar.minX;
        const float segW = std::max(barW * 0.30f, 4.0f);
        const float x0 = bar.minX - segW + _scanPhase * (barW + segW);
        renderer.pushClip(bar);
        renderer.drawRoundedRect(
            math::FRectangle(x0, bar.minY, x0 + segW, bar.maxY),
            math::FVector4(0.18f, 0.45f, 0.78f, 1.0f), 2.0f);
        renderer.popClip();
        renderer.drawBorderRect(bar, unfilledBorder, unfilledBorderWidth, 2.0f);
        return;
    }

    // Filled portion — clamp to >= 1px width when normalized > 0 so a
    // tiny non-zero value still shows a visible bar.
    if (filled.maxX > filled.minX + 0.5f) {
        renderer.drawRoundedRect(filled,
            math::FVector4(0.18f, 0.45f, 0.78f, 1.0f), 2.0f);
    }
    renderer.drawBorderRect(bar, unfilledBorder, unfilledBorderWidth, 2.0f);

    // Optional value text overlay — only when the widget is tall enough
    // and the host has called setText (optional helper for "loading 75%"
    // overlays). For v1 we keep the helper opt-in; the widget doesn't
    // show a label by default to keep render output minimal.
}

Widget* createProgressBarWidget() {
    return new ProgressBar();
}

} // namespace ayt::ui
