#include "AYUI/Panel.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Style.h"

namespace ayt::ui {

Panel::Panel() {
    // Sensible default size — matches Window's default scale and gives a
    // visible region without forcing every JSON to specify size. Layout
    // JSON that does specify size will overwrite this.
    setSize(math::FVector2(200.0f, 150.0f));
}

Panel::~Panel() {
}

void Panel::setPadding(float left, float top, float right, float bottom) {
    _padding = math::FVector4(left, top, right, bottom);
}

void Panel::onRender(IRenderBackend& renderer) {
    if (!_backgroundEnabled) {
        return;
    }
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    // R-5: style resolution is centralized in resolveStyle() (see AYStyle.h).
    // When hasStyle is true the resolved colors drive the draw; otherwise the
    // hardcoded neutral grey fallback wins. Without a wired StyleSheet the
    // StyleManager lookup returns nullptr and resolveStyle reports hasStyle
    // = false — same contract Button uses.
    const ResolvedStyle style = resolveStyle(getStyleId());

    math::FVector4 bg;
    math::FVector4 borderColor;
    float borderWidth;
    if (style.hasStyle) {
        bg = style.backgroundColor;
        borderColor = style.borderColor;
        borderWidth = style.borderWidth;
    } else {
        bg = math::FVector4(0.20f, 0.20f, 0.20f, 1.0f);
        borderColor = math::FVector4(0.30f, 0.30f, 0.30f, 1.0f);
        borderWidth = 1.0f;
    }

    renderer.drawRect(bounds, bg);

    if (_borderEnabled && borderWidth > 0.0f) {
        renderer.drawBorderRect(bounds, borderColor, borderWidth, 0.0f);
    }
}

} // namespace ayt::ui