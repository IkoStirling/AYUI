#pragma once

#include "AYMathTypes.h"
#include "AYMathUtils.h"
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
    void addChild(Widget* child);
    void removeChild(Widget* child);
    void detachFromParent();

    // Spatial properties
    const math::FVector2& getPosition() const { return _position; }
    void setPosition(const math::FVector2& pos) {
        _position = pos;
        markBoundsDirty();
    }

    const math::FVector2& getSize() const { return _size; }
    void setSize(const math::FVector2& size) {
        _size = size;
        markBoundsDirty();
    }

    float getWidth() const { return _size.x; }
    float getHeight() const { return _size.y; }

    bool isLayoutPositionManaged() const { return _layoutPositionManaged; }
    void setLayoutPositionManaged(bool managed) { _layoutPositionManaged = managed; }

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
        _boundsDirty = true;
        for (Widget* child : _children) {
            if (child) {
                child->markBoundsDirty();
            }
        }
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

} // namespace ayt::ui