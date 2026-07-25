#include "AYDockCard.h"
#include "AYBox.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"

namespace ayt::ui {

DockCard::DockCard() {
    // Default size matches a typical inspector / hierarchy panel.
    setSize(math::FVector2(240.0f, 200.0f));
}

DockCard::~DockCard() {
    // Content is owned via destroyWidgetTree to keep the existing
    // children-tree lifecycle. CompoundWidget already tears down its
    // children tree on destruction, but a content Widget added via
    // setContent was attached to DockCard as a regular child (we re-use
    // the children tree to surface hit-test / layout), so the default
    // ~CompoundWidget destructor handles it. The explicit nullptr reset
    // here is defensive in case future code paths replace content
    // without going through setContent.
    _content = nullptr;
}

void DockCard::setTitle(const std::wstring& title) {
    _title = title;
}

void DockCard::setContent(Widget* w) {
    if (_content == w) {
        return;
    }
    // Tear down the previous content (if any). It lives in our child
    // tree so we route through CompoundWidget's removeChild to keep
    // parent / child invariants consistent.
    if (_content) {
        destroyWidgetTree(_content);
        _content = nullptr;
    }
    _content = w;
    if (_content) {
        // CompoundWidget::addChild takes a borrowed pointer; ownership
        // transfers when the parent dies. This is the same pattern
        // ScrollView uses for its content.
        addChild(_content);
    }
}

void DockCard::setCollapsed(bool c) {
    _collapsed = c;
    // AYWidget has no public setDirty flag; performLayout() is cheap
    // for a DockCard (descend into one content child) and is invoked
    // by the host's UIManager on the next frame. We just mutate state.
}

void DockCard::performLayout() {
    // Default to the CompoundFocusableWidget behavior (descend layout
    // into children), then size _content to fit the body region below
    // the header strip. Header is _headerHeight tall; content occupies
    // (height - headerHeight) when not collapsed.
    compoundDescendLayout(this);

    const math::FVector2 sz = getSize();
    if (_content) {
        const float bodyH = _collapsed ? 0.0f : (sz.y - _headerHeight);
        _content->setPosition(math::FVector2(0.0f, _headerHeight));
        _content->setSize(math::FVector2(sz.x, bodyH));
    }
}

void DockCard::onRender(IRenderBackend& renderer) {
    // Re-use Panel's body rendering (background + border).
    Panel::onRender(renderer);

    const math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    // Header strip - draws a slightly darker bar at the top and the
    // title text inside. We deliberately use worldBounds so docking
    // works inside any transform.
    const float h = _headerHeight;
    const math::FRectangle header(
        bounds.minX,
        bounds.minY,
        bounds.minX + (bounds.maxX - bounds.minX),
        bounds.minY + h
    );
    renderer.drawRect(header, math::FVector4(0.16f, 0.16f, 0.18f, 1.0f));
    renderer.drawBorderRect(header, math::FVector4(0.10f, 0.10f, 0.10f, 1.0f), 1.0f, 0.0f);

    if (!_title.empty()) {
        // Title sits inside the header with a small left padding.
        const math::FRectangle textBounds(
            header.minX + 6.0f,
            header.minY,
            header.maxX - 6.0f,
            header.maxY
        );
        renderer.drawText(textBounds, _title, 12, math::FVector4(0.92f, 0.92f, 0.94f, 1.0f));
    }
}

} // namespace ayt::ui