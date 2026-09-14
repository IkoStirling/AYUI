#pragma once

#include "AYUI/Widget.h"

namespace ayt::ui {

class BoxBase;

// Draggable separator between two panels of a VBox or HBox (dock-tree
// split nodes). The thin axis is always SplitterHandle::kDefaultWidth;
// the long axis is the owner's cross extent (set by the box layout).
//
// Orientation describes the DRAG AXIS (the coordinate that changes as
// the cursor moves the handle):
//   Horizontal — panels sit side-by-side in an HBox, drag follows
//                mouseWorldX. This is the legacy behavior.
//   Vertical   — panels stack in a VBox, drag follows mouseWorldY.
// Both orientations share the same hover-reveal / drag / clamp logic
// in BoxBase; only the axis coordinate and the visual rotation differ.
class SplitterHandle : public Widget {
public:
    enum class Orientation { Horizontal, Vertical };

    static constexpr float kDefaultWidth = 4.0f;
    // Hover-reveal delay: matches VSCode-style splitters that stay invisible
    // until the cursor lingers for a short moment. Filter out fast cursor
    // passes that would otherwise flicker the splitter on and off.
    static constexpr float kHoverRevealDelay = 0.15f;

    SplitterHandle();
    ~SplitterHandle() override;

    bool isSplitterHandle() const override { return true; }

    // Must be set before bindPanels. Horizontal == legacy HBox behavior.
    void setOrientation(Orientation orientation) { _orientation = orientation; markBoundsDirty(); markDirty(); }
    Orientation getOrientation() const { return _orientation; }

    // `beforePanelSlot`/`afterPanelSlot` are the owner's slot indices of
    // the two panels this handle separates. Shared by HBox (left/right)
    // and VBox (top/bottom).
    void bindPanels(BoxBase* owner, int beforePanelSlot, int afterPanelSlot);

    bool isDragging() const { return _dragging; }

    // Force-end an in-progress drag (clears `_dragging` + hover reveal).
    // Used by UIManager when capture was lost without a mouse-up, which
    // otherwise leaves isRevealed() true forever via `_dragging`.
    void endDrag();

    // True when the splitter is currently in its revealed (drawn) state —
    // either dragging, or hovering past the reveal delay. Exposed for tests
    // so the test doesn't need to inspect private delay counters.
    bool isRevealed() const;

    // Hit / hover / draw band clamped to kDefaultWidth on the thin axis
    // (guards against a layout bug that accidentally gave the handle
    // fill size along that axis).
    math::FRectangle interactionBand() const;

    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onMouseLeave() override;
    UiCursorHint getCursorHint() const override;
    void onRender(IRenderBackend& renderer) override;
    void tick(float dt) override;

    // Clears hover/reveal counters without touching `_dragging`.
    // Used by the editor host to force-unreveal when the cursor is
    // no longer on any splitter band (leave events alone are not
    // enough — capture / missed WM_MOUSEMOVE can skip onMouseLeave).
    void clearHoverReveal();

private:
    float cursorAxisPos(const math::FVector2& pos) const {
        return (_orientation == Orientation::Horizontal) ? pos.x : pos.y;
    }
    void applyDrag(float mouseAxisPos);

    BoxBase* _owner = nullptr;
    int _beforePanelSlot = -1;
    int _afterPanelSlot = -1;
    Orientation _orientation = Orientation::Horizontal;
    bool _hover = false;
    bool _dragging = false;
    float _dragStartMouseAxisPos = 0.0f;
    float _dragStartPrimarySize = 0.0f;
    bool _adjustBefore = true;
    // Seconds elapsed since the cursor entered the splitter, or -1 when
    // not hovering. Drains back to -1 on onMouseLeave and resets to 0
    // each time the cursor re-enters. Drag overrides the delay (drag is
    // a strong intent — no point waiting 150ms).
    float _hoverElapsed = -1.0f;
};

} // namespace ayt::ui
