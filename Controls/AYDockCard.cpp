#include "AYDockCard.h"
#include "AYBox.h"
#include "AYDockArea.h"
#include "AYDockOverlay.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"
#include "AYUIManager.h"
#include "AYDragDrop.h"
#include "AYDockTrace.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

namespace {

// PR-Dock-TearOff — minimum press→release travel (Manhattan distance)
// before a void-drop promotes the card to a host window. Guards against
// accidental single clicks on the title bar popping a window.
constexpr float kPromoteDragThreshold = 8.0f;

} // namespace

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
    // PR-Dock-TearOff: stamp the payload UP FRONT. UIManager::beginDrag
    // reads source->getDragPayload() BEFORE firing _onDragStart (see
    // AYUIManager.cpp:1498), so the kind must be pre-set or
    // DockArea::onDrop's `kind != "DockCard"` gate silently rejects
    // EVERY drop (this was the "drag does nothing" symptom). The
    // onDragStart rebuild below only refreshes the ghost title.
    {
        DragPayload p;
        p.kind = "DockCard";
        p.data = this;   // DockArea::onDrop prefers payload.data over getDragSource
        setDragPayload(p);
    }
    setOnDragStart([this]() {
        DragPayload p;
        p.kind = "DockCard";
        p.data = this;
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
        // leave userData = 0 here; DockArea::onDrop uses payload.data
        // and queries _cardIndex[id] for the current slot.
        p.userData = 0;
        setDragPayload(p);
        // PR-S5e: the card STAYS visible while dragging — the ghost is
        // a drop preview, not a replacement for the original content
        // (the old setVisible(false) made the docked copy vanish, which
        // reads as "the content disappears when I drag"). The card
        // keeps its slot position until the drop commits (moveInSlot /
        // floatCard / dockCard relocate it); a cancelled drag leaves it
        // exactly where it started. No onDragEnd restoration needed.
    });
    setOnDragEnd([this](bool accepted) {
        // PR-Dock-TearOff: released over NO accepting target → promote
        // the card to a host top-level window (IDE-style tear-off).
        // Guards:
        //   * accepted == false → Esc / cancel, card stays put
        //   * no promote callback → host can't host (stays put)
        //   * drag didn't actually move → a title-bar click without
        //     movement must not pop a window
        UIManager* ui = UIManager::tryGet();
        const bool hadTarget = ui && ui->lastDragHadDropTarget();
        dockTrace(
            "[dock] onDragEnd card=%s accepted=%d promoteCb=%d hadTarget=%d\n",
            getId().c_str(), accepted ? 1 : 0, _promoteCb ? 1 : 0,
            hadTarget ? 1 : 0);
        if (!accepted) {
            return;
        }
        if (ui == nullptr || !_promoteCb) {
            dockTrace("[dock] onDragEnd skip promote (no ui or no cb)\n");
            return;
        }
        // Drop landed on an accepting target (DockArea etc.) — onDrop
        // already relocated / no-op'd the card. Only void drops promote.
        if (ui->lastDragHadDropTarget()) {
            dockTrace("[dock] onDragEnd skip promote (had drop target)\n");
            return;
        }
        const math::FVector2 dropPos = ui->getDragLastMousePos();
        const float moved = std::fabs(dropPos.x - _dragStartPos.x)
                          + std::fabs(dropPos.y - _dragStartPos.y);
        if (moved < kPromoteDragThreshold) {
            dockTrace("[dock] onDragEnd skip promote (moved=%.1f < thr)\n",
                      moved);
            return;
        }
        dockTrace("[dock] onDragEnd PROMOTE card=%s pos=(%.1f,%.1f)\n",
                  getId().c_str(), dropPos.x, dropPos.y);
        // Find the owning DockArea by walking the parent chain (the
        // card sits in a slot VBox/HBox, or in the DockOverlay; both
        // are DockArea children).
        DockArea* dock = nullptr;
        for (Widget* p = getParent(); p != nullptr; p = p->getParent()) {
            if (auto* d = dynamic_cast<DockArea*>(p)) {
                dock = d;
                break;
            }
        }
        if (dock == nullptr) {
            return;
        }
        const std::string id = getId();
        if (id.empty()) {
            return;
        }
        // Slot-docked card → float to the release point first (the
        // parent == DockOverlay gate in detachToOwnWindow requires a
        // floating card). Already-floating card → floatCard no-ops and
        // we promote in place.
        dock->floatCard(id, dropPos);
        // Host accepted → card detaches to the new window; rejected →
        // it stays floating on the overlay (same as dropping on the
        // overlay's empty area).
        detachToOwnWindow();
    });
}

DockCard::~DockCard() {
    // R3-safe parallel of ~TextInput / ~TextArea::TextDocument: drop the
    // UIManager's transient drag pointers BEFORE the vtable dispatch
    // chain unwinds. Without this, a tear-off drag in progress when the
    // editor closes its DockArea (or destroys the card mid-drag) would
    // leave _dragSession.source / _dragSession.currentTarget pointing at
    // freed memory; the next endDrag() would call _onDragEnd on the
    // freed source widget → UB. clearDragStateNoDispatch is the no-virtual
    // variant (commit 3844a95), mirroring clearFocusNoDispatch /
    // clearCaptureNoDispatch.
    if (UIManager* ui = UIManager::tryGet()) {
        ui->clearDragStateNoDispatch(this);
    }

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
    // Size content FIRST, then descend. If compoundDescendLayout runs
    // before the body inset, a full-bleed Center content child can keep a
    // stale full-card rect for a frame and steal title-bar hits.
    const math::FVector2 sz = getSize();
    if (_content) {
        const float bodyH = _collapsed ? 0.0f : std::max(0.0f, sz.y - _headerHeight);
        _content->setPosition(math::FVector2(0.0f, _headerHeight));
        _content->setSize(math::FVector2(sz.x, bodyH));
    }
    compoundDescendLayout(this);
}

Widget* DockCard::hitTest(const math::FVector2& worldPos) {
    if (!isVisible()) {
        return nullptr;
    }
    const math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) {
        return nullptr;
    }
    if (_headerHeight > 0.0f) {
        const math::FRectangle titleBar(
            bounds.minX, bounds.minY,
            bounds.maxX, bounds.minY + _headerHeight);
        if (titleBar.contains(worldPos)) {
            return this;
        }
    }
    // Body / content — reverse-order children, then self.
    return compoundDescendHitTest(this, worldPos);
}

void DockCard::onRender(IRenderBackend& renderer) {
    // Re-use Panel's body rendering (background + border). When the
    // editor punches a Play-mode composite hole it disables the Panel
    // background — skip the header strip too so we don't paint an
    // opaque bar over the 3D blit (card_viewport uses headerHeight=0
    // today, but keep the guard for titled cards).
    if (!isBackgroundEnabled()) {
        return;
    }
    Panel::onRender(renderer);

    const math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    // Header strip - draws a slightly darker bar at the top and the
    // title text inside. We deliberately use worldBounds so docking
    // works inside any transform.
    const float h = _headerHeight;
    if (h <= 0.0f) {
        return;
    }
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
        dockTrace(
            "[dock] titleDown MISS card=%s mouse=(%.1f,%.1f) "
            "title=(%.1f,%.1f)-(%.1f,%.1f) bounds=(%.1f,%.1f)-(%.1f,%.1f)\n",
            getId().c_str(), e.mousePos.x, e.mousePos.y,
            titleBar.minX, titleBar.minY, titleBar.maxX, titleBar.maxY,
            bounds.minX, bounds.minY, bounds.maxX, bounds.maxY);
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
            // PR-Dock-TearOff: remember the press point so the void-drop
            // promote path can enforce the movement threshold.
            _dragStartPos = e.mousePos;
            // Bring the card to the front so the floating copy (if the
            // drop is on the overlay) doesn't render under another
            // floating card added earlier.
            bringToFront();
            dockTrace(
                "[dock] beginDrag OK card=%s mouse=(%.1f,%.1f) "
                "titleH=%.1f parent=%s\n",
                getId().c_str(), e.mousePos.x, e.mousePos.y, _headerHeight,
                getParent() ? getParent()->getId().c_str() : "(null)");
            return true;
        }
        dockTrace("[dock] beginDrag REJECTED card=%s\n", getId().c_str());
    } else {
        dockTrace("[dock] beginDrag no UIManager card=%s\n", getId().c_str());
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

// =============================================================================
// D5.5 — Card promotion (in-panel floating card → top-level host window).
// =============================================================================
//
// The card hands its frame (id + title + x/y/w/h) to a host via the
// injected PromoteCallback. The host (EditorChildWindowManager in the
// editor shell) decides whether to spawn a top-level HWND. If the host
// returns true, we detach from the parent DockOverlay — the host now
// owns the card's lifetime via the new window's UIManager.
//
// We deliberately do NOT delete `this` here: ownership transfer is the
// host's responsibility once it accepts the promotion. This matches the
// existing DockOverlay::removeFloatingCard contract (detach-only, no
// destroy — see AYDockOverlay.cpp:48).
//
// K-INV-D5.5-1 — detachToOwnWindow returns false if no callback was
// injected. Card stays in the overlay. This is the safe default and
// matches D3's "drag without target = no-op" pattern.

bool DockCard::detachToOwnWindow() {
    if (!_promoteCb) {
        return false;
    }

    // Pull the frame from the WORLD position + size. World position is
    // what the host wants: for the primary UIManager (root at client
    // (0,0)) it IS the primary-window client coordinate, so the host
    // can convert to screen space with a single ClientToScreen. Local
    // position would be wrong for a card nested in a DockArea whose
    // world origin is offset (editor shell header / gallery padding).
    // The card is a DockOverlay child at this point (the void-drop
    // path floats it first), but its world origin still reflects the
    // dock chain it came from.
    const math::FVector2 pos = getWorldPosition();
    const math::FVector2 sz  = getSize();

    // Title may be empty (e.g. viewport card); pass through as-is
    // rather than synthesizing a placeholder so the host's "untitled"
    // default owns the UX decision. We pass _title (wstring) so the
    // editor shell can carry wide text straight into the new top-level
    // window's title bar without round-tripping through UTF-8.
    // PR-Dock-TearOff live-card migration: pass `this` so the host can
    // reparent the LIVE widget tree (see PromoteCallback docs /
    // K-INV-D5.5-2). The overlay snapshot happens BEFORE the callback:
    // the host may reparent the card during the callback, so a
    // getParent() after it would miss the DockOverlay gate. We snapshot
    // the overlay and call removeFloatingCard on the snapshot —
    // removeFloatingCard's internal `parent == this` check makes it safe
    // for an already-reparented card (index-only cleanup, no detach).
    DockOverlay* overlay = dynamic_cast<DockOverlay*>(getParent());
    const bool accepted = _promoteCb(
        this,
        _title,
        static_cast<int>(pos.x),
        static_cast<int>(pos.y),
        static_cast<int>(sz.x),
        static_cast<int>(sz.y));

    if (!accepted) {
        // Host declined (e.g. host's window slot is full, layout parse
        // failed). Card stays in the overlay unchanged.
        return false;
    }

    // Detach from the overlay. If the card was a slot-docked card (not a
    // floating overlay child) we treat it as a no-op rather than
    // guessing. The slot-card-promotion path (future cut) will go
    // through a separate API.
    if (overlay) {
        overlay->removeFloatingCard(this);
    }
    return true;
}

} // namespace ayt::ui