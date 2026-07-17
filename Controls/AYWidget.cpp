#include "AYWidget.h"
#include "aymath/MathUtils.h"

namespace ayt::ui {

Widget::Widget()
    : _position(0.0f, 0.0f)
    , _size(100.0f, 50.0f)
    , _boundsDirty(true)
    , _parent(nullptr)
    , _visible(true)
    , _hoverWidget(nullptr)
{
}

Widget::~Widget() {
    // Phase UI-OWN-1: Widget never owns its children. The parent detaches
    // child _parent pointers here so that any subsequent access from the
    // child side (e.g. during child destruction) finds a null parent and
    // skips back-pointers into freed memory. Children themselves are
    // destroyed by the owning container via destroyWidgetTree().
    //
    // Do not call onMouseLeave() here — _hoverWidget may already be freed
    // when teardown order is caller-controlled (partial delete / UI-OWN-1).
    _hoverWidget = nullptr;
    for (Widget* child : _children) {
        if (child) {
            child->_parent = nullptr;
        }
    }
    _children.clear();
}

void Widget::addChild(Widget* child) {
    if (child && child->_parent != this) {
        child->detachFromParent();
        child->_parent = this;
        _children.push_back(child);
    }
}

void Widget::addChildExternal(Widget* child) {
    // Reference-only attach, identical to addChild today. Kept as a separate
    // name so callers (tests, stacks) can document intent. Lifetime of the
    // child remains the caller's responsibility.
    addChild(child);
}

void Widget::removeChild(Widget* child) {
    if (!child) return;
    for (auto it = _children.begin(); it != _children.end(); ++it) {
        if (*it == child) {
            child->_parent = nullptr;
            _children.erase(it);
            return;
        }
    }
}

void Widget::detachFromParent() {
    if (_parent) {
        _parent->removeChild(this);
    }
}

void Widget::bringToFront() {
    if (_parent == nullptr) {
        return;
    }

    std::vector<Widget*>& siblings = _parent->_children;
    for (auto it = siblings.begin(); it != siblings.end(); ++it) {
        if (*it == this) {
            siblings.erase(it);
            siblings.push_back(this);
            return;
        }
    }
}

math::FVector2 Widget::getWorldPosition() const {
    if (_parent) {
        return _parent->getWorldBounds().getMin() + _position;
    }
    return _position;
}

void Widget::updateWorldBounds() {
    math::FVector2 worldPos = getWorldPosition();
    _bounds.f[0] = worldPos.x;
    _bounds.f[1] = worldPos.y;
    _bounds.f[2] = worldPos.x + _size.x;
    _bounds.f[3] = worldPos.y + _size.y;
    _boundsDirty = false;
}

math::FRectangle Widget::getWorldBounds() const {
    // Lazy propagate: walk up the parent chain. If any ancestor is dirty,
    // we need to recompute our own bounds too — because getWorldPosition
    // depends on the parent's world position. This avoids the need to
    // pre-emptively dirty every descendant on every setter call.
    if (_boundsDirty) {
        const_cast<Widget*>(this)->updateWorldBounds();
        return _bounds;
    }
    if (_parent != nullptr && _parent->_boundsDirty) {
        const_cast<Widget*>(this)->updateWorldBounds();
    }
    return _bounds;
}

Widget* Widget::hitTest(const math::FVector2& worldPos) {
    if (!_visible) return nullptr;

    // Check children first (reverse order - last added is on top)
    for (auto it = _children.rbegin(); it != _children.rend(); ++it) {
        Widget* child = *it;
        Widget* hit = child->hitTest(worldPos);
        if (hit) {
            if (_hoverWidget && _hoverWidget != hit) {
                _hoverWidget->onMouseLeave();
            }
            _hoverWidget = hit;
            return hit;
        }
    }

    // If we had a previous hover widget, notify it
    if (_hoverWidget) {
        _hoverWidget->onMouseLeave();
        _hoverWidget = nullptr;
    }

    // Then check self
    math::FRectangle bounds = getWorldBounds();
    if (bounds.contains(worldPos)) {
        return this;
    }
    return nullptr;
}

void Widget::onMouseLeave() {
    // Override in subclasses if needed
}

void Widget::addEventHandler(UIEventType type, std::function<void(UIEvent&)> handler) {
    _eventHandlers[type].push_back(handler);
}

void Widget::removeEventHandlers(UIEventType type) {
    _eventHandlers.erase(type);
}

void Widget::bubbleEvent(UIEvent& e) {
    auto it = _eventHandlers.find(e.type);
    if (it != _eventHandlers.end()) {
        for (auto& handler : it->second) {
            e.target = this;
            handler(e);
            if (e.handled) return;
        }
    }

    if (_parent && !e.handled) {
        _parent->bubbleEvent(e);
    }
}

bool Widget::onMouseMove(const UIMouseEvent& e) {
    AYUNREFERENCED_PARAM(e);
    return false;
}

bool Widget::onMouseButtonDown(const UIMouseEvent& e) {
    AYUNREFERENCED_PARAM(e);
    return false;
}

bool Widget::onMouseButtonUp(const UIMouseEvent& e) {
    AYUNREFERENCED_PARAM(e);
    return false;
}

bool Widget::onKeyDown(int keyCode) {
    AYUNREFERENCED_PARAM(keyCode);
    return false;
}

bool Widget::onKeyUp(int keyCode) {
    AYUNREFERENCED_PARAM(keyCode);
    return false;
}

bool Widget::onTextInput(wchar_t ch) {
    AYUNREFERENCED_PARAM(ch);
    return false;
}

CompoundWidget::CompoundWidget() {
}

CompoundWidget::~CompoundWidget() {
}

void CompoundWidget::performLayout() {
    layoutChildren();
    for (Widget* child : _children) {
        child->performLayout();
    }
}

void CompoundWidget::onChildAdded(Widget* child) {
    AYUNREFERENCED_PARAM(child);
}

void CompoundWidget::onChildRemoved(Widget* child) {
    AYUNREFERENCED_PARAM(child);
}

void Widget::render(IRenderBackend& renderer) {
    if (!_visible) return;
    onRender(renderer);
    renderChildren(renderer);
}

void Widget::renderChildren(IRenderBackend& renderer) {
    for (Widget* child : _children) {
        child->render(renderer);
    }
}

} // namespace ayt::ui