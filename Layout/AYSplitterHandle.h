#pragma once

#include "AYWidget.h"

namespace ayt::ui {

class HBox;

class SplitterHandle : public Widget {
public:
    static constexpr float kDefaultWidth = 4.0f;
    // Hover-reveal delay: matches VSCode-style splitters that stay invisible
    // until the cursor lingers for a short moment. Filter out fast cursor
    // passes that would otherwise flicker the splitter on and off.
    static constexpr float kHoverRevealDelay = 0.15f;

    SplitterHandle();
    ~SplitterHandle() override = default;

    bool isSplitterHandle() const override { return true; }

    void bindPanels(HBox* owner, int leftPanelSlot, int rightPanelSlot);

    bool isDragging() const { return _dragging; }

    // Force-end an in-progress drag (clears `_dragging` + hover reveal).
    // Used by UIManager when capture was lost without a mouse-up, which
    // otherwise leaves isRevealed() true forever via `_dragging`.
    void endDrag();

    // True when the splitter is currently in its revealed (drawn) state —
    // either dragging, or hovering past the reveal delay. Exposed for tests
    // so the test doesn't need to inspect private delay counters.
    bool isRevealed() const;

    // Hit / hover / draw band clamped to kDefaultWidth (guards against a
    // layout bug that accidentally gave the handle fill width).
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
    void applyDrag(float mouseWorldX);

    HBox* _owner = nullptr;
    int _leftPanelSlot = -1;
    int _rightPanelSlot = -1;
    bool _hover = false;
    bool _dragging = false;
    float _dragStartMouseX = 0.0f;
    float _dragStartPrimaryWidth = 0.0f;
    bool _adjustLeft = true;
    // Seconds elapsed since the cursor entered the splitter, or -1 when
    // not hovering. Drains back to -1 on onMouseLeave and resets to 0
    // each time the cursor re-enters. Drag overrides the delay (drag is
    // a strong intent — no point waiting 150ms).
    float _hoverElapsed = -1.0f;
};

} // namespace ayt::ui
