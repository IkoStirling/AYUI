#pragma once

#include "AYUI/LeafWidget.h"

namespace ayt::ui {

// UI-anim cut 2 — Spinner: a leaf loading indicator. Four 4x4 rounded
// dots orbit the widget centre on an 6px-radius circle; per-dot alpha
// staircases (0.9/0.55/0.35/0.2) so the group reads as one rotating
// ring. tick() advances a phase accumulator (0..1 wrap, 0.9s per
// revolution) — a plain integral, no tween state needed.
//
// Size: hosts set the widget size (Gallery uses 24x24); the orbit is
// sized relative to the widget so a 16px spinner still works.

class Spinner : public LeafWidget {
public:
    // One full revolution per 0.9s.
    static constexpr float kPeriodSeconds = 0.9f;
    static constexpr float kOrbitRadius = 6.0f;
    static constexpr float kDotSize = 4.0f;

    Spinner();
    ~Spinner() override;

    void tick(float dt) override;

protected:
    void onRender(IRenderBackend& renderer) override;

private:
    float _phase = 0.0f;   // 0..1 wrap, advanced by tick
};

Widget* createSpinnerWidget();

} // namespace ayt::ui
