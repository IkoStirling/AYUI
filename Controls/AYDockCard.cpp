#include "AYDockCard.h"
#include "AYBox.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"
#include "AYUIManager.h"
#include "AYDragDrop.h"

namespace ayt::ui {

DockCard::DockCard() {
    // Default size matches a typical inspector / hierarchy panel.
    setSize(math::FVector2(240.0f, 200.0f));

    // D3 — tear-off UX wiring. The card is the drag source for the G12
    // drag session; UIManager pulls the payload via getDragPayload() at
    // beginDrag time (see AYUIManager.cpp:1196-1206). We rebuild the
    // payload in onDragStart so it reflects the CURRENT position
    // (title may have changed via setTitle since the last drag).
    //
    // _floatable is initialized to true (header default) so a freshly
    // constructed card is draggable. setFloatable(false) below mirrors
    // the flag to setDraggable(false) for K-INV-D3-2.
    setDraggable(true);
    setOnDragStart([this]() {
        DragPayload p;
        p.kind = "DockCard";
        // Ghost label shows the card title so the user sees what's
        // being dragged. UIManager copies the payload verbatim.
        p.text = _title;
        // userData carries the source slot enum (0..4 = Left/Right/Top/
        // Bottom/Center) so DockArea::onDrop can decide whether the
        // same-slot no-op (K-INV-D3-1) applies. The card itself does
        // not know which slot it lives in; DockArea stamps that into
        // the payload via setDragPayload in its onDragEnter for the
        // own-card-as-source case. For the drag-source card we read
        // the slot at beginDrag time via DockArea::findCard(id)'s
        // location — but the simpler approach is to NOT pre-populate
        // userData here and let DockArea detect the "drag from own
        // slot" case via _cardIndex lookup at drop time. We therefore
        // leave userData = 0 here; DockArea::onDrop uses payload.id
        // and queries _cardIndex[id] for the current slot.
        p.userData = 0;
        setDragPayload(p);
    });
    setOnDragEnd([this](bool /*accepted*/) {
        // Drag session ended (drop fired OR cancel). Cursor reset is
        // driven by UIManager re-polling cursor hint; we don't need to
        // explicitly clear _titleBarHover here because the next
        // onMouseMove / onMouseLeave will refresh it.
    });
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

void DockCard::setFloatable(bool f) {
    _floatable = f;
    // K-INV-D3-2 — a non-floatable card must not start a drag session.
    // setDraggable(false) is the upstream gate in
    // UIManager::beginDrag's `if (!source->isDraggable()) return false`
    // path (mirrors UIManager's existing behaviour for unset widgets).
    setDraggable(f);
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

// =============================================================================
// D3 — Tear-off UX: title-bar mouse path
// =============================================================================
//
// The card is the drag SOURCE for a G12 drag session. DockArea /
// DockOverlay are the drop TARGETS (see AYDockArea.cpp / AYDockOverlay.cpp
// for the drop-side wiring).
//
// Mouse flow (see design.md §17.5 D3):
//   1. LMB down on title strip → beginDrag(this) if _floatable
//   2. Move → UIManager::updateDrag fires onDragEnter/Leave on the
//      DockArea/DockOverlay under the cursor (no onDragOver; the
//      highlight is polled in onRender via isCurrentDropTarget())
//   3. LMB up → endDrag(true) → DockArea::onDrop moves the card
//   4. Esc during drag → cancelDrag() → endDrag(false); card stays put
// =============================================================================

bool DockCard::onMouseButtonDown(const UIMouseEvent& e) {
    // Only LMB starts a tear-off drag. Other buttons keep Panel's
    // default behaviour.
    if (e.mouseButton != 0) {
        return false;
    }

    // K-INV-D3-2 — non-floatable cards short-circuit even before the
    // hit-test. setDraggable(false) already gates beginDrag, but the
    // explicit check here avoids the titleBar hit-test work and makes
    // the intent clear to future readers.
    if (!_floatable) {
        return false;
    }

    const math::FRectangle bounds = getWorldBounds();
    const math::FRectangle titleBar(
        bounds.minX,
        bounds.minY,
        bounds.maxX,
        bounds.minY + _headerHeight
    );

    if (!titleBar.contains(e.mousePos)) {
        return false;
    }

    // Hand off to UIManager's drag session. UIManager::beginDrag will:
    //   * call our _onDragStart (set in ctor) which rebuilds the payload
    //     from the current title
    //   * read getDragPayload() for the ghost label / kind tag
    //   * start the ghost widget at the card's centre
    // UIManager can be null in standalone tests; tryGet() returns
    // nullptr safely rather than asserting.
    if (UIManager* ui = UIManager::tryGet()) {
        if (ui->beginDrag(this)) {
            // Bring the card to the front so the floating copy (if the
            // drop is on the overlay) doesn't render under another
            // floating card added earlier.
            bringToFront();
            return true;
        }
    }
    return false;
}

bool DockCard::onMouseMove(const UIMouseEvent& e) {
    const math::FRectangle bounds = getWorldBounds();
    const math::FRectangle titleBar(
        bounds.minX,
        bounds.minY,
        bounds.maxX,
        bounds.minY + _headerHeight
    );
    _titleBarHover = _floatable && titleBar.contains(e.mousePos);
    // No-op behaviour; return false so UIManager keeps tracking hover
    // propagation (mirrors the Widget::onMouseMove default).
    return false;
}

void DockCard::onMouseLeave() {
    _titleBarHover = false;
    // Inherited leave propagation walks children — same pattern as
    // Window::onMouseLeave (AYWindow.cpp). Widget base handles the
    // call to compoundDescendLeave when applicable.
    Widget::onMouseLeave();
}

UiCursorHint DockCard::getCursorHint() const {
    // Mirror Window::getCursorHint: when hovering the drag handle, the
    // cursor should signal "this can be moved" rather than the default
    // arrow. Move hint is the closest UiCursorHint enum value to the
    // conventional "open hand" / "floating cursor" used by IDE shells.
    if (_titleBarHover && _floatable) {
        return UiCursorHint::Move;
    }
    return UiCursorHint::Default;
}

} // namespace ayt::ui