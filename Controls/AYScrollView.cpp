#include "AYScrollView.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"
#include "AYScrollBarSync.h"
#include "aymath/MathUtils.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

ScrollView::ScrollView() {
    ensureBarsCreated();
}

ScrollView::~ScrollView() {
    // Bars were added via addChildExternal (host-lifetime semantics so
    // TextArea / ListView can hold a ScrollView without re-owning its
    // bars). But when *we* are destroyed, destroyWidgetTree SKIPS
    // externally-owned children — which would leak both ScrollBars.
    // Mirror the rebuildRows() teardown pattern: detach + delete.
    //
    // _content is host-owned (caller passes a Widget* whose lifetime it
    // manages), so do NOT free it here.
    if (_vbar != nullptr) {
        if (_vbar->getParent() == this) {
            removeChild(_vbar);
        }
        delete _vbar;
        _vbar = nullptr;
    }
    if (_hbar != nullptr) {
        if (_hbar->getParent() == this) {
            removeChild(_hbar);
        }
        delete _hbar;
        _hbar = nullptr;
    }
}

void ScrollView::setContent(Widget* content) {
    if (_content == content) return;
    if (_content != nullptr) {
        removeChild(_content);
    }
    _content = content;
    if (_content != nullptr) {
        addChildExternal(_content);
        if (_vbar == nullptr && _vbarEnabled) {
            ensureBarsCreated();
        }
        syncBarsToOffset();
    }
}

void ScrollView::setContentSize(const math::FVector2& size) {
    _scrollState.setContentSize(size);
    syncBarsToOffset();
}

void ScrollView::ensureBarsCreated() {
    if (_vbarEnabled && _vbar == nullptr) {
        _vbar = new ScrollBar();
        _vbar->setOrientation(ScrollBar::Orientation::Vertical);
        _vbar->setOnValueChanged([this](float v) {
            // PR-Container-Shared-Contract: route through ScrollableWidget::scrollBy
            // for the canonical clamp. Preserves ScrollView's behaviour: syncContent
            // position + fire _onScroll whenever the offset actually moved.
            const math::FVector2 vp = getViewportSize();
            const math::FVector2 delta(
                0.0f,
                v - _scrollState.getScrollOffset().y);
            if (_scrollState.scrollBy(delta, vp)) {
                syncContentPosition();
                if (_onScroll) _onScroll(_scrollState.getScrollOffset());
            }
        });
        addChildExternal(_vbar);
    }
    if (_hbarEnabled && _hbar == nullptr) {
        _hbar = new ScrollBar();
        _hbar->setOrientation(ScrollBar::Orientation::Horizontal);
        _hbar->setOnValueChanged([this](float v) {
            const math::FVector2 vp = getViewportSize();
            const math::FVector2 delta(
                v - _scrollState.getScrollOffset().x,
                0.0f);
            if (_scrollState.scrollBy(delta, vp)) {
                syncContentPosition();
                if (_onScroll) _onScroll(_scrollState.getScrollOffset());
            }
        });
        addChildExternal(_hbar);
    }
}

math::FVector2 ScrollView::getViewportSize() const {
    return math::FVector2(getWidth(), getHeight());
}

void ScrollView::syncContentPosition() {
    if (_content == nullptr) return;
    // Pixel-snap: scroll state is already rounded in ScrollableWidget,
    // but belt-and-suspenders so layout never parks content on a
    // half-pixel (1px SDF borders shimmer under fractional offsets).
    const math::FVector2 off = _scrollState.getScrollOffset();
    _content->setPosition(math::FVector2(-std::round(off.x), -std::round(off.y)));
}

// PR-Container-Contract-Cut2: getClientRect is the single source of truth
// for both render clip and hit-test gate. Same math as the legacy
// contentClipRect() helper (kept below as a thin alias so call sites that
// want to be explicit can still use it).
math::FRectangle ScrollView::getClientRect() const {
    math::FRectangle clip = getWorldBounds();
    // Inset by the frame stroke so scrolled children (e.g. ListView)
    // cannot paint over the border pixels — Gallery "page edge covered
    // by list" was getClientRect == full bounds while border was drawn
    // underneath content.
    constexpr float kBorder = 1.0f;
    clip.minX += kBorder;
    clip.minY += kBorder;
    clip.maxX -= kBorder;
    clip.maxY -= kBorder;
    if (clip.maxX < clip.minX) clip.maxX = clip.minX;
    if (clip.maxY < clip.minY) clip.maxY = clip.minY;

    const float barW = ScrollBar::kDefaultBarWidth;
    if (_vbar != nullptr && _vbar->isVisible()) {
        clip.maxX = std::max(clip.minX, clip.maxX - barW);
    }
    if (_hbar != nullptr && _hbar->isVisible()) {
        clip.maxY = std::max(clip.minY, clip.maxY - barW);
    }
    return clip;
}

math::FRectangle ScrollView::contentClipRect() const {
    // Legacy alias — kept so call sites that read this name still compile.
    return getClientRect();
}

void ScrollView::syncBarsToOffset() {
    const math::FVector2 vp = getViewportSize();
    const math::FVector2 content = _scrollState.getContentSize();
    // PR-SyncVerticalBar: direction-agnostic helper. ScrollView is the
    // only H consumer — passing .x keeps the contract identical.
    syncVerticalBar(_vbar, content.y, vp.y, _scrollState.getScrollOffset().y);
    syncVerticalBar(_hbar, content.x, vp.x, _scrollState.getScrollOffset().x);
}

bool ScrollView::scrollBy(const math::FVector2& delta) {
    const bool changed = _scrollState.scrollBy(delta, getViewportSize());
    if (changed) {
        syncContentPosition();
        syncBarsToOffset();
        if (_onScroll) _onScroll(_scrollState.getScrollOffset());
    }
    return changed;
}

// PR-B3 — wheel handler. scrollBy accepts (dx, dy) where positive dy
// moves the scroll offset DOWN — i.e. reveals more content below the
// cursor (content moves up out of the viewport). Wheel conventions on
// every desktop OS are the same: positive deltaY = "user rolled the
// wheel away from them" = content moves UP = scrollOffset INCREASES.
// So we pass deltaY through unchanged here.
bool ScrollView::onMouseWheel(const UIMouseWheelEvent& e) {
    return scrollBy(math::FVector2(0.0f, e.deltaY));
}

void ScrollView::performLayout() {
    // Default size — sensible default 200x150 if not set by host.
    if (getWidth() <= 0.0f) {
        setSize(math::FVector2(200.0f, 150.0f));
    }
    if (_vbarEnabled) ensureBarsCreated();
    if (_hbarEnabled) ensureBarsCreated();

    const float barW = ScrollBar::kDefaultBarWidth;
    if (_vbar != nullptr) {
        _vbar->setPosition(math::FVector2(
            getWidth() - barW, 0.0f));
        _vbar->setSize(math::FVector2(barW, getHeight()));
    }
    if (_hbar != nullptr) {
        _hbar->setPosition(math::FVector2(
            0.0f, getHeight() - barW));
        _hbar->setSize(math::FVector2(
            getWidth() - (_vbar ? barW : 0.0f), barW));
    }

    auto clientSize = [this]() -> math::FVector2 {
        const math::FRectangle c = getClientRect();
        return math::FVector2(
            std::max(0.0f, c.maxX - c.minX),
            std::max(0.0f, c.maxY - c.minY));
    };

    // Vertical-scroll hosts (Gallery content_scroll: hbar off) size the
    // content widget to the client width / preferred height so hit-test
    // bounds match paint. Horizontal overflow hosts (ToolBar) size their
    // content strip themselves — forcing viewport size first would squash
    // natural width and hide the hbar.
    const bool sizeContentToClient = (_content != nullptr)
        && _content->isLayoutSizeManaged()
        && !_hbarEnabled;

    if (sizeContentToClient) {
        const math::FVector2 cs = clientSize();
        const math::FVector2 probe(
            cs.x > 0.0f ? cs.x : std::max(getWidth(), 1.0f),
            cs.y > 0.0f ? cs.y : std::max(getHeight(), 1.0f));
        if (std::fabs(_content->getSize().x - probe.x) > 0.5f
            || std::fabs(_content->getSize().y - probe.y) > 0.5f) {
            _content->setSize(probe);
        }
    }

    CompoundWidget::performLayout();

    if (_content != nullptr) {
        if (sizeContentToClient) {
            auto fitContent = [&]() {
                const math::FVector2 cs = clientSize();
                const math::FVector2 pref = _content->getPreferredContentSize();
                math::FVector2 next(cs.x > 0.0f ? cs.x : pref.x,
                                    std::max(cs.y, pref.y));
                if (std::fabs(_content->getSize().x - next.x) > 0.5f
                    || std::fabs(_content->getSize().y - next.y) > 0.5f) {
                    _content->setSize(next);
                    if (!_content->getChildren().empty()) {
                        _content->performLayout();
                    }
                }
                const math::FVector2 known = _scrollState.getContentSize();
                const math::FVector2 contentSz = _content->getSize();
                if (std::fabs(contentSz.x - known.x) > 0.5f
                    || std::fabs(contentSz.y - known.y) > 0.5f) {
                    _scrollState.setContentSize(contentSz);
                }
                syncContentPosition();
                syncBarsToOffset();
            };
            fitContent();
            // VBar may have just appeared and inset the client — refit.
            const float w1 = _content->getSize().x;
            const float w2 = clientSize().x;
            if (w2 > 0.0f && std::fabs(w1 - w2) > 0.5f) {
                fitContent();
            }
        } else {
            // Legacy / horizontal-overflow path: sample preferred without
            // rewriting the content widget's size.
            const math::FVector2 pref = _content->getPreferredContentSize();
            const math::FVector2 sz = _content->getSize();
            const math::FVector2 next(
                std::max(pref.x, sz.x),
                std::max(pref.y, sz.y));
            const math::FVector2 known = _scrollState.getContentSize();
            if (std::fabs(next.x - known.x) > 0.5f
                || std::fabs(next.y - known.y) > 0.5f) {
                _scrollState.setContentSize(next);
            }
            syncContentPosition();
            syncBarsToOffset();
        }
    }
}

void ScrollView::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    // Background (resolveStyle pattern).
    const ResolvedStyle style = resolveStyle(getStyleId());
    math::FVector4 bg = math::FVector4(0.12f, 0.12f, 0.13f, 1.0f);
    if (style.hasStyle) bg = style.backgroundColor;
    // B3: rounded fill matches the 2px rounded border (drawn last).
    renderer.drawRoundedRect(bounds, bg, 2.0f);

    // Clip + permanently-offset content (see syncContentPosition).
    // getClientRect is inset by the frame border so content cannot cover
    // the stroke. Border is painted AFTER content/bars so chrome wins
    // z-order even if a child kisses the clip edge.
    if (_content != nullptr) {
        syncContentPosition();
        renderer.pushClip(getClientRect());
        _content->render(renderer);
        renderer.popClip();
    }
    if (_vbar != nullptr) _vbar->render(renderer);
    if (_hbar != nullptr) _hbar->render(renderer);

    renderer.drawBorderRect(bounds,
        math::FVector4(0.45f, 0.45f, 0.5f, 1.0f), 1.0f, 2.0f);
}

void ScrollView::renderChildren(IRenderBackend& renderer) {
    // PR-Container-Contract-Cut2: helper handles push/pop balance and
    // skips _vbar/_hbar so bars remain painted after popClip (in
    // onRender above) instead of being clipped out by the cascade.
    // _content is also skipped — it has its own onRender path because
    // of the -scrollOffset sync; double-painting it under the helper
    // would redraw the scrolled frame over the chrome.
    compoundDescendClippedRender(this, renderer, {_vbar, _hbar, _content});
}

Widget* ScrollView::hitTest(const math::FVector2& worldPos) {
    if (!_visible) return nullptr;
    const math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) return nullptr;

    // PR-Container-Contract-Cut2: bars get first crack at the click
    // (they're chrome — visible above content). After bars, defer to
    // the shared clipped helper so the descent path matches the
    // render clip exactly (same getClientRect gate).
    if (_vbar != nullptr && _vbar->isVisible()) {
        if (Widget* hit = _vbar->hitTest(worldPos)) {
            return hit;
        }
    }
    if (_hbar != nullptr && _hbar->isVisible()) {
        if (Widget* hit = _hbar->hitTest(worldPos)) {
            return hit;
        }
    }
    // syncContentPosition must run BEFORE the helper so the descendant
    // tree (rooted at _content) has the -scrollOffset layout that the
    // clipped gate was derived against.
    if (_content != nullptr) {
        syncContentPosition();
    }
    return compoundDescendHitTestClipped(this, worldPos);
}

Widget* createScrollViewWidget() {
    return new ScrollView();
}

} // namespace ayt::ui
