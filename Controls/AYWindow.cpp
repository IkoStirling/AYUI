#include "AYWindow.h"
#include "AYMathUtils.h"

namespace ayt::ui {

Window::Window()
    : _title(L"Window")
    , _movable(true)
    , _resizable(false)
    , _closable(true)
    , _modal(false)
    , _titleBarHeight(28.0f)
    , _isDragging(false)
{
    setSize(math::FVector2(400.0f, 300.0f));
}

Window::~Window() {
}

Widget* Window::hitTest(const math::FVector2& worldPos) {
    if (!_visible) return nullptr;

    math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) return nullptr;

    if (_movable) {
        math::FRectangle titleBar(bounds.minX, bounds.minY,
                                  bounds.maxX, bounds.minY + _titleBarHeight);
        if (titleBar.contains(worldPos)) {
            return this;
        }
    }

    for (auto it = _children.rbegin(); it != _children.rend(); ++it) {
        Widget* child = *it;
        Widget* hit = child->hitTest(worldPos);
        if (hit) return hit;
    }

    return this;
}

bool Window::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0) return false;

    math::FRectangle bounds = getWorldBounds();
    math::FRectangle titleBar(bounds.minX, bounds.minY,
                               bounds.maxX, bounds.minY + _titleBarHeight);

    if (_movable && titleBar.contains(e.mousePos)) {
        _isDragging = true;
        _dragOffset = e.mousePos - math::FVector2(bounds.minX, bounds.minY);
        return true;
    }

    return false;
}

} // namespace ayt::ui