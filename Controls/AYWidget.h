#pragma once

#include "aymath/MathTypes.h"
#include "aymath/MathUtils.h"
#include "AYIRenderBackend.h"

#include <functional>
#include <vector>
#include <string>
#include <unordered_map>

namespace ayt::ui {
    
// Forward declarations
class Widget;

enum class UIEventType {
    UIMouseMove,
    UIMouseButtonDown,
    UIMouseButtonUp,
    UIMouseButtonClick,
    UIMouseButtonDoubleClick,
    UIKeyDown,
    UIKeyUp,
    UITextInput,
    UIFocus,
    UIBlur,
    UISizeChanged,
    UIVisibilityChanged
};

struct UIEvent {
    UIEventType type;
    math::FVector2 mousePos;
    int mouseButton;
    int keyCode;
    wchar_t textChar;
    bool handled;
    Widget* target;

    UIEvent() : type(UIEventType::UIMouseMove), mouseButton(0), keyCode(0),
                textChar(0), handled(false), target(nullptr) {}
};

struct UIMouseEvent {
    math::FVector2 mousePos;
    int mouseButton;

    UIMouseEvent(const math::FVector2& pos, int btn = 0)
        : mousePos(pos), mouseButton(btn) {}
};

enum class UiCursorHint {
    Default,
    Hand,
    SizeHorizontal,
    SizeVertical,
    Move,
};

class Widget {
public:
    Widget();
    virtual ~Widget();

    // Tree operations
    Widget* getParent() const { return _parent; }
    const std::vector<Widget*>& getChildren() const { return _children; }
    // addChild: register the child in this widget's tree. ~Widget() does NOT
    // delete children — destruction is the caller's responsibility. Use
    // destroyWidgetTree() to recursively destroy a heap-allocated tree that
    // was built via WidgetFactory / UILayoutLoader.
    void addChild(Widget* child);
    // addChildExternal: same semantics as addChild today (reference only,
    // never deleted by parent). Kept as a separate name to document caller
    // intent for stack-allocated or externally-owned children (e.g. test
    // fixtures). The parent will never delete the child regardless of which
    // method was used to attach it.
    void addChildExternal(Widget* child);
    void removeChild(Widget* child);
    void detachFromParent();

    // Spatial properties
    const math::FVector2& getPosition() const { return _position; }
    void setPosition(const math::FVector2& pos) {
        // Phase UI-PERF-1: skip the (potentially full-subtree) dirty
        // propagation when the new value equals the old. Layout code calls
        // setPosition/setSize on every child every frame; without this
        // guard each frame re-dirties the whole subtree even when nothing
        // actually changed.
        if (_position.x == pos.x && _position.y == pos.y) {
            return;
        }
        _position = pos;
        markBoundsDirty();
    }

    const math::FVector2& getSize() const { return _size; }
    void setSize(const math::FVector2& size) {
        if (_size.x == size.x && _size.y == size.y) {
            return;
        }
        _size = size;
        markBoundsDirty();
    }

    float getWidth() const { return _size.x; }
    float getHeight() const { return _size.y; }

    bool isLayoutPositionManaged() const { return _layoutPositionManaged; }
    void setLayoutPositionManaged(bool managed) { _layoutPositionManaged = managed; }

    // Layout-managed size: when true, parent layout may overwrite _size on
    // each performLayout() pass. When false, _size is preserved verbatim.
    // Defaults to true so a freshly-added child participates in the layout.
    bool isLayoutSizeManaged() const { return _layoutSizeManaged; }
    void setLayoutSizeManaged(bool managed) { _layoutSizeManaged = managed; }

    void bringToFront();

    // Bounds
    const math::FRectangle& getBounds() const { return _bounds; }
    math::FRectangle getWorldBounds() const;

    // Hit test - overridden by subclasses
    virtual Widget* hitTest(const math::FVector2& worldPos);

    // Mouse leave notification (called when mouse leaves this widget)
    virtual void onMouseLeave();

    // Visibility
    bool isVisible() const { return _visible; }
    void setVisible(bool visible) { _visible = visible; }

    // Event handling - override in subclasses
    virtual bool onMouseMove(const UIMouseEvent& e);
    virtual bool onMouseButtonDown(const UIMouseEvent& e);
    virtual bool onMouseButtonUp(const UIMouseEvent& e);
    virtual bool onKeyDown(int keyCode);
    virtual bool onKeyUp(int keyCode);
    virtual bool onTextInput(wchar_t ch);

    virtual UiCursorHint getCursorHint() const { return UiCursorHint::Default; }

    // Event propagation
    void addEventHandler(UIEventType type, std::function<void(UIEvent&)> handler);
    void removeEventHandlers(UIEventType type);

    // Bubble event up to parent
    void bubbleEvent(UIEvent& e);

    // Layout
    virtual void performLayout() {}

    // Style
    void setStyleId(const std::string& id) { _styleId = id; }
    const std::string& getStyleId() const { return _styleId; }

    // ID
    void setId(const std::string& id) { _id = id; }
    const std::string& getId() const { return _id; }

    // Rendering - Widget calls IRenderBackend
    void setRenderBackend(IRenderBackend* backend) { _renderBackend = backend; }
    IRenderBackend* getRenderBackend() const { return _renderBackend; }

    virtual void render(IRenderBackend& renderer);
    virtual void renderChildren(IRenderBackend& renderer);

    void markBoundsDirty() {
        // Phase UI-PERF-1: lazy-propagate. Previously this recursed through
        // every descendant (O(N) per call), and VBox/HBox layout calls
        // setPosition/setSize on each child every performLayout pass, so
        // the whole tree was re-dirties every frame even when most nodes
        // didn't move. With lazy propagate, only the changed widget is
        // flagged here; getWorldBounds() walks UP the parent chain — if
        // any ancestor is dirty, the current node recomputes too.
        _boundsDirty = true;
    }

protected:
    // Override in subclasses to implement specific rendering
    virtual void onRender(IRenderBackend& renderer) {}

protected:
    math::FVector2 _position;
    math::FVector2 _size;
    math::FRectangle _bounds;
    bool _boundsDirty;

    Widget* _parent;
    std::vector<Widget*> _children;

    bool _visible;
    bool _layoutPositionManaged = true;
    bool _layoutSizeManaged = true;
    std::string _styleId;
    std::string _id;

    Widget* _hoverWidget;  // 当前悬停的子控件

    std::unordered_map<UIEventType, std::vector<std::function<void(UIEvent&)>>> _eventHandlers;
    IRenderBackend* _renderBackend = nullptr;

    void updateWorldBounds();
    math::FVector2 getWorldPosition() const;
};

class CompoundWidget : public Widget {
public:
    CompoundWidget();
    virtual ~CompoundWidget();

    void performLayout() override;

    virtual void layoutChildren() {}

protected:
    void onChildAdded(Widget* child);
    void onChildRemoved(Widget* child);
};

// =============================================================================
// Single-owner destruction helper (Phase UI-OWN-1).
// Recursively destroys a widget tree built via WidgetFactory / UILayoutLoader.
// Convention: factory-allocated trees are released ONLY through this helper
// (or via std::unique_ptr with a deleter that calls it). Calling delete on
// any widget in the tree except the root would orphan its descendants.
// Calling delete on a stack-allocated widget is fine — its child widgets
// are not destroyed by ~Widget() because the parent never owns them.
// =============================================================================
inline void destroyWidgetTree(Widget* root) {
    if (root == nullptr) {
        return;
    }
    // Snapshot children before recursing — _children is not mutated during
    // destruction, but make a local copy so a misbehaving subclass that
    // clears _children in a destructor can't corrupt the iteration.
    std::vector<Widget*> children = root->getChildren();
    for (Widget* child : children) {
        destroyWidgetTree(child);
    }
    delete root;
}

} // namespace ayt::ui