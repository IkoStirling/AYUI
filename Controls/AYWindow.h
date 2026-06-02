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

    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;

    void setTitleBarHeight(float height) { _titleBarHeight = height; }
    float getTitleBarHeight() const { return _titleBarHeight; }

protected:
    std::wstring _title;
    bool _movable;
    bool _resizable;
    bool _closable;
    bool _modal;
    std::function<void()> _onClose;
    float _titleBarHeight;

    bool _isDragging;
    math::FVector2 _dragOffset;
};

} // namespace ayt::ui