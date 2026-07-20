#include "AYWidget.h"
#include "AYCompoundFocusableWidget.h"
#include "aymath/MathUtils.h"

namespace ayt::ui {

// =============================================================================
// Phase B (S3): shared compound-descent helpers. Both CompoundWidget and
// CompoundFocusableWidget route their performLayout/tick/hitTest/onMouseLeave
// through these helpers — single source of truth, diamond-free inheritance.
//
// Each helper takes the widget as an explicit argument because the call site
// (a derived class method) has the right `this` and we want the helper to be
// free of virtual dispatch (the helper operates on `Widget*` directly).
// =============================================================================

void compoundDescendLayout(Widget* self) {
    if (self == nullptr) return;
    // layoutChildren() is virtual on CompoundWidget and on
    // CompoundFocusableWidget. We invoke it via the base pointer; the
    // dispatch is dynamic, so each derived class's override fires.
    // Equivalent to CompoundWidget::performLayout pre-Phase-B.
    if (auto* cw = dynamic_cast<CompoundWidget*>(self)) {
        cw->layoutChildren();
    } else if (auto* cfw = dynamic_cast<CompoundFocusableWidget*>(self)) {
        cfw->layoutChildren();
    }
    for (Widget* child : self->getChildren()) {
        child->performLayout();
    }
}

void compoundDescendTick(Widget* self, float dt) {
    if (self == nullptr) return;
    self->Widget::tick(dt);
    for (Widget* child : self->getChildren()) {
        child->tick(dt);
    }
}

Widget* compoundDescendHitTest(Widget* self, const math::FVector2& worldPos) {
    if (self == nullptr) return nullptr;
    if (!self->isVisible()) return nullptr;
    // Check children first (reverse order — last added is on top).
    const auto& kids = self->getChildren();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        Widget* hit = (*it)->hitTest(worldPos);
        if (hit) {
            return hit;
        }
    }
    // Then check self.
    math::FRectangle bounds = self->getWorldBounds();
    if (bounds.contains(worldPos)) {
        return self;
    }
    return nullptr;
}

void compoundDescendLeave(Widget* self) {
    if (self == nullptr) return;
    for (Widget* child : self->getChildren()) {
        if (child) {
            child->onMouseLeave();
        }
    }
}

Widget::Widget()
    : _position(0.0f, 0.0f)
    , _size(100.0f, 50.0f)
    , _boundsDirty(true)
    , _parent(nullptr)
    , _visible(true)
{
}

Widget::~Widget() {
    // Detach from parent FIRST. Otherwise `delete child` while still in
    // parent's `_children` (common with addChildExternal + host delete)
    // leaves a dangling entry → next tree walk / composition / layout SEGV.
    detachFromParent();

    // Phase UI-OWN-1: Widget never owns its children. Clear child→parent
    // back-pointers so survivors don't point into freed memory. Children
    // themselves are destroyed by the owning container via
    // destroyWidgetTree() (or host delete for external kids).
    //
    // R-6: removed the `_hoverWidget = nullptr` line — that field no
    // longer lives on Widget (it was removed in favor of letting
    // CompoundWidget::onMouseLeave do the cleanup when needed).
    for (Widget* child : _children) {
        if (child) {
            child->_parent = nullptr;
            child->_externallyOwned = false;
        }
    }
    _children.clear();
}

void Widget::addChild(Widget* child) {
    if (child && child->_parent != this) {
        child->detachFromParent();
        child->_parent = this;
        child->_externallyOwned = false;
        _children.push_back(child);
    }
}

void Widget::addChildExternal(Widget* child) {
    // Reference-only attach: host owns lifetime. destroyWidgetTree must
    // detach without delete (stack Modal/TextInput fixtures, Dimmer, etc.).
    if (child && child->_parent != this) {
        child->detachFromParent();
        child->_parent = this;
        child->_externallyOwned = true;
        _children.push_back(child);
    }
}

void Widget::removeChild(Widget* child) {
    if (!child) return;
    for (auto it = _children.begin(); it != _children.end(); ++it) {
        if (*it == child) {
            child->_parent = nullptr;
            child->_externallyOwned = false;
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
    // R-6: base-default behavior for any non-container widget. Containers
    // (CompoundWidget) override this to descend into children first. The
    // old code recursed into `_children` here and maintained a per-Widget
    // `_hoverWidget` pointer, but every widget paid the cost even when it
    // could never host a hovered child (leaf widgets). With the override
    // split, leaf widgets are honest: they only ever hit-test themselves.
    if (!_visible) return nullptr;
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

// --- G11: per-widget theme token overrides ---
void Widget::setStyleTokenOverride(const std::string& key, const math::FVector4& value) {
    if (key.empty()) return;
    // Allow '$foo' or bare 'foo' input — normalize to bare form so the
    // map key matches Theme's storage (which uses bare names without '$').
    const std::string norm = (key[0] == '$') ? key.substr(1) : key;
    _tokenOverrides[norm] = value;
}

void Widget::clearStyleTokenOverrides() {
    _tokenOverrides.clear();
}

bool Widget::hasStyleTokenOverride(const std::string& key) const {
    if (key.empty()) return false;
    const std::string norm = (key[0] == '$') ? key.substr(1) : key;
    return _tokenOverrides.find(norm) != _tokenOverrides.end();
}

CompoundWidget::CompoundWidget() {
}

CompoundWidget::~CompoundWidget() {
}

void CompoundWidget::performLayout() {
    compoundDescendLayout(this);
}

void CompoundWidget::tick(float dt) {
    // Cascade: own tick first, then children. Mirrors performLayout.
    compoundDescendTick(this, dt);
}

Widget* CompoundWidget::hitTest(const math::FVector2& worldPos) {
    // R-6: containers descend into children first. We do NOT maintain a
    // _hoverWidget field on the container anymore — UIManager owns the
    // single source of truth for hover and calls onMouseLeave on the
    // outgoing widget via updateHoverWidget(). The previous design kept
    // a stale pointer here that could leak cursor hints past a mouse-up
    // (B4); the onMouseLeave override below propagates mouse-leave to
    // descendants when a hover transition occurs through this container.
    return compoundDescendHitTest(this, worldPos);
}

void CompoundWidget::onMouseLeave() {
    // R-6: when the mouse leaves the container (or stops hovering a child
    // because the cursor moved outside the container), propagate the
    // leave to every descendant so transient hover/press flags on
    // InteractiveWidget children clear up. Without this, an
    // InteractiveWidget that was hovered while the cursor was inside
    // the container but whose own bounds the cursor has now left would
    // still report `_isMouseOver = true` until the next onMouseMove.
    compoundDescendLeave(this);
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
    // G12 — drop-target highlight. When this widget is the active drop
    // target during a drag session, paint a 1px accent border (matches
    // the SplitterHandle drag-reveal palette so the editor feels
    // consistent). Default visual; hosts can suppress by clearing
    // isCurrentDropTarget or by drawing their own highlight inside an
    // onDragEnter callback.
    if (_isCurrentDropTarget) {
        const math::FRectangle b = getWorldBounds();
        renderer.drawBorderRect(b,
            math::FVector4(0.40f, 0.48f, 0.62f, 1.0f), 1.0f, 2.0f);
    }
}

void Widget::renderChildren(IRenderBackend& renderer) {
    for (Widget* child : _children) {
        child->render(renderer);
    }
}

} // namespace ayt::ui