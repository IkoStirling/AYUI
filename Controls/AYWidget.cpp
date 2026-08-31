#include "AYUI/Widget.h"
#include "AYUI/CompoundFocusableWidget.h"
#include "AYUI/Style.h"
#include "AYMath/MathUtils.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace ayt::ui {

namespace {
std::atomic<uint64_t> g_nextAccessibilityId{1};

struct WidgetRetainedState {
    std::vector<math::FRectangle> damageRegions;
    LayerCachePolicy policy = LayerCachePolicy::Disabled;
    IRenderBackend* backend = nullptr;
    IRenderBackend::LayerHandle layer{};
    IRenderBackend::LayerDesc desc{};
    bool descValid = false;
    bool painted = false;
    bool autoActive = false;
    uint32_t stableFrames = 0;
    uint32_t unstableFrames = 0;
};

std::unordered_map<const Widget*, WidgetRetainedState> g_widgetRetainedStates;

bool damageRectsTouchOrOverlap(const math::FRectangle& a,
                               const math::FRectangle& b) {
    return a.minX <= b.maxX && a.maxX >= b.minX
        && a.minY <= b.maxY && a.maxY >= b.minY;
}

math::FRectangle unionDamageRects(const math::FRectangle& a,
                                  const math::FRectangle& b) {
    return math::FRectangle(
        std::min(a.minX, b.minX), std::min(a.minY, b.minY),
        std::max(a.maxX, b.maxX), std::max(a.maxY, b.maxY));
}

math::FRectangle intersectDamageRect(const math::FRectangle& a,
                                     const math::FRectangle& b) {
    return math::FRectangle(
        std::max(a.minX, b.minX), std::max(a.minY, b.minY),
        std::min(a.maxX, b.maxX), std::min(a.maxY, b.maxY));
}

bool layerDescsEqual(const IRenderBackend::LayerDesc& a,
                     const IRenderBackend::LayerDesc& b) {
    return a.logicalBounds.minX == b.logicalBounds.minX
        && a.logicalBounds.minY == b.logicalBounds.minY
        && a.logicalBounds.maxX == b.logicalBounds.maxX
        && a.logicalBounds.maxY == b.logicalBounds.maxY
        && a.dpiScale == b.dpiScale && a.hasAlpha == b.hasAlpha
        && a.overlay == b.overlay && a.clearMode == b.clearMode
        && a.clearColor == b.clearColor;
}

void releaseWidgetLayer(WidgetRetainedState& state) {
    if (state.backend != nullptr && state.layer.isValid()) {
        state.backend->releaseLayer(state.layer);
    }
    state.backend = nullptr;
    state.layer = {-1};
    state.desc = {};
    state.descValid = false;
    state.painted = false;
}
}

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

// PR-Container-Shared-Contract: same shape as compoundDescendHitTest but
// gates descent by clientRect first. Use this from containers with chrome
// (scrollbars, title bar) that wants hits OUTSIDE the client area to fall
// through to `self` (so chrome still catches them) but to NOT descend into
// children outside the client area (so stale off-screen slots don't claim
// clicks).
Widget* compoundDescendHitTestClipped(Widget* self,
                                      const math::FRectangle& clientRect,
                                      const math::FVector2& worldPos) {
    if (self == nullptr) return nullptr;
    if (!self->isVisible()) return nullptr;
    // Outside client area — only return self if the point still lies in
    // world bounds (chrome / background). Children outside the client
    // area are unreachable through this gate.
    if (!clientRect.contains(worldPos)) {
        return self->getWorldBounds().contains(worldPos) ? self : nullptr;
    }
    // Inside client area — reverse-order descent into children.
    for (auto it = self->getChildren().rbegin(); it != self->getChildren().rend(); ++it) {
        if (Widget* hit = (*it)->hitTest(worldPos)) {
            return hit;
        }
    }
    return self;
}

// PR-Container-Contract-Cut2: single-arg overload that derives the
// clientRect from self. Containers whose getClientRect() override is
// the single source of truth for both hit-test and render clip use
// this overload — keeps the call site symmetric (no need to type
// "this->getClientRect()" inline next to "this->" again).
Widget* compoundDescendHitTestClipped(Widget* self,
                                      const math::FVector2& worldPos) {
    if (self == nullptr) return nullptr;
    return compoundDescendHitTestClipped(self, self->getClientRect(), worldPos);
}

// PR-Container-Contract-Cut2: pushClip(clientRect) + render non-excluded
// children + popClip. Matches the 3-arg hit-test gate so a child
// painted via this helper is bounded by the same rect that contains
// its hit-testable area. The `exclude` list lets callers keep children
// they draw explicitly after popClip (typical: scrollbars, which need
// to be drawn over the chrome area, not under the content clip).
void compoundDescendClippedRender(Widget* self, IRenderBackend& renderer,
                                  std::initializer_list<Widget*> exclude) {
    if (self == nullptr) return;
    renderer.pushClip(self->getClientRect());
    for (Widget* child : self->getChildren()) {
        if (child == nullptr) continue;
        bool skip = false;
        for (Widget* e : exclude) {
            if (e == child) { skip = true; break; }
        }
        if (!skip) child->render(renderer);
    }
    renderer.popClip();
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
    _accessibilityId = g_nextAccessibilityId.fetch_add(1, std::memory_order_relaxed);
}

Widget::~Widget() {
    if (auto retained = g_widgetRetainedStates.find(this);
        retained != g_widgetRetainedStates.end()) {
        releaseWidgetLayer(retained->second);
        g_widgetRetainedStates.erase(retained);
    }
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
    if (child == nullptr) return;
    if (child->_parent == this) {
        // Already in our tree. addChild's semantic is "we own it" so
        // if the child was previously addChildExternal'd, promote it
        // to fully-owned — otherwise destroyWidgetTree would leak it
        // when treating the externally-owned flag as host-managed.
        // Code-review Sweep3-#4: previously we early-returned when
        // _parent matched and skipped the flag flip; a fixture that
        // did addChildExternal first and then addChild (e.g. to take
        // ownership after a manual construction step) would leak on
        // tree teardown. Always normalize the flag on the
        // owning-path call.
        if (child->_externallyOwned) {
            child->_externallyOwned = false;
        }
        return;
    }
    child->detachFromParent();
    child->_parent = this;
    child->_externallyOwned = false;
    _children.push_back(child);
    // Re-attaching a child invalidates the parent's presentation cache.
    markDirty();
}

void Widget::addChildExternal(Widget* child) {
    // Reference-only attach: host owns lifetime. destroyWidgetTree must
    // detach without delete (stack Modal/TextInput fixtures, Dimmer, etc.).
    if (child && child->_parent != this) {
        child->detachFromParent();
        child->_parent = this;
        child->_externallyOwned = true;
        _children.push_back(child);
        markDirty();
    }
}

void Widget::removeChild(Widget* child) {
    if (!child) return;
    for (auto it = _children.begin(); it != _children.end(); ++it) {
        if (*it == child) {
            child->_parent = nullptr;
            child->_externallyOwned = false;
            _children.erase(it);
            // Repaint the parent so the removed child's old pixels are
            // covered on the next frame.
            markDirty();
            return;
        }
    }
}

bool Widget::moveChildToIndex(Widget* child, size_t index) {
    if (child == nullptr || child->_parent != this) {
        return false;
    }
    if (index >= _children.size()) {
        return false;
    }
    size_t cur = static_cast<size_t>(-1);
    for (size_t i = 0; i < _children.size(); ++i) {
        if (_children[i] == child) {
            cur = i;
            break;
        }
    }
    if (cur == static_cast<size_t>(-1) || cur == index) {
        return cur == index;
    }
    _children.erase(_children.begin() + static_cast<std::ptrdiff_t>(cur));
    // `index` is the desired FINAL index after the move. Do not
    // decrement when index > cur — that made "move down" a no-op.
    _children.insert(_children.begin() + static_cast<std::ptrdiff_t>(index), child);
    markDirty();
    return true;
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
            _parent->markDirty();
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
    // AYUI-Perf-2026-08-26: refresh the world-bounds cache after a
    // recompute. Both _bounds and _boundsCache carry the same value;
    // callers that want the cached path use _boundsCache directly.
    _boundsCache = _bounds;
    _boundsCacheDirty = false;
}

void Widget::markDescendantsBoundsDirty() {
    // AYUI-Perf-2026-08-26: parent's setPosition/setSize calls this so
    // every descendant's cached world-bounds is invalidated. Without it,
    // a moved parent would leave every child reading a stale world rect
    // from the cache until each child itself moved (which doesn't happen
    // during layout — only the parent is repositioned).
    for (Widget* child : _children) {
        if (child == nullptr) continue;
        child->_boundsCacheDirty = true;
        child->_displayListDirty = true;
        child->_dirtyThis = true;
        child->_dirtyRect = math::FRectangle();
        child->clearDirtyRegions();
        child->markDescendantsBoundsDirty();
    }
}

void Widget::markDirtyFromDescendant() {
    const bool wasFullDirty = _dirtyThis;
    _dirtyThis = true;
    _dirtyRect = math::FRectangle();
    clearDirtyRegions();
    if (!wasFullDirty && _parent != nullptr) {
        _parent->markDirtyFromDescendant();
    }
}

void Widget::markDirtyFromDescendant(const math::FRectangle& damage) {
    if (isDirtyRectEmpty(damage)) {
        markDirtyFromDescendant();
        return;
    }
    if (_dirtyThis) return;
    const bool regionChanged = appendDirtyRegion(damage);
    if (!regionChanged) return;
    if (isDirtyRectEmpty(_dirtyRect)) {
        _dirtyRect = damage;
    } else {
        _dirtyRect = math::FRectangle::fromMinMax(
            math::FVector2(std::min(_dirtyRect.minX, damage.minX),
                           std::min(_dirtyRect.minY, damage.minY)),
            math::FVector2(std::max(_dirtyRect.maxX, damage.maxX),
                           std::max(_dirtyRect.maxY, damage.maxY)));
    }
    // Propagate the original region, not the compatibility union. Two small
    // distant child invalidations must remain two regions at the root.
    if (_parent != nullptr) {
        _parent->markDirtyFromDescendant(damage);
    }
}

void Widget::markStyleSubtreeDirty() {
    _displayListDirty = true;
    _dirtyThis = true;
    _dirtyRect = math::FRectangle();
    clearDirtyRegions();
    for (Widget* child : _children) {
        if (child != nullptr) child->markStyleSubtreeDirty();
    }
    if (_parent != nullptr) _parent->markDirtyFromDescendant();
}

math::FRectangle Widget::getWorldBounds() const {
    // AYUI-Perf-2026-08-26: cache fast path. Previously this method walked
    // the parent chain on every call (O(depth)) and re-checked the
    // parent-origin / local-position drift on the way back up. Combined
    // with the dock-tree hit-test (which calls getWorldBounds once per
    // candidate node), this was O(depth^2) per query. With the cache,
    // the common "tree hasn't moved since last query" case is a single
    // boolean check.

    // First: lazily flush any dirty parent so its world origin is
    // up-to-date. We don't need its rect — we just need to ensure the
    // ancestor chain has been recomputed if any link is dirty.
    if (_parent != nullptr) {
        (void)_parent->getWorldBounds();
    }

    // Cache hit — return the cached rect directly. This is the new
    // hot path; everything below runs only on cache miss.
    if (!_boundsCacheDirty) {
        return _boundsCache;
    }

    // Cache miss path — same body as the pre-cache implementation, with
    // the recompute counter increment for tests.
    if (_boundsDirty) {
        const_cast<Widget*>(this)->updateWorldBounds();
#ifndef NDEBUG
        _worldBoundsRecomputeCount++;
#endif
        return _bounds;
    }
    if (_parent != nullptr) {
        const math::FVector2 expected =
            _parent->getWorldBounds().getMin() + _position;
        if (std::fabs(_bounds.minX - expected.x) > 0.01f
            || std::fabs(_bounds.minY - expected.y) > 0.01f) {
            const_cast<Widget*>(this)->updateWorldBounds();
#ifndef NDEBUG
            _worldBoundsRecomputeCount++;
#endif
        } else {
            // Parent origin matches the last-cached local + parent
            // world — the cache was dirty for another reason (e.g. a
            // position tween just landed). Refresh the cache from the
            // already-correct _bounds rather than running the math
            // path again.
            _boundsCache = _bounds;
            _boundsCacheDirty = false;
        }
    } else {
        // No parent + dirty cache means _bounds may also be stale.
        // updateWorldBounds does the work and populates the cache.
        const_cast<Widget*>(this)->updateWorldBounds();
#ifndef NDEBUG
        _worldBoundsRecomputeCount++;
#endif
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

bool Widget::onTextInputText(const std::wstring& text) {
    bool handled = false;
    for (wchar_t ch : text) {
        handled = onTextInput(ch) || handled;
    }
    return handled;
}

// --- G11: per-widget theme token overrides ---
void Widget::setStyleTokenOverride(const std::string& key, const math::FVector4& value) {
    if (key.empty()) return;
    // Allow '$foo' or bare 'foo' input — normalize to bare form so the
    // map key matches Theme's storage (which uses bare names without '$').
    const std::string norm = (key[0] == '$') ? key.substr(1) : key;
    _tokenOverrides[norm] = value;
    // AYUI-DirtyRect-2026-08-26 (rebase fix): token override change
    // re-tints the resolved style; onRender must re-run.
    markStyleSubtreeDirty();
}

void Widget::clearStyleTokenOverrides() {
    if (_tokenOverrides.empty()) {
        return;
    }
    _tokenOverrides.clear();
    markStyleSubtreeDirty();
}

bool Widget::hasStyleTokenOverride(const std::string& key) const {
    if (key.empty()) return false;
    const std::string norm = (key[0] == '$') ? key.substr(1) : key;
    return _tokenOverrides.find(norm) != _tokenOverrides.end();
}

bool Widget::findInheritedStyleTokenOverride(const std::string& key,
                                             math::FVector4& outValue) const {
    if (key.empty()) return false;
    const std::string norm = (key[0] == '$') ? key.substr(1) : key;
    for (const Widget* current = this; current != nullptr;
         current = current->_parent) {
        const auto it = current->_tokenOverrides.find(norm);
        if (it != current->_tokenOverrides.end()) {
            outValue = it->second;
            return true;
        }
    }
    return false;
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

bool Widget::recordNestedRenderIfNeeded(IRenderBackend& renderer) {
    // Complex containers such as ScrollView/ListView/TreeView render selected
    // children from onRender so their border can be painted afterwards. When
    // that happens under a parent's recorder, retain a dynamic child call,
    // then execute the child against the real backend. Flattening the child's
    // current commands into the parent would make later child-only dirty
    // updates replay stale content.
    if (auto* recorder = dynamic_cast<DisplayListRecorder*>(&renderer)) {
        Widget* nested = this;
        recorder->recordAndForwardNested([nested](IRenderBackend& backend) {
            nested->render(backend);
        });
        return true;
    }
    return false;
}

const std::vector<math::FRectangle>& Widget::getDirtyRegions() const {
    static const std::vector<math::FRectangle> kEmpty;
    const auto it = g_widgetRetainedStates.find(this);
    return it == g_widgetRetainedStates.end() ? kEmpty : it->second.damageRegions;
}

bool Widget::appendDirtyRegion(const math::FRectangle& damage) {
    if (isDirtyRectEmpty(damage)) return false;
    WidgetRetainedState& state = g_widgetRetainedStates[this];
    math::FRectangle merged = damage;
    for (size_t i = 0; i < state.damageRegions.size();) {
        const math::FRectangle& existing = state.damageRegions[i];
        if (existing.minX <= merged.minX && existing.minY <= merged.minY
            && existing.maxX >= merged.maxX && existing.maxY >= merged.maxY) {
            return false;
        }
        if (damageRectsTouchOrOverlap(existing, merged)) {
            merged = unionDamageRects(existing, merged);
            state.damageRegions.erase(state.damageRegions.begin()
                                      + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
    if (state.damageRegions.size() >= kMaxDamageRegions) {
        for (const math::FRectangle& existing : state.damageRegions) {
            merged = unionDamageRects(merged, existing);
        }
        state.damageRegions.clear();
    }
    state.damageRegions.push_back(merged);
    return true;
}

void Widget::clearDirtyRegions() {
    const auto it = g_widgetRetainedStates.find(this);
    if (it != g_widgetRetainedStates.end()) {
        it->second.damageRegions.clear();
    }
}

void Widget::setLayerCachePolicy(LayerCachePolicy policy) {
    WidgetRetainedState& state = g_widgetRetainedStates[this];
    if (state.policy == policy) return;
    if (policy == LayerCachePolicy::Disabled) {
        releaseWidgetLayer(state);
        state.autoActive = false;
        state.stableFrames = 0;
        state.unstableFrames = 0;
    }
    state.policy = policy;
    markDirty();
}

LayerCachePolicy Widget::getLayerCachePolicy() const {
    const auto it = g_widgetRetainedStates.find(this);
    return it == g_widgetRetainedStates.end()
        ? LayerCachePolicy::Disabled : it->second.policy;
}

bool Widget::hasActiveLayerCache() const {
    const auto it = g_widgetRetainedStates.find(this);
    return it != g_widgetRetainedStates.end() && it->second.layer.isValid()
        && it->second.painted
        && (it->second.policy == LayerCachePolicy::Always
            || it->second.autoActive);
}

size_t Widget::estimateSubtreeDisplayCommandCount() const {
    size_t count = _displayListValid ? _displayList.size() : 1u;
    for (const Widget* child : _children) {
        if (child != nullptr && child->isVisible()) {
            count += child->estimateSubtreeDisplayCommandCount();
        }
    }
    return count;
}

bool Widget::tryRenderLayerCache(IRenderBackend& renderer) {
    auto stateIt = g_widgetRetainedStates.find(this);
    if (stateIt == g_widgetRetainedStates.end()
        || stateIt->second.policy == LayerCachePolicy::Disabled
        || !renderer.supportsRenderTargets()) {
        return false;
    }
    WidgetRetainedState& state = stateIt->second;
    const bool invalidated = _dirtyThis || hasDirtyRect();
    if (state.policy == LayerCachePolicy::Auto) {
        if (state.autoActive) {
            state.unstableFrames = invalidated ? state.unstableFrames + 1u : 0u;
            if (state.unstableFrames >= 3u) {
                releaseWidgetLayer(state);
                state.autoActive = false;
                state.stableFrames = 0;
                state.unstableFrames = 0;
                return false;
            }
        } else {
            state.stableFrames = invalidated ? 0u : state.stableFrames + 1u;
            const math::FRectangle bounds = getWorldBounds();
            const float area = std::max(0.0f, bounds.maxX - bounds.minX)
                * std::max(0.0f, bounds.maxY - bounds.minY);
            if (state.stableFrames < 3u || area < 4096.0f
                || estimateSubtreeDisplayCommandCount() < 12u) {
                return false;
            }
            state.autoActive = true;
        }
    }

    const math::FRectangle bounds = getWorldBounds();
    if (isDirtyRectEmpty(bounds)) return false;
    IRenderBackend::LayerDesc desc;
    desc.logicalBounds = bounds;
    desc.dpiScale = renderer.getUiScale();
    desc.hasAlpha = true;
    desc.overlay = false;
    desc.clearMode = IRenderBackend::LayerClearMode::Transparent;

    if (state.backend != &renderer) {
        releaseWidgetLayer(state);
        state.backend = &renderer;
    }
    const bool descChanged = !state.descValid || !layerDescsEqual(state.desc, desc);
    if (!state.layer.isValid()) {
        state.layer = renderer.createLayer(desc);
        state.painted = false;
    } else if (descChanged && !renderer.updateLayer(state.layer, desc)) {
        renderer.releaseLayer(state.layer);
        state.layer = renderer.createLayer(desc);
        state.painted = false;
    }
    if (!state.layer.isValid()) return false;
    state.desc = desc;
    state.descValid = true;

    const bool backendDirty = renderer.isLayerDirty(state.layer);
    const bool needsPaint = !state.painted || invalidated || backendDirty || descChanged;
    bool fullRedraw = !state.painted || _dirtyThis || backendDirty || descChanged;
    std::vector<math::FRectangle> regions;
    if (needsPaint && !fullRedraw) {
        regions = getDirtyRegions();
        if (regions.empty() && hasDirtyRect()) regions.push_back(getDirtyRect());
        float repaintArea = 0.0f;
        for (math::FRectangle& region : regions) {
            region = intersectDamageRect(region, bounds);
            if (!isDirtyRectEmpty(region)) {
                repaintArea += (region.maxX - region.minX) * (region.maxY - region.minY);
            }
        }
        regions.erase(std::remove_if(regions.begin(), regions.end(),
            [](const math::FRectangle& r) { return Widget::isDirtyRectEmpty(r); }),
            regions.end());
        const float layerArea = (bounds.maxX - bounds.minX) * (bounds.maxY - bounds.minY);
        if (regions.empty() || repaintArea >= layerArea * 0.70f) fullRedraw = true;
    }
    if (needsPaint && fullRedraw) regions.assign(1u, bounds);

    if (needsPaint) {
        for (const math::FRectangle& damage : regions) {
            if (state.painted && !backendDirty) {
                renderer.invalidateLayer(state.layer,
                    fullRedraw ? math::FRectangle() : damage);
            }
            IRenderBackend::LayerPaint paint;
            paint.damage = damage;
            paint.fullRedraw = fullRedraw;
            if (!renderer.beginLayerPaint(state.layer, paint)) {
                renderer.invalidateLayer(state.layer);
                state.painted = false;
                return false;
            }
            if (!fullRedraw) renderer.pushClip(damage);
            renderSubtreeContent(renderer);
            if (!fullRedraw) renderer.popClip();
            renderer.endLayerPaint(state.layer);
            state.painted = true;
        }
    }
    if (!state.painted || renderer.isLayerDirty(state.layer)) return false;
    renderer.compositeLayer(state.layer, bounds, 1.0f);
    if (renderer.isLayerDirty(state.layer)) {
        state.painted = false;
        return false;
    }
    return true;
}

void Widget::render(IRenderBackend& renderer) {
    // Record even a currently hidden nested widget. Its visibility is
    // evaluated by the dynamic command on every replay, so showing it later
    // cannot leave it permanently absent from a container's cached order.
    if (recordNestedRenderIfNeeded(renderer)) return;
    if (!_visible) return;
    if (tryRenderLayerCache(renderer)) return;
    renderSubtreeContent(renderer);
}

void Widget::renderSubtreeContent(IRenderBackend& renderer) {
    // UIRenderBackend remains an immediate per-frame submission backend:
    // beginFrame() discards prior UiItems and bgfx transient submissions.
    // The Widget-local display list below retains high-level commands only;
    // it still replays every frame, preserving painter order while skipping
    // stable onRender/style/layout command construction.
    // PR-anim: push the node opacity so the whole subtree (own paints +
    // children) fades as one unit. Fast path when fully opaque — the
    // pre-opacity rendering path is byte-identical.
    const bool fading = _opacity < (1.0f - 1e-5f);
    if (fading) {
        renderer.pushOpacity(_opacity);
    }
    if (_displayListPolicy == DisplayListPolicy::Retained) {
        // Style/theme swaps invalidate retained paint lazily. This avoids an
        // O(N) tree walk while guaranteeing that no cached command can replay
        // colors resolved against an older StyleManager generation.
        const uint64_t styleVersion = StyleManager::get().getResolveCacheVersion();
        if (_displayListStyleVersion != styleVersion) {
            _displayListDirty = true;
        }
        if (_displayListValid && !_displayListDirty) {
            _displayList.replay(renderer);
        } else {
            DisplayList candidate;
            DisplayListRecorder recorder(renderer, candidate);
            onRender(recorder);
            if (recorder.isCacheable()) {
                _displayList = std::move(candidate);
                _displayListValid = true;
                _displayListStyleVersion = styleVersion;
            } else {
                // The recorder already forwarded this frame. Keep the legacy
                // path as the next-frame fallback for resource/pass commands
                // whose handles cannot safely live in a generic list.
                _displayList.clear();
                _displayListValid = false;
            }
        }
    } else {
        onRender(renderer);
    }
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
            math::FVector4(0.40f, 0.48f, 0.62f, 1.0f), 2.0f);
    }
    if (fading) {
        renderer.popOpacity();
    }
    // This frame consumed the pending invalidation. Submission still occurs
    // next frame because the backend command buffer is frame-local.
    _dirtyThis = false;
    _dirtyRect = math::FRectangle();
    clearDirtyRegions();
    _displayListDirty = false;
}

void Widget::setDisplayListPolicy(DisplayListPolicy policy) {
    if (_displayListPolicy == policy) {
        return;
    }
    _displayListPolicy = policy;
    _displayList.clear();
    _displayListValid = false;
    _displayListDirty = true;
    _displayListStyleVersion = 0;
    markDirty();
}

void Widget::tick(float dt) {
    // PR-anim: advance the opacity tween. Compound containers invoke each
    // child's virtual tick; every override therefore chains its direct base
    // tick to preserve this common animation step.
    const bool wasActive = _opacityAnim.active;
    const float prevOpacity = _opacity;
    float t;
    if (_opacityAnim.advance(dt, t)) {
        _opacity = tweenLerp(_opacityAnim.from, _opacityAnim.to, t);
    } else if (wasActive) {
        // Tween completed this frame — snap to the exact target (the eased
        // path never runs on the completion frame).
        _opacity = _opacityAnim.to;
    }
    // AYUI-DirtyRect-2026-08-26 (rebase fix): opacity change → repaint.
    if (_opacity != prevOpacity) {
        markDirty();
    }

    // UI-anim cut 2: position tween. Writes _position directly (not via
    // setPosition — that would cancel the in-flight tween we're feeding).
    const bool posActive = _posAnim.active;
    const math::FVector2 prevPos = _position;
    float pt;
    if (_posAnim.advance(dt, pt)) {
        _position = tweenLerp(_posAnim.from, _posAnim.to, pt);
    } else if (posActive) {
        _position = _posAnim.to;
    }
    // AYUI-DirtyRect-2026-08-26 (rebase fix): position tween frames
    // must repaint each step (popup slide-ins etc.).
    if (_position.x != prevPos.x || _position.y != prevPos.y) {
        markBoundsDirty();
        markDescendantsBoundsDirty();
        markDirty();
    }
}

void Widget::setOpacity(float opacity) {
    const float clamped = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    if (_opacity == clamped) {
        return;
    }
    _opacity = clamped;
    // A direct set cancels any in-flight tween — the caller took over.
    _opacityAnim.active = false;
    // AYUI-DirtyRect-2026-08-26 (rebase fix): opacity changed; the
    // alpha pass on existing pixels must re-run.
    markDirty();
}

void Widget::animateOpacity(float to, float durationMs, AnimationCurve curve) {
    const float target = to < 0.0f ? 0.0f : (to > 1.0f ? 1.0f : to);
    if (durationMs <= 0.0f) {
        // Instant snap — same semantics as setOpacity.
        _opacityAnim.active = false;
        setOpacity(target);
        return;
    }
    _opacityAnim.start(_opacity, target, durationMs, curve);
}

void Widget::animatePositionTo(const math::FVector2& to, float durationMs,
                               AnimationCurve curve) {
    if (durationMs <= 0.0f) {
        // Instant snap — same semantics as setPosition (and it cancels any
        // in-flight tween the same way).
        _posAnim.active = false;
        setPosition(to);
        return;
    }
    _posAnim.start(_position, to, durationMs, curve);
}

void Widget::renderChildren(IRenderBackend& renderer) {
    for (Widget* child : _children) {
        child->render(renderer);
    }
}

} // namespace ayt::ui
