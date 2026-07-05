#pragma once

#include "AYWidget.h"

namespace ayt::ui {

class Window : public CompoundWidget {
public:
    Window();
    virtual ~Window();

    void setTitle(const std::wstring& title) { _title = title; }
    const std::wstring& getTitle() const { return _title; }

    void setMovable(bool movable) { _movable = movable; }
    bool isMovable() const { return _movable; }

    void setResizable(bool resizable) { _resizable = resizable; }
    bool isResizable() const { return _resizable; }

    void setClosable(bool closable) { _closable = closable; }
    bool isClosable() const { return _closable; }

    void setModal(bool modal) { _modal = modal; }
    bool isModal() const { return _modal; }

    void setOnClose(std::function<void()> callback) { _onClose = callback; }

    void setMinSize(float width, float height);
    void setMinSize(const math::FVector2& size);
    const math::FVector2& getMinSize() const { return _minSize; }

    void setSize(const math::FVector2& size);

    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;

    void setTitleBarHeight(float height) { _titleBarHeight = height; }
    float getTitleBarHeight() const { return _titleBarHeight; }

    bool isDragging() const { return _isDragging; }

    void onMouseLeave() override;

    UiCursorHint getCursorHint() const override;

    void onRender(IRenderBackend& renderer) override;

    void layoutChildren() override;

protected:
    math::FVector2 localPositionFromMouse(const math::FVector2& mouseWorldPos) const;
    void clampPositionWithinParent();

    std::wstring _title;
    bool _movable;
    bool _resizable;
    bool _closable;
    bool _modal;
    std::function<void()> _onClose;
    float _titleBarHeight;
    math::FVector2 _minSize;

    bool _isDragging;
    bool _titleBarHover = false;
    math::FVector2 _dragOffset;
};

} // namespace ayt::ui
