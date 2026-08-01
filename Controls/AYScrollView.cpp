#include "AYScrollView.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"
#include "aymath/MathUtils.h"

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
            const float maxOff = (_scrollState.getContentSize().y -
                                  getViewportSize().y);
            if (maxOff <= 0.0f) return;
            // Map v (0..contentHeight) to scrollOffset.y.
            _scrollState.setScrollOffset(math::FVector2(
                _scrollState.getScrollOffset().x, v));
            if (_onScroll) _onScroll(_scrollState.getScrollOffset());
        });
        addChildExternal(_vbar);
    }
    if (_hbarEnabled && _hbar == nullptr) {
        _hbar = new ScrollBar();
        _hbar->setOrientation(ScrollBar::Orientation::Horizontal);
        _hbar->setOnValueChanged([this](float v) {
            const float maxOff = (_scrollState.getContentSize().x -
                                  getViewportSize().x);
            if (maxOff <= 0.0f) return;
            _scrollState.setScrollOffset(math::FVector2(
                v, _scrollState.getScrollOffset().y));
            if (_onScroll) _scrollState.getScrollOffset();
        });
        addChildExternal(_hbar);
    }
}

math::FVector2 ScrollView::getViewportSize() const {
    return math::FVector2(getWidth(), getHeight());
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
        syncBarsToOffset();
        if (_onScroll) _onScroll(_scrollState.getScrollOffset());
    }
    return changed;
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

    // If content has no explicit content size, derive it from the
    // content widget's own size.
    if (_content != nullptr) {
        const math::FVector2 known = _scrollState.getContentSize();
        if (known.x <= 0.0f && known.y <= 0.0f) {
            _scrollState.setContentSize(_content->getSize());
            syncBarsToOffset();
        }
    }

    // Default CompoundWidget cascade (recurse into children).
    CompoundWidget::performLayout();
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

    // Render children — content first (with offset), bars last.
    if (_content != nullptr) {
        // Apply scroll offset to content. We don't mutate the content
        // widget's position (it stays at its layout-determined place);
        // we instead push a translate transform. For now (no transform
        // stack abstraction in v1), we synthesize the worldPosition
        // via a clip and by overriding the content's draw calls. To
        // keep implementation simple and testable, setPosition the
        // content widget by the negative offset, render, restore.
        const math::FVector2 origPos = _content->getPosition();
        _content->setPosition(math::FVector2(
            origPos.x - _scrollState.getScrollOffset().x,
            origPos.y - _scrollState.getScrollOffset().y));
        _content->render(renderer);
        _content->setPosition(origPos);
    }
    // Render bars via standard children loop (CompoundWidget provides
    // render/renderChildren). Done via renderChildren — but we want
    // content drawn before bars. Simplest: drop the default render
    // recursion and pick: content -> vbar -> hbar.
    if (_vbar != nullptr) _vbar->render(renderer);
    if (_hbar != nullptr) _hbar->render(renderer);
}

Widget* createScrollViewWidget() {
    return new ScrollView();
}

} // namespace ayt::ui
