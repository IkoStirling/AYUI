#include "AYUI/Spinner.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Style.h"
#include "AYMath/MathUtils.h"

#include <cmath>

namespace ayt::ui {

Spinner::Spinner() {
    setSize(math::FVector2(24.0f, 24.0f));
}

Spinner::~Spinner() = default;

void Spinner::tick(float dt) {
    // Chain the base cascade (opacity/position tweens), then advance the
    // orbit phase.
    Widget::tick(dt);
    const float motionDt = AnimationSettings::get().playbackDelta(
        dt, AnimationImportance::Decorative);
    if (motionDt <= 0.0f) return;
    _phase += motionDt / kPeriodSeconds;
    if (_phase > 1.0f) _phase -= 1.0f;
    // The phase mutates without a setter; invalidate any future cached
    // presentation every animation frame.
    markDirty();
}

void Spinner::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    const float cx = (bounds.minX + bounds.maxX) * 0.5f;
    const float cy = (bounds.minY + bounds.maxY) * 0.5f;
    const float half = kDotSize * 0.5f;

    // Per-dot alpha staircase — the brightest dot leads the rotation, the
    // dimmest trails, so the group reads as a spinning ring even while
    // every dot is on the same orbit.
    const float kAlphas[4] = { 0.9f, 0.55f, 0.35f, 0.2f };
    const float kTwoPi = 6.2831853f;

    for (int i = 0; i < 4; ++i) {
        const float angle = _phase * kTwoPi + static_cast<float>(i) * (kTwoPi * 0.25f);
        const float dx = std::cos(angle) * kOrbitRadius;
        const float dy = std::sin(angle) * kOrbitRadius;
        renderer.drawRoundedRect(
            math::FRectangle(cx + dx - half,
                             cy + dy - half,
                             cx + dx + half,
                             cy + dy + half),
            math::FVector4(resolveAccentColor(kAlphas[i])),
            2.0f);
    }
}

Widget* createSpinnerWidget() {
    return new Spinner();
}

} // namespace ayt::ui
