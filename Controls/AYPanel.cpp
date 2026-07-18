#include "AYPanel.h"
#include "AYIRenderBackend.h"
#include "AYStyle.h"

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
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    // Style lookup mirrors Button::onRender (R-5 contract). If no
    // StyleSheet is registered the manager returns nullptr and we use the
    // hardcoded neutral grey fallback — same pattern as Button so existing
    // tests that don't wire StyleManager keep passing.
    math::FVector4 bg(0.20f, 0.20f, 0.20f, 1.0f);
    math::FVector4 borderColor(0.30f, 0.30f, 0.30f, 1.0f);
    float borderWidth = 1.0f;
    bool useStyle = false;
    if (!getStyleId().empty()) {
        if (const WidgetStyle* s = StyleManager::get().getStyle(getStyleId())) {
            // StyleBuilder::makeDefault() background is (0.2, 0.2, 0.2, 1).
            // If the resolved style matches makeDefault we treat it as "no
            // explicit background" and fall back to the hardcoded fills.
            // This keeps the visual consistent whether or not a stylesheet
            // is wired in.
            const WidgetStyle def = StyleBuilder::makeDefault();
            const bool bgIsDefault = (s->backgroundColor.x == def.backgroundColor.x &&
                                       s->backgroundColor.y == def.backgroundColor.y &&
                                       s->backgroundColor.z == def.backgroundColor.z &&
                                       s->backgroundColor.w == def.backgroundColor.w);
            if (!bgIsDefault) {
                bg = s->backgroundColor;
                borderColor = s->border.color;
                borderWidth = s->border.width;
                useStyle = true;
            }
        }
    }

    renderer.drawRect(bounds, bg);

    if (_borderEnabled && borderWidth > 0.0f) {
        renderer.drawBorderRect(bounds, borderColor, borderWidth, 0.0f);
    }
}

} // namespace ayt::ui