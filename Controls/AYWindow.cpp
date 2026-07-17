#include "AYWindow.h"
#include "AYIRenderBackend.h"
#include "aymath/MathUtils.h"

#include <algorithm>

namespace ayt::ui {

namespace {

float clampAxis(float value, float parentSize, float windowSize, float minKeepVisible)
{
    const float keepVisible = std::min(windowSize, std::max(minKeepVisible, 1.0f));

    if (windowSize <= parentSize) {
        const float maxValue = parentSize - windowSize;
        return std::clamp(value, 0.0f, std::max(0.0f, maxValue));
    }

    const float minValue = keepVisible - windowSize;
    const float maxValue = parentSize - keepVisible;
    return std::clamp(value, minValue, maxValue);
}

} // namespace

Window::Window()
    : _title(L"Window")
    , _movable(true)
    , _resizable(false)
    , _closable(true)
    , _modal(false)
    , _titleBarHeight(28.0f)
    , _minSize(120.0f, 80.0f)
    , _isDragging(false)
{
    setSize(math::FVector2(400.0f, 300.0f));
}

Window::~Window() {
}

void Window::setMinSize(float width, float height) {
    _minSize = math::FVector2(std::max(width, 1.0f), std::max(height, 1.0f));
    setSize(getSize());
}

void Window::setMinSize(const math::FVector2& size) {
    setMinSize(size.x, size.y);
}

void Window::setSize(const math::FVector2& size) {
    Widget::setSize(math::FVector2(std::max(size.x, _minSize.x), std::max(size.y, _minSize.y)));
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

math::FVector2 Window::localPositionFromMouse(const math::FVector2& mouseWorldPos) const {
    math::FVector2 parentOrigin(0.0f, 0.0f);
    if (Widget* parent = getParent()) {
        const math::FRectangle parentBounds = parent->getWorldBounds();
        parentOrigin = math::FVector2(parentBounds.minX, parentBounds.minY);
    }
    return mouseWorldPos - parentOrigin - _dragOffset;
}

void Window::clampPositionWithinParent() {
    Widget* parent = getParent();
    if (parent == nullptr) {
        return;
    }

    const float parentWidth  = parent->getWidth();
    const float parentHeight = parent->getHeight();
    const float windowWidth  = getWidth();
    const float windowHeight = getHeight();

    const float minKeepWidth  = std::max(_minSize.x, _titleBarHeight);
    const float minKeepHeight = std::max(_minSize.y, _titleBarHeight);

    const math::FVector2 pos = getPosition();
    const float clampedX = clampAxis(pos.x, parentWidth, windowWidth, minKeepWidth);
    const float clampedY = clampAxis(pos.y, parentHeight, windowHeight, minKeepHeight);
    setPosition(math::FVector2(clampedX, clampedY));
}

bool Window::onMouseMove(const UIMouseEvent& e) {
    math::FRectangle bounds = getWorldBounds();
    math::FRectangle titleBar(bounds.minX, bounds.minY,
                               bounds.maxX, bounds.minY + _titleBarHeight);
    _titleBarHover = _movable && titleBar.contains(e.mousePos);

    if (!_isDragging || !_movable) {
        return _titleBarHover;
    }

    setPosition(localPositionFromMouse(e.mousePos));
    clampPositionWithinParent();
    return true;
}

bool Window::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0) return false;

    math::FRectangle bounds = getWorldBounds();
    math::FRectangle titleBar(bounds.minX, bounds.minY,
                               bounds.maxX, bounds.minY + _titleBarHeight);

    if (_movable && titleBar.contains(e.mousePos)) {
        _isDragging = true;
        _dragOffset = e.mousePos - math::FVector2(bounds.minX, bounds.minY);
        setLayoutPositionManaged(false);
        bringToFront();
        return true;
    }

    return false;
}

bool Window::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }

    if (_isDragging) {
        _isDragging = false;
        clampPositionWithinParent();
        return true;
    }

    return false;
}

UiCursorHint Window::getCursorHint() const {
    if (_movable && (_isDragging || _titleBarHover)) {
        return UiCursorHint::Move;
    }
    return UiCursorHint::Default;
}

void Window::onMouseLeave() {
    _titleBarHover = false;
}

void Window::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    math::FRectangle titleBar(bounds.minX, bounds.minY, bounds.maxX, bounds.minY + _titleBarHeight);
    math::FRectangle body(bounds.minX, bounds.minY + _titleBarHeight, bounds.maxX, bounds.maxY);

    renderer.drawRect(bounds, math::FVector4(0.16f, 0.16f, 0.18f, 1.0f));
    renderer.drawRect(titleBar, math::FVector4(0.22f, 0.22f, 0.25f, 1.0f));
    renderer.drawRect(body, math::FVector4(0.12f, 0.12f, 0.14f, 1.0f));
    renderer.drawBorderRect(bounds, math::FVector4(0.08f, 0.08f, 0.08f, 1.0f), 1.0f, 0.0f);

    if (!_title.empty()) {
        math::FRectangle titleBounds(titleBar.minX + 8.0f, titleBar.minY,
                                     titleBar.maxX - 8.0f, titleBar.maxY);
        renderer.drawText(titleBounds, _title, 13, math::FVector4(0.92f, 0.92f, 0.92f, 1.0f));
    }
}

void Window::layoutChildren() {
    const float paddingLeft = 8.0f;
    const float paddingTop = 8.0f;
    const float paddingRight = 8.0f;
    const float paddingBottom = 8.0f;
    const float spacing = 6.0f;

    const float contentTop = _titleBarHeight + paddingTop;
    const float contentWidth = getWidth() - paddingLeft - paddingRight;
    const float contentHeight = getHeight() - contentTop - paddingBottom;
    if (contentWidth <= 0.0f || contentHeight <= 0.0f) {
        return;
    }

    size_t childCount = _children.size();
    if (childCount == 0) {
        return;
    }

    float totalFixedHeight = 0.0f;
    size_t fillCount = 0;
    for (Widget* child : _children) {
        const float childHeight = child->getHeight();
        if (childHeight > 0.0f) {
            totalFixedHeight += childHeight;
        } else {
            ++fillCount;
        }
        totalFixedHeight += spacing;
    }
    if (childCount > 0) {
        totalFixedHeight -= spacing;
    }

    const float fillHeight = (fillCount > 0)
        ? (contentHeight - totalFixedHeight) / static_cast<float>(fillCount)
        : 0.0f;

    float y = contentTop;
    for (Widget* child : _children) {
        float childHeight = child->getHeight();
        if (childHeight <= 0.0f) {
            childHeight = fillHeight;
        }

        child->setPosition(math::FVector2(paddingLeft, y));
        child->setSize(math::FVector2(contentWidth, childHeight));
        y += childHeight + spacing;
    }
}

} // namespace ayt::ui
