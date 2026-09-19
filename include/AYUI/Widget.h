#pragma once

#include "AYUI/Accessibility.h"

#include "AYMath/MathTypes.h"
#include "AYMath/MathUtils.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/DisplayList.h"
#include "AYUI/DragDrop.h"
#include "AYUI/Tween.h"

#include <algorithm>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <initializer_list>
#include <utility>

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

// Pixel-retained subtree policy. Disabled preserves the display-list/immediate
// path. Always explicitly caches this widget plus descendants in a Layer.
// Auto promotes only sufficiently complex, stable, non-trivial subtrees and
// demotes them again after repeated invalidation.
enum class LayerCachePolicy : uint8_t {
    Disabled,
    Always,
    Auto
};

struct AutoLayerCacheTuning {
    float minimumArea = 4096.0f;
    size_t minimumCommands = 12u;
    uint32_t promotionStableFrames = 3u;
    uint32_t demotionInvalidFrames = 3u;
    // EMA weight of the newest frame. Higher values react faster to churn.
    float sampleWeight = 0.25f;
    // Estimated command submissions saved per frame after accounting for
    // repaint probability and the one Layer composite.
    float minimumExpectedCommandSavings = 2.0f;
};

struct AutoLayerCacheMetrics {
    bool active = false;
    uint32_t stableFrames = 0u;
    uint32_t unstableFrames = 0u;
    uint64_t promotions = 0u;
    uint64_t demotions = 0u;
    float invalidationRate = 1.0f;
    float estimatedCommands = 0.0f;
    float expectedCommandSavings = 0.0f;
};

// Responsive placement for a free-positioned child. Anchor coordinates are
// normalized to the parent rectangle. offsetMin/offsetMax are signed pixel
// deltas from those anchor points to the child's near/far edges:
//
//   childMin = parentSize * anchorMin + offsetMin
//   childMax = parentSize * anchorMax + offsetMax
//
// Equal min/max values keep the child at a fixed size relative to one parent
// point; a range stretches the child as the parent changes size. Pivot is
// retained for authoring/transform-origin semantics and does not change the
// edge equation above.
struct AnchorLayout {
    math::FVector2 anchorMin{0.0f, 0.0f};
    math::FVector2 anchorMax{0.0f, 0.0f};
    math::FVector2 offsetMin{0.0f, 0.0f};
    math::FVector2 offsetMax{0.0f, 0.0f};
    math::FVector2 pivot{0.5f, 0.5f};
};

// Width-query rules evaluated against the direct parent's logical DIP width.
// maxParentWidth <= 0 means unbounded. Rules can alter effective visibility
// and, for free-positioned widgets, substitute an AnchorLayout. The authored
// visibility/anchor remain untouched, so leaving a breakpoint is lossless.
enum class ResponsiveVisibility {
    Inherit,
    Visible,
    Hidden
};

struct ResponsiveLayoutRule {
    std::string name;
    float minParentWidth = 0.0f;
    float maxParentWidth = 0.0f;
    ResponsiveVisibility visibility = ResponsiveVisibility::Inherit;
    bool overrideAnchors = false;
    AnchorLayout anchors;
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
    // Reorder an already-attached child. Returns false if `child` is not
    // ours or `index` is out of range. Used by layout editors / dock tools.
    bool moveChildToIndex(Widget* child, size_t index);

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
            // An explicit setter still takes ownership from an in-flight
            // tween even when the supplied value equals this frame's sample.
            cancelPositionAnimation(true);
            return;
        }
        // A direct set cancels any in-flight position tween — the caller
        // took over (mirrors setOpacity).
        cancelPositionAnimation(true);
        _position = pos;
        markBoundsDirty();
        // AYUI-DirtyRect-2026-08-26: position change can move our painted
        // pixels on the parent's surface. The widget must re-render this
        // frame; also propagate to ancestors so the old slot (if any) is
        // repainted as background. markDirty() (default = invalid rect)
        // marks the whole widget and propagates a "you must paint this
        // frame" flag up to parent — see Widget::markDirty().
        markDirty();
        // AYUI-Perf-2026-08-26 (Batch C rebase fix): invalidate every
        // descendant's cached world-bounds. The parent's world origin
        // shifted; without this, children would hand back a stale
        // cached rect on the next getWorldBounds call until each child
        // itself moved.
        markDescendantsBoundsDirty();
    }

    const math::FVector2& getSize() const { return _size; }
    void setSize(const math::FVector2& size) {
        if (_size.x == size.x && _size.y == size.y) {
            return;
        }
        _size = size;
        markBoundsDirty();
        // AYUI-DirtyRect-2026-08-26: same reasoning as setPosition — a
        // size change reshapes our paint + the parent's render regions.
        markDirty();
        // AYUI-Perf-2026-08-26 (Batch C rebase fix): a size change
        // shifts the rect's max corner, so descendants whose layout
        // derived offsets from this widget's size also need their
        // caches invalidated.
        markDescendantsBoundsDirty();

        // Anchors are a direct parent-size constraint, not a viewport-only
        // layout effect. UIManager deliberately skips a full performLayout
        // pass while the client size is unchanged, so waiting for that pass
        // leaves anchored children stale when an editor, animation, or host
        // resizes an inner free-layout Panel. Resolve direct anchored
        // children now; child setSize() cascades the same rule to nested
        // anchored descendants. Structured layout children keep their
        // position/size-managed flags and are therefore unaffected.
        for (Widget* child : _children) {
            if (child == nullptr) continue;
            if (child->hasResponsiveLayoutRules()) {
                child->applyResponsiveLayout(_size);
            } else if (child->hasAnchorLayout() &&
                       !child->isLayoutPositionManaged() &&
                       !child->isLayoutSizeManaged()) {
                child->applyAnchorLayout(_size);
            }
        }
    }

    // PR-B3 hotfix — scrollable content size separate from the widget's
    // *current* render size. By default this is just getSize(); VBox /
    // HBox override it to compute the natural stacked size of visible
    // children so a ScrollView wrapping a content-fills-viewport VBox
    // can still report a scrollable extent (the visible page's natural
    // height, not the VBox's viewport-bound height).
    virtual math::FVector2 getPreferredContentSize() const { return _size; }

    float getWidth() const { return _size.x; }
    float getHeight() const { return _size.y; }

    bool isLayoutPositionManaged() const { return _layoutPositionManaged; }
    void setLayoutPositionManaged(bool managed) { _layoutPositionManaged = managed; }

    // Layout-managed size: when true, parent layout may overwrite _size on
    // each performLayout() pass. When false, _size is preserved verbatim.
    // Defaults to true so a freshly-added child participates in the layout.
    bool isLayoutSizeManaged() const { return _layoutSizeManaged; }
    void setLayoutSizeManaged(bool managed) { _layoutSizeManaged = managed; }

    // UE/RectTransform-style responsive anchors for children of free-layout
    // containers. Structured parents (Box/Grid/etc.) retain precedence via
    // their own layout pass and should leave this disabled.
    bool hasAnchorLayout() const { return _anchorLayoutEnabled; }
    const AnchorLayout& getAnchorLayout() const { return _anchorLayout; }
    void setAnchorLayout(const AnchorLayout& layout);
    void clearAnchorLayout();

    // Change the anchor/pivot while preserving the widget's current visual
    // rectangle. The widget must already be attached to its intended parent.
    void setAnchorLayoutPreservingRect(const math::FVector2& anchorMin,
                                       const math::FVector2& anchorMax,
                                       const math::FVector2& pivot =
                                           math::FVector2(0.5f, 0.5f));

    // Recompute edge offsets after an editor/host directly changes position
    // or size. applyAnchorLayout() is the parent-layout side of the contract.
    void refreshAnchorOffsetsFromCurrentRect();
    void applyAnchorLayout(const math::FVector2& parentSize);

    const std::vector<ResponsiveLayoutRule>& getResponsiveLayoutRules() const {
        return _responsiveLayoutRules;
    }
    bool hasResponsiveLayoutRules() const {
        return !_responsiveLayoutRules.empty();
    }
    void setResponsiveLayoutRules(std::vector<ResponsiveLayoutRule> rules);
    void clearResponsiveLayoutRules();
    int getActiveResponsiveRuleIndex() const {
        return _activeResponsiveRuleIndex;
    }
    const ResponsiveLayoutRule* getActiveResponsiveLayoutRule() const;
    void applyResponsiveLayout(const math::FVector2& parentSize);

    void bringToFront();

    // Bounds
    const math::FRectangle& getBounds() const { return _bounds; }
    math::FRectangle getWorldBounds() const;

    // ====================================================================
    // Container contract for clip + offset + hitTest.
    // All container widgets (CompoundWidget, ScrollView, ListView, Window,
    // HBox, TextArea, TreeView) MUST honour this 3-piece contract in
    // lockstep:
    //   1) renderChildren: pushClip(this->getClientRect()) before painting
    //      any child that has scroll offset applied or that may overflow
    //      the visible area; popClip() after.
    //   2) hitTest: gate child descent with this->getClientRect() so a
    //      scrolled-off child cannot be clicked at its stale on-screen
    //      slot. The CompoundWidget default does NOT do this — a derived
    //      container with chrome (scrollbars / title bar) MUST override
    //      and gate with the helper compoundDescendHitTestClipped.
    //   3) scrolling: route every scrollOffset mutation through
    //      ScrollableWidget::scrollBy() (the canonical clamp) or the
    //      pure-function clampScrollOffset() (Window body). Do not
    //      hand-roll std::clamp at the call site.
    //
    // Default getClientRect() returns getWorldBounds(). Override only when
    // the container has chrome that eats into the world bounds
    // (scrollbars, title bar, etc.).
    // ====================================================================
    virtual math::FRectangle getClientRect() const { return getWorldBounds(); }

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
    bool isVisible() const {
        if (_responsiveVisibility == ResponsiveVisibility::Visible) return true;
        if (_responsiveVisibility == ResponsiveVisibility::Hidden) return false;
        return _visible;
    }
    // Persistence and authoring inspect the declared value, while rendering,
    // hit testing and parent layout consume isVisible()'s resolved value.
    bool isAuthoredVisible() const { return _visible; }
    void setVisible(bool visible) {
        if (_visible == visible) {
            return;
        }
        _visible = visible;
        // Both transitions must dirty the parent. Hiding a previously
        // painted child otherwise leaves its old pixels on the surface.
        markDirty();
    }

    // Event handling - override in subclasses
    virtual bool onMouseMove(const UIMouseEvent& e);
    virtual bool onMouseButtonDown(const UIMouseEvent& e);
    virtual bool onMouseButtonUp(const UIMouseEvent& e);
    // Layered hosts can opt into retrying a pointer-down behind an unhandled
    // descendant. Ordinary Widget trees retain the historical single-target
    // behavior. A blocking boundary consumes the event even when the leaf did
    // not handle it (modal/full-screen input shields use this contract).
    virtual bool retriesUnhandledPointerWithinChildren() const { return false; }
    virtual bool allowsUnhandledPointerRetryBehind() const { return false; }
    virtual bool blocksLowerPointerInput() const { return false; }
    // Called when the host must terminate pointer capture without a real
    // release event (for example, when a window loses focus). The default
    // preserves the historical behaviour for drag controls by synthesizing
    // a release outside the widget. Transactional controls can override this
    // to roll their operation back instead of committing at a fake position.
    virtual void onCaptureCancelled();
    virtual bool onKeyDown(int keyCode);
    virtual bool onKeyUp(int keyCode);
    virtual bool onTextInput(wchar_t ch);
    // Atomic committed-text event. The default preserves compatibility with
    // existing widgets by dispatching each wchar_t through onTextInput().
    // Editors override this so a surrogate pair or IME chunk is one edit and
    // one undo record.
    virtual bool onTextInputText(const std::wstring& text);

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
    // cascades into CompoundWidget children. A subclass override MUST call
    // its direct base tick before advancing custom state; virtual dispatch
    // reaches the override from the parent cascade, so omitting that call
    // would stall common opacity/position animation and child traversal.
    virtual void tick(float dt);

    // ---------------------------------------------------------------------
    // Opacity / fade transitions (PR-anim). Widget::render pushes _opacity
    // into the renderer's opacity stack, so the whole subtree fades as one
    // unit (children multiply against the parent's alpha). With no fade
    // active _opacity stays 1.0 and rendering is byte-identical to the
    // pre-opacity code path.
    // ---------------------------------------------------------------------
    void setOpacity(float opacity);
    float getOpacity() const { return _opacity; }
    // Tweens _opacity from its current value to `to` over durationMs,
    // driven by tick(dt). durationMs <= 0 snaps immediately. Curve table
    // mirrors MockRenderer's animation handles (same easing).
    void animateOpacity(float to, float durationMs,
                        AnimationCurve curve = AnimationCurve::EaseOut);
    void animateOpacity(float to, const AnimationOptions& options);
    bool isOpacityAnimating() const { return _opacityAnim.active; }

    // UI-anim cut 2: position tween. Tweens _position from its current
    // value to `to` over durationMs, driven by tick(dt). NOTE: only for
    // overlay / manually-positioned children (popups, tooltips) — widgets
    // inside a laid-out tree get their position overwritten by the parent's
    // performLayout(), so a position tween is a no-op there by design.
    void animatePositionTo(const math::FVector2& to, float durationMs,
                           AnimationCurve curve = AnimationCurve::EaseOut);
    void animatePositionTo(const math::FVector2& to,
                           const AnimationOptions& options);
    bool isPositionAnimating() const { return _posAnim.active; }

    // Common playback controls for the built-in Widget tracks. Completion
    // and cancellation callbacks are supplied through AnimationOptions.
    void pauseAnimations();
    void resumeAnimations();
    void cancelAnimations(bool snapToEnd = false);
    bool areAnimationsPaused() const {
        return (_opacityAnim.active && _opacityAnim.paused)
            || (_posAnim.active && _posAnim.paused);
    }

    // Style
    void setStyleId(const std::string& id) {
        // AYUI-DirtyRect-2026-08-26: style id change swaps which colors
        // and metrics onRender reads from the StyleSheet — must re-render.
        if (_styleId == id) {
            return;
        }
        _styleId = id;
        markDirty();
    }
    const std::string& getStyleId() const { return _styleId; }

    // Declarative interaction metadata. These strings describe the contract
    // between a layout and its host controller; Widget deliberately does not
    // know how controllers are instantiated. UILayoutLoader can resolve the
    // names against callbacks registered by a host, while authoring tools can
    // round-trip them even when no game/controller code is present.
    void setControllerId(const std::string& id) { _controllerId = id; }
    const std::string& getControllerId() const { return _controllerId; }
    void setEventBinding(const std::string& eventName,
                         const std::string& handlerName) {
        if (eventName.empty()) return;
        if (handlerName.empty()) {
            _eventBindings.erase(eventName);
        } else {
            _eventBindings[eventName] = handlerName;
        }
    }
    void clearEventBinding(const std::string& eventName) {
        _eventBindings.erase(eventName);
    }
    const std::string& getEventBinding(const std::string& eventName) const {
        static const std::string empty;
        const auto it = _eventBindings.find(eventName);
        return it != _eventBindings.end() ? it->second : empty;
    }
    const std::unordered_map<std::string, std::string>& getEventBindings() const {
        return _eventBindings;
    }

    // Source localization metadata. Runtime widgets keep the resolved text,
    // while these keys preserve the authoring contract for JSON round-trips.
    void setLocalizationKey(const std::string& property,
                            const std::string& key) {
        if (key.empty()) _localizationKeys.erase(property);
        else _localizationKeys[property] = key;
    }
    const std::string& getLocalizationKey(const std::string& property) const {
        static const std::string empty;
        const auto found = _localizationKeys.find(property);
        return found != _localizationKeys.end() ? found->second : empty;
    }
    void setLocalizationKeys(const std::string& property,
                             std::vector<std::string> keys) {
        if (keys.empty()) _localizationKeyLists.erase(property);
        else _localizationKeyLists[property] = std::move(keys);
    }
    const std::vector<std::string>& getLocalizationKeys(
        const std::string& property) const {
        static const std::vector<std::string> empty;
        const auto found = _localizationKeyLists.find(property);
        return found != _localizationKeyLists.end() ? found->second : empty;
    }

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
    // Resolve a token through this widget and its ancestors. The closest
    // declaration wins, matching CSS custom-property inheritance.
    bool findInheritedStyleTokenOverride(const std::string& key,
                                         math::FVector4& outValue) const;

    // Accessibility metadata. Roles/labels left at their defaults are
    // inferred from concrete control types when UIManager snapshots the
    // semantic tree. Explicit metadata always wins over inference.
    uint64_t getAccessibilityId() const { return _accessibilityId; }
    void setAccessibilityRole(AccessibilityRole role) {
        _accessibilityRole = role;
        _accessibilityRoleExplicit = true;
    }
    AccessibilityRole getAccessibilityRole() const { return _accessibilityRole; }
    bool hasExplicitAccessibilityRole() const { return _accessibilityRoleExplicit; }
    void clearAccessibilityRole() {
        _accessibilityRole = AccessibilityRole::Generic;
        _accessibilityRoleExplicit = false;
    }
    void setAccessibilityLabel(const std::wstring& label) { _accessibilityLabel = label; }
    const std::wstring& getAccessibilityLabel() const { return _accessibilityLabel; }
    void setAccessibilityDescription(const std::wstring& description) {
        _accessibilityDescription = description;
    }
    const std::wstring& getAccessibilityDescription() const {
        return _accessibilityDescription;
    }
    void setAccessibilityValue(const std::wstring& value) { _accessibilityValue = value; }
    const std::wstring& getAccessibilityValue() const { return _accessibilityValue; }
    void setAccessibilityHidden(bool hidden) { _accessibilityHidden = hidden; }
    bool isAccessibilityHidden() const { return _accessibilityHidden; }
    void setAccessibilityLiveSetting(AccessibilityLiveSetting setting) {
        _accessibilityLiveSetting = setting;
    }
    AccessibilityLiveSetting getAccessibilityLiveSetting() const {
        return _accessibilityLiveSetting;
    }
    void setAccessibilityActionHandler(
        std::function<bool(AccessibilityAction)> handler) {
        _accessibilityActionHandler = std::move(handler);
    }

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
    // ====================================================================
    // AYUI-Audit-2026-08-26: per-kind acceptance API. setAcceptDrops(true)
    // opts in to all kinds (legacy behavior). To restrict which kinds this
    // target honors, also call setAcceptDropKinds({"FileList", "Image"})
    // -- an empty list (default) means "accept any kind", matching the
    // previous opaque any-accepts-anything behavior so existing hosts do
    // not silently start rejecting drops. UIManager::updateDrag /
    // ::endDrag consult acceptsKind() while walking for targets; a kind
    // not in the list is invisible to the drag system (no enter/leave
    // highlight, no onDrop callback).
    //
    // Vector-over-unordered-set rationale: drop-accept lists are tiny
    // (hosts typically enumerate 1-3 kinds per target), and a linear
    // scan keeps the API trivially copyable without a custom hash.
    // ====================================================================
    void  setAcceptDropKinds(std::vector<std::string> kinds) {
        _acceptDropKinds = std::move(kinds);
    }
    const std::vector<std::string>& getAcceptDropKinds() const {
        return _acceptDropKinds;
    }
    // True when kinds list is empty (accept any) OR kinds contains `kind`.
    bool  acceptsKind(const std::string& kind) const {
        if (_acceptDropKinds.empty()) {
            return true;
        }
        for (const std::string& k : _acceptDropKinds) {
            if (k == kind) return true;
        }
        return false;
    }
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
    void setCurrentDropTarget(bool t) {
        if (_isCurrentDropTarget == t) return;
        _isCurrentDropTarget = t;
        markDirty();
    }

    // ID
    void setId(const std::string& id) { _id = id; }
    const std::string& getId() const { return _id; }

    // Rendering - Widget calls IRenderBackend
    void setRenderBackend(IRenderBackend* backend) { _renderBackend = backend; }
    IRenderBackend* getRenderBackend() const { return _renderBackend; }

    virtual void render(IRenderBackend& renderer);
    virtual void renderChildren(IRenderBackend& renderer);

    // Retained is the default: onRender is recorded only when this widget's
    // local presentation is invalid, then the cached high-level commands are
    // replayed every frame. Immediate preserves the pre-display-list path for
    // diagnostics and custom widgets. Unsupported resource/pass operations
    // automatically fall back to Immediate for that widget without changing
    // the configured policy.
    void setDisplayListPolicy(DisplayListPolicy policy);
    DisplayListPolicy getDisplayListPolicy() const { return _displayListPolicy; }
    bool hasCachedDisplayList() const { return _displayListValid; }
    size_t getCachedDisplayCommandCount() const { return _displayList.size(); }

    // Optional pixel cache for this widget's complete subtree. The Layer is
    // clipped to getWorldBounds(); custom widgets that intentionally paint
    // outside those bounds should keep this disabled or provide a containing
    // cache widget. Allocation/paint failures fall back to normal rendering.
    void setLayerCachePolicy(LayerCachePolicy policy);
    LayerCachePolicy getLayerCachePolicy() const;
    void setAutoLayerCacheTuning(const AutoLayerCacheTuning& tuning);
    AutoLayerCacheTuning getAutoLayerCacheTuning() const;
    AutoLayerCacheMetrics getAutoLayerCacheMetrics() const;
    bool hasActiveLayerCache() const;

    void markBoundsDirty() {
        // Phase UI-PERF-1: lazy-propagate. Previously this recursed through
        // every descendant (O(N) per call), and VBox/HBox layout calls
        // setPosition/setSize on each child every performLayout pass, so
        // the whole tree was re-dirties every frame even when most nodes
        // didn't move. With lazy propagate, only the changed widget is
        // flagged here; getWorldBounds() walks UP the parent chain — if
        // any ancestor is dirty, the current node recomputes too.
        _boundsDirty = true;
        // AYUI-Perf-2026-08-26 (Batch C rebase fix): also flip the
        // cache dirty flag so a subsequent getWorldBounds() call sees
        // the dirty bit and recomputes. Without this, a stale
        // _boundsCache field could survive a setPosition cycle (the
        // parent chain has changed but the cached rect still reflects
        // the old world origin).
        _boundsCacheDirty = true;
        _displayListDirty = true;
    }

    // AYUI-Perf-2026-08-26 (Batch C rebase fix): walk children
    // recursively, marking each subtree's bounds cache stale. Used by
    // setPosition/setSize so the parent's own movement cascades
    // through every descendant (the parent's world-origin shift would
    // otherwise leave every child reading from a stale cached rect on
    // the next getWorldBounds).
    void markDescendantsBoundsDirty();

    // =================================================================
    // AYUI invalidation/damage tracking.
    // =================================================================
    // markDirty(invalidRect) marks THIS widget dirty. The default (invalid
    // rect) means "the whole widget's cached presentation is invalid".
    // Passing a valid rect unions it into _dirtyRect for future retained
    // display-list or render-target caches. Explicit damage is expressed in
    // root-canvas logical coordinates (the same space as getWorldBounds()).
    //
    // Dirty state propagates to ancestors so retained pixel layers can
    // invalidate the composited branch. It does NOT gate Widget::render():
    // partial root repaint still traverses the tree, while the backend clip
    // rejects geometry outside damage. Frame-local submissions are always
    // rebuilt for whichever region is being painted.
    //
    // NOTE: "invalid" here = FRectangle{0,0,0,0}, which is the default
    // ctor and has zero area. We don't need a separate sentinel; the
    // default-constructed rectangle serves as both "clean" and "invalid
    // mark the whole widget" — see Widget::markDirty() body. AYMath
    // doesn't ship a `FRectangle::empty()` helper, so we use a static
    // helper here.
    static bool isDirtyRectEmpty(const math::FRectangle& r) {
        // Zero-area rect = the default ctor output. Inverse or equal min/max
        // also count as "no paint region".
        return r.maxX <= r.minX || r.maxY <= r.minY;
    }

    void markDirty(const math::FRectangle& r = math::FRectangle()) {
        const bool hasExplicitDamage = !isDirtyRectEmpty(r);
        const bool wasFullDirty = _dirtyThis;
        bool damageChanged = false;
        if (hasExplicitDamage) {
            // Union into _dirtyRect. Empty union (current rect empty)
            // collapses to just r.
            if (!_dirtyThis && isDirtyRectEmpty(_dirtyRect)) {
                _dirtyRect = r;
                damageChanged = appendDirtyRegion(r);
            } else if (!_dirtyThis) {
                _dirtyRect = math::FRectangle::fromMinMax(
                    math::FVector2(
                        std::min(_dirtyRect.minX, r.minX),
                        std::min(_dirtyRect.minY, r.minY)),
                    math::FVector2(
                        std::max(_dirtyRect.maxX, r.maxX),
                        std::max(_dirtyRect.maxY, r.maxY)));
                damageChanged = appendDirtyRegion(r);
            }
        } else {
            // Empty/invalid rect passed → mark the whole widget.
            _dirtyThis = true;
            _dirtyRect = math::FRectangle();
            clearDirtyRegions();
        }
        _displayListDirty = true;
        if (_parent != nullptr) {
            if (hasExplicitDamage && !wasFullDirty && damageChanged) {
                // Audit B-NEW-2 / M-R-9: explicit damage is expressed in
                // THIS widget's local paint frame (not root). Translate
                // by `_position` so the same region is correctly described
                // in the parent's local frame on the way up the chain.
                // The accumulation through every ancestor lands the rect
                // in the root's local frame at the top, where retained
                // layer caches compare it against getWorldBounds().
                const math::FRectangle parentRect(
                    r.minX + _position.x, r.minY + _position.y,
                    r.maxX + _position.x, r.maxY + _position.y);
                _parent->markDirtyFromDescendant(parentRect);
            } else if (!hasExplicitDamage && !wasFullDirty) {
                // The default form deliberately remains conservative: an
                // arbitrary widget may paint shadows/outsets beyond bounds.
                _parent->markDirtyFromDescendant();
            }
        }
    }

    // Test/debug: which cache/damage invalidation markers are pending?
    bool isDirtyThis() const { return _dirtyThis; }
    bool hasDirtyRect() const { return !isDirtyRectEmpty(_dirtyRect); }
    const math::FRectangle& getDirtyRect() const { return _dirtyRect; }
    const std::vector<math::FRectangle>& getDirtyRegions() const;
    static constexpr size_t kMaxDamageRegions = 8u;

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
    bool _anchorLayoutEnabled = false;
    AnchorLayout _anchorLayout;
    std::vector<ResponsiveLayoutRule> _responsiveLayoutRules;
    ResponsiveVisibility _responsiveVisibility =
        ResponsiveVisibility::Inherit;
    int _activeResponsiveRuleIndex = -1;

    // PR-anim: tree opacity + fade transition state. _opacity == 1.0 is
    // the fast path (no pushOpacity, byte-identical rendering); the
    // AnimState is idle until animateOpacity starts a tween.
    float _opacity = 1.0f;
    AnimState<float> _opacityAnim;
    AnimationCallbacks _opacityAnimationCallbacks;

    // UI-anim cut 2: position tween (popup slide-ins). Idle by default —
    // only popups start it; getWorldBounds self-heals on drift.
    AnimState<math::FVector2> _posAnim;
    AnimationCallbacks _positionAnimationCallbacks;

    void cancelOpacityAnimation(bool notify);
    void cancelPositionAnimation(bool notify);

    std::string _styleId;
    std::string _id;
    std::string _controllerId;
    std::unordered_map<std::string, std::string> _eventBindings;
    std::unordered_map<std::string, std::string> _localizationKeys;
    std::unordered_map<std::string, std::vector<std::string>>
        _localizationKeyLists;
    // G11 — per-widget token overrides. Keyed by bare token name (no
    // leading '$'). Resolved during StyleSheet parsing AND during
    // resolveStyle() so a JSON-loaded style with `"$color.bg": "..."`
    // AND a programmatic setStyleTokenOverride() both flow through.
    std::unordered_map<std::string, math::FVector4> _tokenOverrides;

    uint64_t _accessibilityId = 0;
    AccessibilityRole _accessibilityRole = AccessibilityRole::Generic;
    bool _accessibilityRoleExplicit = false;
    bool _accessibilityHidden = false;
    AccessibilityLiveSetting _accessibilityLiveSetting = AccessibilityLiveSetting::Off;
    std::wstring _accessibilityLabel;
    std::wstring _accessibilityDescription;
    std::wstring _accessibilityValue;
    std::function<bool(AccessibilityAction)> _accessibilityActionHandler;

    // G12 — Drag & Drop state. Empty std::function defaults pay no
    // runtime cost; only widgets that opt in (setDraggable / setAcceptDrops
    // + a callback) allocate the closure capture. The "horizontal feature"
    // argument against adding these to base: it's deliberate — every
    // widget needs to be reachable as a drag source or target without
    // deriving a new class, and a mixin would force host boilerplate that
    // duplicates this exact field set per subclass.
    bool _draggable = false;
    bool _acceptDrops = false;
    // AYUI-Audit-2026-08-26: see setAcceptDropKinds/acceptsKind above.
    // Empty list = accept any kind (preserves the legacy
    // any-accepts-anything contract).
    std::vector<std::string> _acceptDropKinds;
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

    // =================================================================
    // AYUI invalidation/damage state.
    // =================================================================
    // _dirtyThis: the whole widget's cached presentation is invalid.
    // Set true on construction and cleared after the widget is submitted.
    //
    // _dirtyRect: a sub-rectangle union of all damage since the last
    // render. Default-constructed == zero area == empty == clean. Used by
    // partial-redraw/cache callers; for typical setters,
    // markDirty(invalid) is sufficient and clears _dirtyRect.
    //
    // IMPORTANT: neither field suppresses Widget traversal by itself.
    // Widget-local display lists avoid rerunning onRender(), and an enabled
    // root pixel layer uses _dirtyRect as its clipped repaint region.
    bool _dirtyThis = true;
    math::FRectangle _dirtyRect;

    // Local retained presentation. Child invalidation marks ancestors dirty
    // for subtree/damage bookkeeping but does not rebuild their local list.
    // Geometry changes cascade _displayListDirty through descendants because
    // current drawing commands use world-space coordinates.
    DisplayListPolicy _displayListPolicy = DisplayListPolicy::Retained;
    DisplayList _displayList;
    bool _displayListValid = false;
    bool _displayListDirty = true;
    uint64_t _displayListStyleVersion = 0;

    // AYUI-Perf-2026-08-26 (Batch C rebase fix): cached world-bounds
    // result + dirty flag. mutable so the const getWorldBounds() can
    // update the cache after a recompute without breaking the const
    // contract. _boundsCacheDirty is true whenever the cache holds a
    // stale value (initial state, or after markBoundsDirty /
    // markDescendantsBoundsDirty fired). The cache is invalidated by
    // setPosition, setSize, and a parent's position/size change (which
    // propagates downward recursively).
    mutable math::FRectangle _boundsCache;
    mutable bool _boundsCacheDirty = true;
    // Debug counter for tests — incremented whenever getWorldBounds()
    // does a fresh chain walk + recompute (as opposed to returning the
    // cached rect). Build with #undef NDEBUG to surface it; default
    // builds compile the increment out.
#ifndef NDEBUG
    mutable int _worldBoundsRecomputeCount = 0;
public:
    int debugGetWorldBoundsRecomputeCount() const { return _worldBoundsRecomputeCount; }
    void debugResetWorldBoundsRecomputeCount() { _worldBoundsRecomputeCount = 0; }
protected:
#endif

    void updateWorldBounds();
    math::FVector2 getWorldPosition() const;
    // Keep the no-argument symbol for incrementally-built hosts/TUs. The
    // overload carries opt-in rectangular damage without changing the
    // established ABI entry point.
    void markDirtyFromDescendant();
    void markDirtyFromDescendant(const math::FRectangle& damage);
    void markStyleSubtreeDirty();
    bool recordNestedRenderIfNeeded(IRenderBackend& renderer);
    bool appendDirtyRegion(const math::FRectangle& damage);
    void clearDirtyRegions();
    bool tryRenderLayerCache(IRenderBackend& renderer);
    void renderSubtreeContent(IRenderBackend& renderer);
    size_t estimateSubtreeDisplayCommandCount() const;
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

// PR-Container-Shared-Contract: sibling of compoundDescendHitTest that
// gates descent by clientRect first. Use this from any container that
// has chrome eating into getWorldBounds() (scrollbars, title bar). The
// default compoundDescendHitTest is unchanged; it still serves
// CompoundWidget / CompoundFocusableWidget which have no chrome.
Widget* compoundDescendHitTestClipped(Widget* self,
                                      const math::FRectangle& clientRect,
                                      const math::FVector2& worldPos);

// PR-Container-Contract-Cut2: single-arg overload that derives the
// clientRect from self. Containers whose getClientRect() override is
// the single source of truth for both hit-test and render clip can
// call this instead of recomputing the rect inline. Declaration must
// come AFTER the 3-arg overload above (it's a thin wrapper, not a
// template — no risk of overload-resolution surprises, but the
// signature is distinct enough that either order compiles).
Widget* compoundDescendHitTestClipped(Widget* self,
                                      const math::FVector2& worldPos);

// PR-Container-Contract-Cut2: pushClip(clientRect) + render non-excluded
// children + popClip. Sibling of compoundDescendClippedRender that
// threads through the same clientRect the hit-test gate uses, so any
// add-child-after-cut2 call renders inside the container's clip box
// instead of leaking onto chrome. `exclude` solves the "vbar was
// addChild-ed but the container draws it after popClip" pattern —
// passing {_vbar, _hbar} keeps the bars out of the clipped cascade
// so the container's explicit bar paint after popClip stays
// authoritative.
void compoundDescendClippedRender(Widget* self,
                                  IRenderBackend& renderer,
                                  std::initializer_list<Widget*> exclude = {});

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
