#include "AYScrollView.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"
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
    const math::FVector2 off = _scrollState.getScrollOffset();
    _content->setPosition(math::FVector2(-off.x, -off.y));
}

math::FRectangle ScrollView::contentClipRect() const {
    math::FRectangle clip = getWorldBounds();
    const float barW = ScrollBar::kDefaultBarWidth;
    if (_vbar != nullptr && _vbar->isVisible()) {
        clip.maxX = std::max(clip.minX, clip.maxX - barW);
    }
    if (_hbar != nullptr && _hbar->isVisible()) {
        clip.maxY = std::max(clip.minY, clip.maxY - barW);
    }
    return clip;
}

void ScrollView::syncBarsToOffset() {
    const math::FVector2 vp = getViewportSize();
    const math::FVector2 content = _scrollState.getContentSize();
    if (_vbar != nullptr) {
        _vbar->setRange(0.0f, content.y);
        _vbar->setViewportSize(vp.y);
        _vbar->setValue(_scrollState.getScrollOffset().y);
    }
    if (_hbar != nullptr) {
        _hbar->setRange(0.0f, content.x);
        _hbar->setViewportSize(vp.x);
        _hbar->setValue(_scrollState.getScrollOffset().x);
    }
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

    // Refresh content size AFTER children layout so page switches
    // (e.g. short Basics → tall Capabilities) update maxScrollOffset /
    // vbar. Sampling only when content size was still (0,0) froze the
    // first page's height and left later pages without a scrollbar.
    CompoundWidget::performLayout();

    if (_content != nullptr) {
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

void ScrollView::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    // Background (resolveStyle pattern).
    const ResolvedStyle style = resolveStyle(getStyleId());
    math::FVector4 bg = math::FVector4(0.12f, 0.12f, 0.13f, 1.0f);
    if (style.hasStyle) bg = style.backgroundColor;
    renderer.drawRect(bounds, bg);
    renderer.drawBorderRect(bounds,
        math::FVector4(0.45f, 0.45f, 0.5f, 1.0f), 1.0f, 2.0f);

    // Clip + permanently-offset content (see syncContentPosition). Same
    // pattern as Window::renderChildren — without pushClip, scrolled
    // pages paint over chrome ("内容没有被裁剪").
    if (_content != nullptr) {
        syncContentPosition();
        renderer.pushClip(contentClipRect());
        _content->render(renderer);
        renderer.popClip();
    }
    if (_vbar != nullptr) _vbar->render(renderer);
    if (_hbar != nullptr) _hbar->render(renderer);
}

void ScrollView::renderChildren(IRenderBackend& /*renderer*/) {
    // Intentionally empty — see onRender. Bars/content are drawn there
    // so a second unoffset content paint cannot cover the scrolled view.
}

Widget* ScrollView::hitTest(const math::FVector2& worldPos) {
    if (!_visible) return nullptr;
    const math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) return nullptr;

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
    // Clip hits to the visible content rect — scrolled-off children must
    // not remain clickable at their old on-screen slots.
    if (_content != nullptr && contentClipRect().contains(worldPos)) {
        syncContentPosition();
        if (Widget* hit = _content->hitTest(worldPos)) {
            return hit;
        }
    }
    return this;
}

Widget* createScrollViewWidget() {
    return new ScrollView();
}

} // namespace ayt::ui
