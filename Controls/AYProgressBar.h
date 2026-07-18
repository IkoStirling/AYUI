#pragma once

#include "AYLeafWidget.h"
#include <functional>

namespace ayt::ui {

// C-7 ProgressBar. Display-only value widget — shows `_value` mapped
// into [_min, _max] as a horizontal fill. Not interactive (extends
// LeafWidget, default no-op mouse handlers). The same value contract
// as Slider (see AYValueWidget.h) is duplicated here verbatim; promote
// to a polymorphic base when a third consumer appears.
//
// Use cases: loading progress, health bars, throughput meters. To "set
// the bar to 75%" the host calls setValue(0.75f) — no animation, no
// drag — every render reflects the most recent setValue.

class ProgressBar : public LeafWidget {
public:
    static constexpr float kBarHeight = 6.0f;
    static constexpr float kMinMaxEpsilon = 1e-5f;

    ProgressBar();
    ~ProgressBar() override;

    void  setMin(float v);
    float getMin() const { return _min; }

    void  setMax(float v);
    float getMax() const { return _max; }

    void setValueRange(float minVal, float maxVal);

    void  setValue(float v);
    float getValue() const { return _value; }

    float getNormalized() const;

    void setOnValueChanged(std::function<void(float)> cb) {
        _onValueChanged = std::move(cb);
    }

    math::FRectangle getBarRect() const;
    math::FRectangle getFilledRect() const;

protected:
    void onRender(IRenderBackend& renderer) override;

    float _min = 0.0f;
    float _max = 1.0f;
    float _value = 0.5f;
    std::function<void(float)> _onValueChanged;
};

Widget* createProgressBarWidget();

} // namespace ayt::ui
