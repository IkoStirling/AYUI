#pragma once

#include "AYWidget.h"

namespace ayt::ui {

class HBox;

class SplitterHandle : public Widget {
public:
    static constexpr float kDefaultWidth = 4.0f;

    SplitterHandle();
    ~SplitterHandle() override = default;

    void bindPanels(HBox* owner, int leftPanelSlot, int rightPanelSlot);

    bool isDragging() const { return _dragging; }

    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onMouseLeave() override;
    UiCursorHint getCursorHint() const override;
    void onRender(IRenderBackend& renderer) override;

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
};

} // namespace ayt::ui
