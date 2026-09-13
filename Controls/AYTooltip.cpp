#include "AYUI/Tooltip.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/UIManager.h"
#include <algorithm>
#include <cwctype>

namespace ayt::ui {

namespace {

float tooltipTextAdvanceUnits(const std::wstring& text) {
    float units = 0.0f;
    for (wchar_t ch : text) {
        if (ch == L'\n' || ch == L'\r') continue;
        // Latin UI text is typically about half an em wide, while CJK
        // ideographs and most non-ASCII symbols occupy a full em. Treating
        // every code point as Latin clipped localized tooltips such as
        // "下移当前层" even though the English source fitted correctly.
        units += ch <= 0x7fu ? 0.55f : 1.0f;
    }
    return units;
}

} // namespace

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
    // Code-review 2026-08-02 #7: clear _target back-pointer so any
    // post-destruction path that checks getTarget() observes null.
    //
    // PR-C1 — also unregister from UIManager's hover-timer driver so
    // the next update() doesn't deref this dead pointer. After
    // shutdown() the active manager is nullptr (tryGet returns null)
    // so this is a silent no-op in the orderly path; the standalone
    // `delete tip` path (e.g. `Tooltip* tip = attachTo(...); delete tip;`)
    // is what this guards against.
    //
    // P3: the overlay parent does NOT destroy us (addChildExternal in
    // attachTo), so ~Widget leaving children intact is fine — the host
    // owns lifetime and is responsible for calling detach() first.
    _target = nullptr;
    if (UIManager* ui = UIManager::tryGet()) {
        ui->unregisterTooltip(this);
    }
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
    // P3 fix (Gallery exit-crash 0xFFFFFFFFFFFFFFFF): mount via
    // addChildExternal, NOT openPopup. openPopup sets _activeDropdown=tip,
    // which makes the click-outside detector in onMouseButtonUp call
    // closePopup(_activeDropdown, destroy=true) the moment the user clicks
    // anywhere outside the anchor button — that destroyWidgetTree's the
    // tooltip and leaves the host's raw pointer dangling. Mounting as a
    // plain overlay sibling keeps the tip alive until the host explicitly
    // destroys it (matches the documented "caller owns the Tooltip*"
    // contract on the public header).
    if (Widget* overlay = ui.getOverlayRoot()) {
        overlay->addChildExternal(tip);
    }
    // PR-C1 — register AFTER attaching to the overlay so the hover-timer
    // driver sees the tooltip mounted before it starts ticking. update()
    // snapshots _tooltips before iterating, so a late register is safe
    // (next frame picks it up). Order doesn't matter for correctness.
    // before the driver starts ticking. update() snapshots _tooltips
    // before iterating, so a late register is safe (next frame picks
    // it up). Order doesn't matter for correctness — both calls are
    // independent — but the openPopup-first ordering matches the
    // destroy-order contract in tearDownOverlayChildren (clear list
    // before destroyWidgetTree), which keeps the two paths symmetric.
    ui.registerTooltip(tip);
    tip->setVisible(false);
    return tip;
}

void Tooltip::detach() {
    // Code-review 2026-08-02 #7: tooltip is mounted on the overlay, so
    // target destruction doesn't reach us through destroyWidgetTree. Null
    // _target so tick() early-returns on the next call instead of
    // dereferencing a freed anchor. Also pull ourselves off the overlay
    // so the manager doesn't keep ticking / rendering a stranded popup.
    //
    // PR-C1 — unregister from the hover-timer driver BEFORE pulling
    // ourselves off the overlay. The driver's update() loop snapshots
    // _tooltips but the snapshot doesn't survive the unregister call,
    // so unregister-first guarantees no concurrent deref. closePopup's
    // own Tooltip check would also unregister, but doing it here keeps
    // the contract symmetric with ~Tooltip.
    _target = nullptr;
    if (UIManager* ui = UIManager::tryGet()) {
        ui->unregisterTooltip(this);
    }
    // P3 fix: removeChild directly from the overlay. detach() must NOT go
    // through closePopup — the tooltip is no longer registered as
    // _activeDropdown (attachTo bypassed openPopup), so closePopup's
    // _activeDropdown / hover / capture guards would silently skip the
    // bookkeeping we already did and leave the overlay still holding the
    // tip. Direct removeChild is the matching pair of the addChildExternal
    // we did in attachTo.
    if (Widget* p = getParent()) {
        p->removeChild(this);
    }
    hide();
}

void Tooltip::ensureLabelCreated() {
    if (_label != nullptr) return;
    _label = new TextLabel();
    addChild(_label);   // owning
}

void Tooltip::setText(const std::wstring& text) {
    ensureLabelCreated();
    _label->setText(text);
    // AYUI-DirtyRect-2026-08-26: the label re-marks dirty on its own, but
    // the tooltip's own plate (drawn by onRender) needs to repaint
    // because the bounds may change with the new text width. markDirty
    // propagates up to the overlay root so the popup refreshes.
    markDirty();
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
    // Code-review 2026-08-02 #7: tooltip lifetime is independent of target
    // (mounted on overlay). When target dies first, _target dangles.
    // detach() clears it for the orderly path; this guard catches the
    // case where the host forgot to detach() and the target was destroyed
    // out from under us. Hides any in-flight tip + clears hover state.
    if (_target == nullptr) {
        if (_hovering || _visible) {
            _hovering = false;
            _hoverTime = 0.0f;
            hide();
        }
        return;
    }
    const math::FRectangle tBounds = _target->getWorldBounds();

    // Tooltip follows target visibility. When the target's page is
    // hidden (e.g. user switches from Capabilities -> Input via nav),
    // the target's world bounds may STILL geometrically intersect the
    // mouse cursor if the bounds were last computed when the page was
    // visible — setVisible(false) does NOT move children, it only
    // hides them. Without this gate, the tooltip could trigger over a
    // hidden ancestor whose geometry overlaps the current mouse pos.
    // Gallery-reported: hover TextInput on Input page fired the
    // Capabilities C1 tooltip because page_basics...page_capabilities
    // are siblings inside the same ScrollView; positions are stacked
    // vertically so a hidden page's geometry doesn't intersect the
    // visible viewport — but ANY host layout where hidden ancestors
    // span the visible area (scrolled-out pages, dialog-stacked
    // overlays) would trip this. Treat invisible targets as "not inside".
    const bool inside =
        _target->isVisible() && tBounds.contains(mousePos);
    if (inside) {
        if (!_hovering) {
            _hovering = true;
            _hoverTime = 0.0f;
        }
        _hoverTime += dt;
        if (!_visible && _hoverTime >= _hoverDelay) {
            show();
        }
        if (_visible && !isPositionAnimating()) {
            // Keep position synced in case the target moves or the
            // viewport changed while hovering. Skipped while the
            // slide-in tween runs (setPosition would cancel it).
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
    _hiding = false;
    setVisible(true);
    syncPosition();
    // UI animation lane: pop-in fade (120ms EaseOut).
    setOpacity(0.0f);
    animateOpacity(1.0f, 120.0f, AnimationCurve::EaseOut);
    // UI-anim cut 2: slide in from 8px above the sync'd position. The
    // hover tick re-syncs position every frame — guarded by
    // !isPositionAnimating() there so the slide isn't killed on frame 1.
    const math::FVector2 p = getPosition();
    setPosition(p + math::FVector2(0.0f, -8.0f));
    animatePositionTo(p, 120.0f, AnimationCurve::EaseOut);
    // AYUI-DirtyRect-2026-08-26: becoming visible mid-frame; setVisible
    // already markDirty'd, but the multi-property mutation above
    // (opacity, position, size) can race with render ordering — mark
    // once more to guarantee the next frame redraws.
    markDirty();
}

void Tooltip::hide() {
    _visible = false;
    if (_hiding) {
        return;
    }
    // UI animation lane: fade out instead of vanishing. The tooltip stays
    // visible (rendering the fade) while _hiding is set; tick(float) flips
    // setVisible(false) when the fade completes. Fade-out is skipped when
    // there is no live UIManager to drive the tick — hide stays instant
    // in teardown paths.
    if (isVisible() && getOpacity() > 0.01f && UIManager::tryGet() != nullptr) {
        animateOpacity(0.0f, 100.0f, AnimationCurve::EaseIn);
        _hiding = true;
        return;
    }
    setVisible(false);
}

void Tooltip::tick(float dt) {
    Widget::tick(dt);
    // Fade-out completion — driven by the overlay cascade, so it keeps
    // running even when the hover driver (3-arg tick) has paused.
    if (_hiding && !isOpacityAnimating() && getOpacity() <= 0.01f) {
        _hiding = false;
        setVisible(false);
    }
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
        const float textW = std::max(
            fontPx,
            fontPx * tooltipTextAdvanceUnits(_label->getText()));
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
    // B1: rounded plate + one drawBorderRect replaces the old 4-strip
    // border (4 draw calls → 1 SDF item). Radius 3px matches the Menu
    // dialog look; the plate must be rounded too, or background corners
    // poke out beyond the border ring.
    constexpr float kRadius = 3.0f;
    // Background plate with slight alpha — matches Menu palette so the
    // tooltip reads as a dialog-style frame, not a bare TextLabel slab.
    renderer.drawRoundedRect(b, math::FVector4(0.13f, 0.14f, 0.17f, 0.96f), kRadius);
    // 4-sided border, 1px wide, matches Menu::onRender for visual parity
    // with the dropdown popups. Previous code only drew top+bottom strips,
    // which left the tooltip looking like a horizontal band instead of a
    // wrapped dialog box.
    renderer.drawBorderRect(b, math::FVector4(0.45f, 0.45f, 0.50f, 1.0f), 1.0f, kRadius);
}

Widget* createTooltipWidget() { return new Tooltip(); }

} // namespace ayt::ui
