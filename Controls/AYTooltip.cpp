#include "AYTooltip.h"
#include "IAYRenderBackend.h"
#include "AYUIManager.h"
#include <algorithm>

namespace ayt::ui {

Tooltip::Tooltip() {
    setSize(math::FVector2(0.0f, 0.0f));   // recomputed on layout
    setVisible(false);
    ensureLabelCreated();
}

// DECISION 3: tooltip does not steal mouse input. Override hitTest so
// pointer events pass through to the underlying target.
Widget* Tooltip::hitTest(const math::FVector2& /*worldPos*/) {
    return nullptr;
}

Tooltip::~Tooltip() {
    // CompoundWidget's destructor handles child cleanup.
}

Tooltip* Tooltip::attachTo(Widget* target) {
    if (target == nullptr) return nullptr;
    Tooltip* tip = new Tooltip();
    // Phase A (A2): mount the tooltip on UIManager's overlay root instead
    // of as an owning child of `target`. The tooltip's lifetime becomes
    // independent of the target — the caller is expected to call
    // Tooltip::show / tick / hide, and UIManager::closePopup on overlay
    // teardown. The tooltip is hidden by default and only becomes visible
    // after tick() accumulates hover time past the delay (DECISION 1).
    //
    // The tooltip pointer returned remains owned by the caller. To detach,
    // call closePopup via UIManager with the tip pointer. The tick()
    // signature keeps the explicit viewport so callers can override
    // the UIManager viewport (e.g. tests).
    // Phase D §5.3 — set _target BEFORE any UIManager call so that
    // tooltip_initial_state tests (which construct a Tooltip without an
    // active UIManager) still getTarget() == the attaching widget.
    tip->_target = target;
    UIManager* uiPtr = UIManager::tryGet();
    if (uiPtr == nullptr) return tip;   // tooltip usable without overlay
    UIManager& ui = *uiPtr;
    ui.openPopup(target, tip);
    tip->setVisible(false);
    return tip;
}

void Tooltip::ensureLabelCreated() {
    if (_label != nullptr) return;
    _label = new TextLabel();
    addChild(_label);   // owning
}

void Tooltip::setText(const std::wstring& text) {
    ensureLabelCreated();
    _label->setText(text);
}

const std::wstring& Tooltip::getText() const {
    static const std::wstring kEmpty;
    if (_label == nullptr) return kEmpty;
    return _label->getText();
}

void Tooltip::tick(float dt, const math::FVector2& mousePos,
                   const math::FVector2& viewportSize) {
    // Phase A (A2): viewport fallback. If the caller passes (0,0) (the
    // default sentinel for "unset"), pull live metrics from UIManager so
    // the flip-above heuristic stays correct in production. Tests that
    // want a fixed viewport still pass an explicit size.
    if (viewportSize.x <= 0.0f && viewportSize.y <= 0.0f) {
        if (UIManager* ui = UIManager::tryGet()) {
            _viewportSize = ui->getClientSize();
        } else {
            return;   // no active manager — skip tick
        }
    } else {
        _viewportSize = viewportSize;
    }
    if (_target == nullptr) return;
    const math::FRectangle tBounds = _target->getWorldBounds();

    const bool inside = tBounds.contains(mousePos);
    if (inside) {
        if (!_hovering) {
            _hovering = true;
            _hoverTime = 0.0f;
        }
        _hoverTime += dt;
        if (!_visible && _hoverTime >= _hoverDelay) {
            show();
        }
        if (_visible) {
            // Keep position synced in case the target moves or the
            // viewport changed while hovering.
            syncPosition();
        }
    } else {
        if (_hovering) {
            _hovering = false;
            _hoverTime = 0.0f;
            hide();
        }
    }
}

void Tooltip::show() {
    _visible = true;
    setVisible(true);
    syncPosition();
}

void Tooltip::hide() {
    _visible = false;
    setVisible(false);
}

void Tooltip::syncPosition() {
    if (_target == nullptr) return;
    const math::FRectangle tBounds = _target->getWorldBounds();
    // Force a layout pass to compute label size.
    performLayout();
    const math::FVector2 tipSize = getSize();
    // Default: below target.
    math::FVector2 pos(tBounds.minX, tBounds.maxY + kTipOffsetY);
    // Flip above if it would overflow the viewport bottom.
    if (_viewportSize.y > 0.0f &&
        pos.y + tipSize.y > _viewportSize.y) {
        pos.y = tBounds.minY - tipSize.y - kTipOffsetY;
        if (pos.y < 0.0f) pos.y = 0.0f;
    }
    setPosition(pos);
}

void Tooltip::performLayout() {
    if (_label != nullptr) {
        // TextLabel does not auto-measure glyphs (no text shaper in v1).
        // Size the label from an approximate advance so flip/clamp math
        // in syncPosition() sees a real tip height instead of the
        // Widget default 100x50 — which made near-bottom tips fail to
        // flip above the anchor in tests / tiny viewports.
        const float fontPx = static_cast<float>(std::max(1, _label->getFontSize()));
        const float approxCharW = fontPx * 0.55f;
        const float textW = std::max(
            fontPx,
            approxCharW * static_cast<float>(_label->getText().size()));
        const float textH = fontPx + 2.0f;
        _label->setSize(math::FVector2(textW, textH));
    }
    CompoundWidget::performLayout();
    if (_label == nullptr) return;
    const math::FVector2 labelPad(kDefaultPadding * 2.0f, kDefaultPadding * 2.0f);
    const math::FVector2 labelSize = _label->getSize();
    setSize(math::FVector2(labelSize.x + labelPad.x,
                            labelSize.y + labelPad.y));
    _label->setPosition(math::FVector2(kDefaultPadding, kDefaultPadding));
}

void Tooltip::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;
    // Background plate with slight alpha.
    renderer.drawRect(b, math::FVector4(0.10f, 0.10f, 0.13f, 0.92f));
    // Subtle border in lighter shade.
    renderer.drawRect(
        math::FRectangle(b.minX, b.minY, b.maxX, b.minY + 1.0f),
        math::FVector4(0.45f, 0.45f, 0.50f, 1.0f));
    renderer.drawRect(
        math::FRectangle(b.minX, b.maxY - 1.0f, b.maxX, b.maxY),
        math::FVector4(0.45f, 0.45f, 0.50f, 1.0f));
}

Widget* createTooltipWidget() { return new Tooltip(); }

} // namespace ayt::ui
