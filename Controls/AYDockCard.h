#pragma once

#include "AYPanel.h"
#include <functional>
#include <memory>
#include <string>

namespace ayt::ui {

class DockArea;

// D1: DockCard - a leaf panel with a title + optional icon that hosts a
// single widget tree. Built on top of Panel so existing style + border +
// padding plumbing keeps working. The "thing" the user moves between slots
// in the editor shell.
//
// D1 ships the data model only; tear-off UX (drag-to-float) is D3 and the
// persistence layer is D4. D1 cards already render with their title text
// in the header strip and host arbitrary content (any Widget subclass).
//
// Lifecycle: DockCard owns its content Widget* via setContent. When the
// card is destroyed (or detached from a slot), the content is destroyed
// through destroyWidgetTree to mirror how CompoundWidget handles children.
class DockCard : public Panel {
public:
    DockCard();
    ~DockCard() override;

    // The card's stable identifier inside the DockArea registry. Used by
    // DockArea::findCard / saveLayout / loadLayout to round-trip the card.
    const std::string& getId() const { return _id; }
    void setId(const std::string& id) { _id = id; }

    // Title shown in the header strip. Stored as wide string to match
    // the rest of the AYUI text API (Button, TextLabel, etc.).
    const std::wstring& getTitle() const { return _title; }
    void setTitle(const std::wstring& title);

    // Optional glyph (asset name) for the header strip. v1.5 just stores
    // the string; the render path that draws the icon lives in D1's
    // header draw and is a no-op when empty.
    const std::string& getIcon() const { return _icon; }
    void setIcon(const std::string& icon) { _icon = icon; }

    // Replaces the currently-hosted content. The previous content (if any)
    // is destroyed. Pass nullptr to clear.
    void setContent(Widget* w);
    Widget* getContent() const { return _content; }

    // Whether the card shows a close ("x") affordance in its header.
    // When true, the header paints an X; a press fires onCloseRequested
    // (host removes the card / closes the promoted child window).
    void setClosable(bool c) { _closable = c; }
    bool isClosable() const { return _closable; }

    using CloseCallback = std::function<void(DockCard* card)>;
    void setOnCloseRequested(CloseCallback cb) { _onCloseRequested = std::move(cb); }

    // Whether the card can be torn off into a floating card. D1 stores
    // the flag; D3 wires the actual drag path. setFloatable(false) also
    // clears setDraggable(false) so the title bar never starts a session
    // for an explicitly non-floatable card (K-INV-D3-2).
    void setFloatable(bool f);
    bool isFloatable() const { return _floatable; }

    // Whether the header is collapsed (only the title strip is visible).
    // Default: false. Stored as a flag for D3 / D4 round-trip.
    void setCollapsed(bool c);
    bool isCollapsed() const { return _collapsed; }

    // Header strip height (logical pixels). Default 22px.
    void setHeaderHeight(float h) { _headerHeight = h; }
    float getHeaderHeight() const { return _headerHeight; }

    void onRender(IRenderBackend& renderer) override;
    void performLayout() override;
    // Body rect below the header — shared by renderChildren clip and
    // hitTest descent (same contract as Window::getClientRect).
    math::FRectangle getClientRect() const override;
    // Clip content to the body so fixed-size grandchildren (Gallery
    // Ping button, labels) cannot paint into neighboring dock panels.
    void renderChildren(IRenderBackend& renderer) override;
    // Title-bar chrome claims hits before content children. Center-slot
    // cards (full-bleed Panel content) otherwise lost title presses to a
    // mis-sized child and became undraggable after a swap into Center.
    Widget* hitTest(const math::FVector2& worldPos) override;

    // D3 — tear-off UX. The card's title bar becomes a drag handle that
    // tears the card off into a floating card. The actual G12 wiring lives
    // in the ctor (setDraggable + setOnDragStart + setOnDragEnd); these
    // overrides are the mouse path.
    //
    // K-INV-D3-2 — _floatable=false short-circuits the title-bar drag.
    // Return type mirrors Widget::onMouseButtonDown (bool) — overriding
    // with a different return type triggers C2555.
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    // Close-X commits on mouse-up so UIManager can clear capture before
    // destroyWidgetTree runs (down-path destroy left a dangling capture → AE).
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    void onMouseLeave() override;
    UiCursorHint getCursorHint() const override;

    // D5.5 — Card promotion. The card hands off its frame to an external
    // host (typically the editor's EditorChildWindowManager) which then
    // owns it as a top-level HWND. AYUI does not depend on the host type:
    // the PromoteCallback is injected at runtime by the editor shell
    // (see EditorChildWindowManager::setPromoteCallback). Returning true
    // means the host accepted the promotion — DockCard then detaches
    // itself from its parent DockOverlay. Returning false aborts the
    // promotion and the card stays in the overlay.
    //
    // PR-Dock-TearOff live-card migration: the host receives the card
    // ITSELF (DockCard*), not just its id — the host may reparent the
    // live widget tree into its own UIManager (addChild auto-detaches
    // from the old parent). x/y are the card's world position (= primary
    // client coords when the primary root sits at (0,0)); the host is
    // responsible for client→screen conversion on platforms that need it.
    //
    // K-INV-D5.5-2 — ownership contract: while the callback runs the
    // card still belongs to the calling tree (this call happens before
    // detachToOwnWindow's removeFloatingCard). Returning false must
    // leave the card untouched; returning true after reparenting is
    // safe — removeFloatingCard on an already-reparented card only
    // clears the overlay's bookkeeping index (see AYDockOverlay.cpp).
    using PromoteCallback = std::function<bool(
        DockCard* card,
        const std::wstring& title,
        int x, int y, int w, int h)>;

    void setPromoteCallback(PromoteCallback cb) { _promoteCb = std::move(cb); }
    bool detachToOwnWindow();

    // Host-window chrome (promoted OS HWND only). Overlay floating
    // DockCards must leave these false — no maximize 口, no SE grip.
    void setShowResizeGrip(bool show) { _showResizeGrip = show; }
    bool showResizeGrip() const { return _showResizeGrip; }
    void setShowMaximizeButton(bool show) { _showMaximizeButton = show; }
    bool showMaximizeButton() const { return _showMaximizeButton; }
    // Visual only — host keeps this in sync with OS IsZoomed.
    void setMaximizedVisual(bool maximized) { _maximizedVisual = maximized; }
    bool maximizedVisual() const { return _maximizedVisual; }

    // Raw handler (not std::function): promote runs mid-drag while the
    // card still holds a CloseCallback; assigning another std::function
    // of the same signature corrupted tidy and AVed in Gallery promote.
    using MaximizeHandler = void (*)(void* user, DockCard* card);
    void setMaximizeHandler(MaximizeHandler fn, void* user) {
        _maximizeFn = fn;
        _maximizeUser = user;
    }
    void clearMaximizeHandler() {
        _maximizeFn = nullptr;
        _maximizeUser = nullptr;
    }

private:
    std::string _id;
    std::wstring _title;
    std::string _icon;
    Widget* _content = nullptr;       // owned (destroyed via destroyWidgetTree)
    bool _closable = true;
    bool _floatable = true;
    bool _collapsed = false;
    float _headerHeight = 22.0f;

    // D3 — title-bar hover tracking (drives cursor hint + onMouseLeave).
    bool _titleBarHover = false;
    bool _closeHover = false;
    bool _maximizeHover = false;
    // Close affordance: armed on LMB-down over X, fired on LMB-up.
    bool _closeArmed = false;
    bool _maximizeArmed = false;
    bool _showResizeGrip = false;
    bool _showMaximizeButton = false;
    bool _maximizedVisual = false;
    MaximizeHandler _maximizeFn = nullptr;
    void* _maximizeUser = nullptr;

    math::FRectangle closeButtonRect() const;
    math::FRectangle maximizeButtonRect() const;

    // PR-Dock-TearOff — press position of the last accepted title-bar
    // drag. The void-drop promote path (onDragEnd) compares it against
    // the release position so a click-without-movement never pops a
    // host window.
    math::FVector2 _dragStartPos = math::FVector2(0.0f, 0.0f);

    // D5.5 — promotion hook. Default empty (detachToOwnWindow no-ops).
    PromoteCallback _promoteCb;
    CloseCallback   _onCloseRequested;
};

} // namespace ayt::ui