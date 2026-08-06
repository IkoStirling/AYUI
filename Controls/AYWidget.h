#pragma once

#include "aymath/MathTypes.h"
#include "aymath/MathUtils.h"
#include "IAYRenderBackend.h"
#include "AYDragDrop.h"

#include <functional>
#include <vector>
#include <string>
#include <unordered_map>

namespace ayt::ui {
    
// Forward declarations
class Widget;
class UIManager;

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

// PR-B3 — wheel event. Positive deltaY = scroll DOWN (content moves up);
// deltaX is unused today (only V scrollbars are wired) but kept in the
// event so horizontal wheels (Shift+wheel on Win32, native tilt on macOS)
// can be routed later without breaking the API.
//
// We don't reuse UIMouseEvent because wheel events have no button code
// (mouseButton would be a meaningless 0/2/3 convention) and the routing
// semantics differ (no capture, no focus-only semantics, no click
// follow-up).
struct UIMouseWheelEvent {
    math::FVector2 mousePos;
    float deltaY;

    UIMouseWheelEvent(const math::FVector2& pos, float dy)
        : mousePos(pos), deltaY(dy) {}
};

enum class UiCursorHint {
    Default,
    Hand,
    // PR-B1: split SizeHorizontal/SizeVertical into SizeWe/SizeNs so the
    // names match Windows cursor conventions. Existing call sites use
    // SizeHorizontal/SizeVertical (legacy aliases) and remain compatible.
    SizeHorizontal,  // legacy alias for SizeWe
    SizeVertical,    // legacy alias for SizeNs
    SizeWe,          // ⇔ ↔ (left-right edge or horizontal splitter)
    SizeNs,          // ⇕ (top-bottom edge or vertical splitter)
    SizeNwse,        // ⤡ (NW-SE diagonal: SE / NW corners)
    SizeNesw,        // ⤢ (NE-SW diagonal: NE / SW corners)
    Move,
    Beam,
};

class Widget {
public:
    Widget();
    virtual ~Widget();

    // G12 — UIManager is the only class that fires the drag/drop
    // callbacks (beginDrag → source->_onDragStart, updateDrag →
    // target->_onDragEnter/Leave, endDrag → target->_onDrop +
    // source->_onDragEnd). Friend grant keeps the protected
    // _onDrag*/_drop callback fields from leaking into the public
    // surface; mirrors the same UIManager-friend pattern used for the
    // protected focus/capture helpers in earlier phases.
    friend class UIManager;

    // Tree operations
    Widget* getParent() const { return _parent; }
    const std::vector<Widget*>& getChildren() const { return _children; }
    // addChild: register the child in this widget's tree. ~Widget() does NOT
    // delete children — destruction is the caller's responsibility. Use
    // destroyWidgetTree() to recursively destroy a heap-allocated tree that
    // was built via WidgetFactory / UILayoutLoader.
    void addChild(Widget* child);
    // addChildExternal: attach without transferring destroy ownership.
    // destroyWidgetTree() must detach these without delete — stack fixtures
    // and host-owned Modal/Dimmer rely on this (batch SEGV if violated).
    void addChildExternal(Widget* child);
    void removeChild(Widget* child);
    void detachFromParent();

    // True when attached via addChildExternal (host owns lifetime).
    bool isExternallyOwned() const { return _externallyOwned; }

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

    // Identity for layout (HBox slot width / hit priority). Prefer this over
    // dynamic_cast<SplitterHandle*> — RTTI failure would treat a splitter as a
    // fill-width slot, making the hover band hundreds of px wide so "leave"
    // never fires and the accent stays lit.
    virtual bool isSplitterHandle() const { return false; }

    // =================================================================
    // Phase C (S4): UIManager uses this to decide whether to enable
    // AYDevice::TextInput (the IME gate) when focus changes. Default false;
    // TextInput + TextArea::TextDocument override true in PR-2. We prefer
    // this virtual over dynamic_cast<TextInput*> because TextArea's IME
    // events flow through its inner TextDocument, not the outer
    // CompoundWidget — a plain dynamic_cast<TextInput*> would miss it.
    // =================================================================
    virtual bool isTextEditingWidget() const { return false; }

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

    // PR-B3 — wheel routing. Default returns false (no scrollable
    // behaviour). ScrollView / ListView override this and return true
    // when the wheel actually moved content (caller uses the bool to
    // decide whether to suppress a parent scroll container).
    virtual bool onMouseWheel(const UIMouseWheelEvent& e) {
        AYUNREFERENCED_PARAM(e);
        return false;
    }

    virtual UiCursorHint getCursorHint() const { return UiCursorHint::Default; }

    // Event propagation
    void addEventHandler(UIEventType type, std::function<void(UIEvent&)> handler);
    void removeEventHandlers(UIEventType type);

    // Bubble event up to parent
    void bubbleEvent(UIEvent& e);

    // Layout
    virtual void performLayout() {}

    // Per-frame tick. UIManager::update(dt) drives the root widget which
    // cascades into CompoundWidget children. Default is a no-op; widgets
    // with time-based behavior (SplitterHandle's hover reveal delay) override.
    // Mirrors the performLayout cascade: CompoundWidget::tick walks children.
    virtual void tick(float dt) { AYUNREFERENCED_PARAM(dt); }

    // Style
    void setStyleId(const std::string& id) { _styleId = id; }
    const std::string& getStyleId() const { return _styleId; }

    // =================================================================
    // G11 — per-widget token overrides. When the active theme resolves
    // a `$tokenName` reference in this widget's resolved style, this
    // map's entry (if any) wins over the theme value. Lets hosts say
    // "this one button is red" without forking the entire theme.
    //
    // Overrides apply ONLY to the resolveStyle() path. The StyleSheet
    // stores literal FVector4 values, so by the time a style is loaded
    // from JSON any $token reference has been expanded at load time;
    // a widget that wants different colors must provide them via
    // setStyleTokenOverride() AND have the resolver consult this map
    // (see AYStyle.cpp::resolveStyle + Theme::resolveColor).
    // =================================================================
    void setStyleTokenOverride(const std::string& key, const math::FVector4& value);
    void clearStyleTokenOverrides();
    const std::unordered_map<std::string, math::FVector4>& getStyleTokenOverrides() const {
        return _tokenOverrides;
    }
    bool hasStyleTokenOverride(const std::string& key) const;

    // =================================================================
    // G12 — Drag & Drop API.
    // =================================================================
    // DragSource side: a widget can opt in to be a drag source via
    // setDraggable(true). The host (typically the widget's own
    // onMouseButtonDown handler) calls UIManager::beginDrag(this, ...)
    // when it decides user intent warrants a drag. The widget's
    // payload is read from getDragPayload() inside beginDrag; the
    // host sets it via setDragPayload() before the gesture starts.
    void setDraggable(bool d) { _draggable = d; }
    bool isDraggable() const  { return _draggable; }
    void setDragPayload(const DragPayload& p) { _dragPayload = p; }
    const DragPayload& getDragPayload() const { return _dragPayload; }
    void setOnDragStart(std::function<void()> cb)   { _onDragStart = std::move(cb); }
    void setOnDragEnd  (std::function<void(bool /*accepted*/)> cb) {
        _onDragEnd = std::move(cb);
    }

    // DropTarget side: a widget can opt in to receive drops via
    // setAcceptDrops(true). UIManager walks up from the widget under
    // the cursor to find the nearest accepting ancestor; that ancestor
    // gets onDragEnter + onDragLeave + onDrop callbacks.
    void  setAcceptDrops(bool a) { _acceptDrops = a; }
    bool  isAcceptDrops() const  { return _acceptDrops; }
    void  setOnDrop      (std::function<void(const DragPayload&)> cb) {
        _onDrop = std::move(cb);
    }
    void  setOnDragEnter (std::function<void(const DragPayload&)> cb) {
        _onDragEnter = std::move(cb);
    }
    void  setOnDragLeave (std::function<void()> cb) { _onDragLeave = std::move(cb); }

    // G12 internal — used by UIManager to toggle the drop-target highlight.
    // Not part of the host-facing API; public only because UIManager is
    // friended via the .cpp implementation.
    bool isCurrentDropTarget() const            { return _isCurrentDropTarget; }
    void setCurrentDropTarget(bool t)          { _isCurrentDropTarget = t; }

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

    // Set by addChildExternal; cleared on detach. destroyWidgetTree skips
    // delete for these nodes (host/stack owns them).
    bool _externallyOwned = false;

    bool _visible;
    bool _layoutPositionManaged = true;
    bool _layoutSizeManaged = true;
    std::string _styleId;
    std::string _id;
    // G11 — per-widget token overrides. Keyed by bare token name (no
    // leading '$'). Resolved during StyleSheet parsing AND during
    // resolveStyle() so a JSON-loaded style with `"$color.bg": "..."`
    // AND a programmatic setStyleTokenOverride() both flow through.
    std::unordered_map<std::string, math::FVector4> _tokenOverrides;

    // G12 — Drag & Drop state. Empty std::function defaults pay no
    // runtime cost; only widgets that opt in (setDraggable / setAcceptDrops
    // + a callback) allocate the closure capture. The "horizontal feature"
    // argument against adding these to base: it's deliberate — every
    // widget needs to be reachable as a drag source or target without
    // deriving a new class, and a mixin would force host boilerplate that
    // duplicates this exact field set per subclass.
    bool _draggable = false;
    bool _acceptDrops = false;
    DragPayload _dragPayload;
    std::function<void()>                       _onDragStart;
    std::function<void(bool /*accepted*/)>      _onDragEnd;
    std::function<void(const DragPayload&)>     _onDrop;
    std::function<void(const DragPayload&)>     _onDragEnter;
    std::function<void()>                       _onDragLeave;
    bool _isCurrentDropTarget = false;

    // R-6: removed `Widget::_hoverWidget` field. The previous design stored
    // "the currently hovered child" on every Widget, but the only purpose was
    // to call onMouseLeave() on the previous child when hitTest moved away.
    // UIManager already does that via updateHoverWidget() (it owns the single
    // source of truth for hover). Leaf widgets never needed the field.
    // CompoundWidget::onMouseLeave now propagates mouse-leave to all
    // descendants so a stale hover state on a child can't survive an
    // external mouse-out event.

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
    void tick(float dt) override;

    // R-6: CompoundWidget overrides hitTest to descend into children, and
    // overrides onMouseLeave to notify every descendant. The previous
    // base-default implementation worked for raw `Widget` containers but
    // could not express "the container has no children, hit self only"
    // cleanly — every widget stored a _hoverWidget field even when it
    // could never have one. Splitting the two cases makes the data model
    // honest (only containers can host a hover child) and fixes B4 (mouse
    // leave now reaches every descendant without relying on UIManager
    // re-picking the cursor hint after mouse-up).
    Widget* hitTest(const math::FVector2& worldPos) override;
    void onMouseLeave() override;

    virtual void layoutChildren() {}

protected:
    void onChildAdded(Widget* child);
    void onChildRemoved(Widget* child);
};

// ============================================================================
// Phase B (S3): shared compound-descent helpers used by BOTH
// CompoundWidget and CompoundFocusableWidget. Anonymous-namespace helpers in
// AYWidget.cpp own the implementation; this block is the public linkage.
//
// Why a shared helper (and not making CompoundFocusableWidget inherit
// CompoundWidget)? Multi-inheritance would form a diamond under Widget (both
// bases inherit Widget directly). Single inheritance from FocusableWidget +
// shared helpers is diamond-free and behavior-identical.
// ============================================================================
void compoundDescendLayout(Widget* self);
void compoundDescendTick(Widget* self, float dt);
Widget* compoundDescendHitTest(Widget* self, const math::FVector2& worldPos);
void compoundDescendLeave(Widget* self);

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
    // Capture before detachFromParent → removeChild clears the flag.
    const bool externalRoot = root->isExternallyOwned();

    // Detach from parent BEFORE delete so the parent's `_children` never
    // holds a dangling pointer. Callers that destroy an overlay popup
    // (or any still-parented subtree) without an explicit removeChild —
    // e.g. UIManager::loadFromString — would otherwise leave stale
    // entries that crash on the next walk / shutdown (0xC0000005).
    root->detachFromParent();

    // Snapshot children before recursing — detachFromParent on each child
    // mutates this node's `_children`, so iterate a local copy.
    std::vector<Widget*> children = root->getChildren();
    for (Widget* child : children) {
        if (child == nullptr) {
            continue;
        }
        // Host/stack-owned (addChildExternal): detach only — never delete.
        // Factory/heap subtrees remain recursively destroyed.
        if (child->isExternallyOwned()) {
            child->detachFromParent();
            continue;
        }
        destroyWidgetTree(child);
    }
    if (externalRoot) {
        return;
    }
    delete root;
}

} // namespace ayt::ui