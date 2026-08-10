#include "AYBox.h"
#include "AYSplitterHandle.h"
#include "AYWindow.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

namespace {

constexpr float kSizeChangeEpsilon = 0.01f;

} // namespace

BoxBase::BoxBase()
    : _spacing(4.0f)
    , _padding(4.0f, 4.0f, 4.0f, 4.0f)
{
}

BoxBase::~BoxBase() {
}

void BoxBase::setPadding(float left, float top, float right, float bottom) {
    _padding = math::FVector4(left, top, right, bottom);
}

void BoxBase::layoutChildren() {
}

void BoxBase::addWidget(Widget* widget, float size, const BoxSlotLimits& limits) {
    if (widget == nullptr) {
        return;
    }
    removeWidget(widget);
    addChild(widget);
    if (widget->getParent() != this) {
        return;
    }

    Slot slot;
    slot.widget = widget;
    slot.size = size;
    slot.limits = limits;
    // Prefer virtual isSplitterHandle() over dynamic_cast — see Widget.h.
    slot.isSplitter = widget->isSplitterHandle();
    if (slot.isSplitter) {
        // Splitters are never fill slots; pin size so a missed loader
        // path (size=0) cannot stretch the hover band across the row.
        slot.size = SplitterHandle::kDefaultWidth;
    }
    _slots.push_back(slot);

    if (slot.isSplitter) {
        rebindSplitters();
    }
}

void BoxBase::insertWidget(int index, Widget* widget, float size, const BoxSlotLimits& limits) {
    if (widget == nullptr) {
        return;
    }
    removeWidget(widget);
    addChild(widget);
    if (widget->getParent() != this) {
        return;
    }

    Slot slot;
    slot.widget = widget;
    slot.size = size;
    slot.limits = limits;
    slot.isSplitter = widget->isSplitterHandle();
    if (slot.isSplitter) {
        slot.size = SplitterHandle::kDefaultWidth;
    }

    if (index >= static_cast<int>(_slots.size())) {
        _slots.push_back(slot);
        index = static_cast<int>(_slots.size()) - 1;
    } else {
        _slots.insert(_slots.begin() + index, slot);
    }

    if (slot.isSplitter) {
        rebindSplitters();
    }
}

void BoxBase::removeWidget(Widget* widget) {
    if (widget == nullptr) {
        return;
    }
    _slots.erase(
        std::remove_if(_slots.begin(), _slots.end(),
            [widget](const Slot& slot) { return slot.widget == widget; }),
        _slots.end());
    if (widget->getParent() == this) {
        removeChild(widget);
    }
    // Code-review 2026-08-02 #9: removing a panel between two splitters
    // leaves the surrounding splitter widgets' cached neighbor indices
    // (panelSlotBefore / panelSlotAfter) stale. Re-derive so the next
    // drag-end re-resolves them safely. Mirrors what addWidget /
    // insertWidget already do (rebindSplitters()).
    rebindSplitters();
}

void BoxBase::setSlotLimits(int slotIndex, const BoxSlotLimits& limits) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return;
    }
    _slots[static_cast<size_t>(slotIndex)].limits = limits;
}

float BoxBase::slotSize(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return 0.0f;
    }
    return _slots[static_cast<size_t>(slotIndex)].size;
}

void BoxBase::setSlotSize(int slotIndex, float size) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return;
    }
    _slots[static_cast<size_t>(slotIndex)].size = size;
}

bool BoxBase::isSplitterSlot(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return false;
    }
    return _slots[static_cast<size_t>(slotIndex)].isSplitter;
}

int BoxBase::panelSlotBefore(int slotIndex) const {
    for (int i = slotIndex - 1; i >= 0; --i) {
        if (!isSplitterSlot(i)) {
            return i;
        }
    }
    return -1;
}

int BoxBase::panelSlotAfter(int slotIndex) const {
    for (int i = slotIndex + 1; i < static_cast<int>(_slots.size()); ++i) {
        if (!isSplitterSlot(i)) {
            return i;
        }
    }
    return -1;
}

void BoxBase::bindSplitter(SplitterHandle* splitter, int splitterSlotIndex) {
    if (splitter == nullptr) {
        return;
    }

    const int beforePanel = panelSlotBefore(splitterSlotIndex);
    const int afterPanel = panelSlotAfter(splitterSlotIndex);
    if (beforePanel < 0 || afterPanel < 0) {
        return;
    }

    splitter->bindPanels(this, beforePanel, afterPanel);
}

void BoxBase::rebindSplitters() {
    for (size_t i = 0; i < _slots.size(); ++i) {
        if (_slots[i].isSplitter) {
            bindSplitter(static_cast<SplitterHandle*>(_slots[i].widget), static_cast<int>(i));
        }
    }
}

void BoxBase::performLayout() {
    rebindSplitters();
    layoutChildren();
}

void BoxBase::render(IRenderBackend& renderer) {
    if (!_visible) {
        return;
    }

    // Single-pass insertion-order render. Previously this looped twice with
    // per-slot dynamic_cast<SplitterHandle*>; splitters are cached on the
    // Slot at insertion time. Splitters sit between panels (no z-order
    // conflict, non-overlapping bounds) so the visual result is identical.
    for (const Slot& slot : _slots) {
        if (slot.widget != nullptr) {
            slot.widget->render(renderer);
        }
    }
}

Widget* BoxBase::hitTest(const math::FVector2& worldPos) {
    if (!_visible) {
        return nullptr;
    }

    // Descend BEFORE gating on self bounds. CompoundWidget / the pre-
    // hoist VBox path did the same: ScrollView content VBoxes often keep
    // a small default size (or viewport-sized bounds) while children are
    // laid out to their natural height and painted via the scroll offset.
    // An early getWorldBounds().contains() reject (legacy HBox-only) made
    // those children unreachable — Gallery content clicks fell through to
    // the ScrollView host. Splitters are still probed first so a handle
    // overlapping a panel edge wins.
    for (int i = static_cast<int>(_slots.size()) - 1; i >= 0; --i) {
        if (!_slots[static_cast<size_t>(i)].isSplitter) {
            continue;
        }
        Widget* hit = _slots[static_cast<size_t>(i)].widget->hitTest(worldPos);
        if (hit != nullptr) {
            return hit;
        }
    }

    for (auto it = _children.rbegin(); it != _children.rend(); ++it) {
        // Skip splitters — already probed above via the cached Slot flag.
        if ((*it)->isSplitterHandle()) {
            continue;
        }
        Widget* hit = (*it)->hitTest(worldPos);
        if (hit != nullptr) {
            return hit;
        }
    }

    return getWorldBounds().contains(worldPos) ? this : nullptr;
}

void BoxBase::applySplitterDrag(int beforePanelSlot, int afterPanelSlot, float mouseAxisPos,
                                float dragStartMouseAxisPos, float dragStartPrimarySize,
                                bool adjustBefore) {
    // Code-review 2026-08-02 #9: cached stale neighbor indices can fall
    // out of range after a removeWidget before rebindSplitters() runs
    // (or in concurrent shutdown paths). Bounds-check targetIndex
    // before any write — the clampSlotSize helper validates too, but
    // the unguarded `_slots[targetIndex].size =` below would otherwise
    // touch out-of-bounds memory.
    if (beforePanelSlot < 0 || afterPanelSlot < 0) {
        return;
    }
    const int targetIndex = adjustBefore ? beforePanelSlot : afterPanelSlot;
    if (targetIndex < 0 || targetIndex >= static_cast<int>(_slots.size())) {
        return;
    }
    if (_slots[static_cast<size_t>(targetIndex)].widget == nullptr) {
        return;
    }

    const float d = mouseAxisPos - dragStartMouseAxisPos;
    const float currentSize = _slots[static_cast<size_t>(targetIndex)].size;
    const float desiredSize =
        adjustBefore ? (dragStartPrimarySize + d) : (dragStartPrimarySize - d);
    const float clampedSize = clampSlotSize(targetIndex, desiredSize);

    if (std::abs(clampedSize - currentSize) < kSizeChangeEpsilon) {
        return;
    }

    _slots[static_cast<size_t>(targetIndex)].size = clampedSize;
    performLayout();
}

float VBox::axisContentLength() const {
    return std::max(0.0f, getSize().y - _padding.y - _padding.w);
}

float HBox::axisContentLength() const {
    return std::max(0.0f, getSize().x - _padding.x - _padding.z);
}

float BoxBase::resolveMinSlotSize(const Slot& slot) const {
    const float content = axisContentLength();
    float minS = kMinPanelSize;

    if (const Window* window = dynamic_cast<const Window*>(slot.widget)) {
        minS = std::max(minS, window->getMinSize().x);
    }
    if (slot.limits.minWidth > 0.0f) {
        minS = std::max(minS, slot.limits.minWidth);
    }
    if (slot.limits.minWidthPercent > 0.0f) {
        minS = std::max(minS, content * slot.limits.minWidthPercent * 0.01f);
    }

    return minS;
}

float BoxBase::resolveMaxSlotSize(const Slot& slot) const {
    const float content = axisContentLength();
    float maxS = content;

    if (slot.limits.maxWidth > 0.0f) {
        maxS = std::min(maxS, slot.limits.maxWidth);
    }
    if (slot.limits.maxWidthPercent > 0.0f) {
        maxS = std::min(maxS, content * slot.limits.maxWidthPercent * 0.01f);
    }

    return maxS;
}

float BoxBase::minSlotSize(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return kMinPanelSize;
    }
    if (isSplitterSlot(slotIndex)) {
        return SplitterHandle::kDefaultWidth;
    }
    return resolveMinSlotSize(_slots[static_cast<size_t>(slotIndex)]);
}

float BoxBase::maxSlotSize(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return axisContentLength();
    }
    if (isSplitterSlot(slotIndex)) {
        return SplitterHandle::kDefaultWidth;
    }

    const float total = axisContentLength();
    const float spacingTotal = _spacing * static_cast<float>(_slots.size() - 1);
    float reserved = spacingTotal;

    for (int i = 0; i < static_cast<int>(_slots.size()); ++i) {
        if (i == slotIndex) {
            continue;
        }
        if (isSplitterSlot(i)) {
            reserved += SplitterHandle::kDefaultWidth;
        } else if (_slots[static_cast<size_t>(i)].size > 0.0f) {
            reserved += _slots[static_cast<size_t>(i)].size;
        } else {
            reserved += minSlotSize(i);
        }
    }

    const float neighborLimitedMax = std::max(minSlotSize(slotIndex), total - reserved);
    const float slotLimitedMax = resolveMaxSlotSize(_slots[static_cast<size_t>(slotIndex)]);
    return std::max(minSlotSize(slotIndex), std::min(slotLimitedMax, neighborLimitedMax));
}

float BoxBase::clampSlotSize(int slotIndex, float size) const {
    return std::clamp(size, minSlotSize(slotIndex), maxSlotSize(slotIndex));
}

VBox::VBox() {
}

VBox::~VBox() {
}

math::FVector2 BoxBase::getPreferredContentSize() const {
    // Default: just return the widget's own size. VBox overrides to
    // sum visible children. HBox stays on this default (no override).
    return getSize();
}

namespace {
// PR-B3 hotfix — walk a widget subtree and sum the natural height of
// visible children. A leaf widget (no children) returns its own size;
// a CompoundWidget sumas visible children recursively. This lets
// getPreferredContentSize drill through nested VBox/CompoundWidget
// stacks to find the real content height — when a VBox wraps a VBox
// that wraps the page widgets, the parent's getSize() is the viewport
// but the inner VBox's stack of fixed-height children tells us the
// real content height.
float walkNaturalHeight(const Widget* w) {
    if (w == nullptr) return 0.0f;
    if (!w->isVisible()) return 0.0f;
    // PR-B3 follow-up: if this widget is itself a VBox, prefer its
    // cached natural height (sum of children's natural heights
    // before fill stretch). The walker otherwise sums the stretched
    // heights of every descendant and reports a content extent
    // equal to the viewport — which makes ScrollView conclude "no
    // overflow" and disable the scrollbar even when pages actually
    // overflow.
    if (const auto* vb = dynamic_cast<const VBox*>(w)) {
        const float cached = vb->getCachedNaturalHeight();
        if (cached >= 0.0f) return cached;
    }
    if (w->getChildren().empty()) return w->getSize().y;
    float total = 0.0f;
    for (const Widget* c : w->getChildren()) {
        total += walkNaturalHeight(c);
    }
    return total;
}
float walkNaturalMaxWidth(const Widget* w) {
    if (w == nullptr || !w->isVisible()) return 0.0f;
    if (w->getChildren().empty()) return w->getSize().x;
    float maxW = 0.0f;
    for (const Widget* c : w->getChildren()) {
        const float cw = w->getSize().x > 0.0f ? w->getSize().x : walkNaturalMaxWidth(c);
        if (cw > maxW) maxW = cw;
    }
    return maxW;
}
} // namespace

math::FVector2 VBox::getPreferredContentSize() const {
    // PR-B3 hotfix (Bug #4 follow-up) — prefer the cached natural
    // height (computed in layoutChildren BEFORE the fill stretch).
    // The natural height is the sum of children's natural heights +
    // spacing + padding, which is what a ScrollView wrapping this
    // VBox needs to know to compute its scrollable extent.
    //
    // Fallback: if layout hasn't run yet (_naturalHeight == -1.0f),
    // compute on the fly via the walkNaturalHeight walker. The
    // walker itself remains in place — tests and hosts that call
    // getPreferredContentSize without first laying out the tree
    // still get a sensible answer.
    float maxW = 0.0f;
    for (const auto& slot : _slots) {
        if (slot.widget == nullptr || !slot.widget->isVisible()
            || slot.widget->getParent() != this) {
            continue;
        }
        const float cw = std::max(
            slot.widget->getSize().x,
            walkNaturalMaxWidth(slot.widget));
        if (cw > maxW) maxW = cw;
    }
    const float w = std::max(maxW, getSize().x);
    float totalH = _naturalHeight;
    if (totalH < 0.0f) {
        // Layout hasn't run — fall back to the walker.
        totalH = _padding.y + _padding.w;
        size_t visibleCount = 0;
        for (const auto& slot : _slots) {
            if (slot.widget == nullptr || !slot.widget->isVisible()
                || slot.widget->getParent() != this) {
                continue;
            }
            const float slotH = (slot.size > 0.0f)
                ? slot.size
                : walkNaturalHeight(slot.widget);
            totalH += slotH;
            if (visibleCount > 0) totalH += _spacing;
            ++visibleCount;
        }
    }
    return math::FVector2(w, totalH);
}

void VBox::layoutChildren() {
    math::FVector2 size = getSize();
    float availableWidth = std::max(0.0f, size.x - _padding.x - _padding.z);

    size_t childCount = _slots.size();
    if (childCount == 0) { _naturalHeight = 0.0f; return; }

    float totalFixedHeight = 0.0f;
    size_t fillCount = 0;
    size_t visibleSlotCount = 0;
    for (const auto& slot : _slots) {
        if (slot.widget == nullptr || !slot.widget->isVisible()
            || slot.widget->getParent() != this) {
            continue;
        }
        ++visibleSlotCount;
        if (slot.isSplitter) {
            totalFixedHeight += SplitterHandle::kDefaultWidth;
        } else if (slot.size > 0.0f) {
            totalFixedHeight += slot.size;
        } else {
            fillCount++;
        }
        totalFixedHeight += _spacing;
    }
    if (visibleSlotCount > 0) {
        totalFixedHeight -= _spacing;
    }

    float availableHeight = std::max(0.0f, size.y - _padding.y - _padding.w);
    float fillHeight = (fillCount > 0) ? (availableHeight - totalFixedHeight) / fillCount : 0.0f;
    fillHeight = std::max(0.0f, fillHeight);

    // PR-B3 hotfix (Bug #4 follow-up) — cache the natural height (sum
    // of children's natural heights, *no fill stretch*) so
    // getPreferredContentSize can return a truthful content extent.
    // Without this, a content-fills-viewport VBox reports a preferred
    // size equal to the viewport (children stretched to absorb the
    // space), and the wrapping ScrollView concludes "no overflow,
    // scrollbar disabled" — even though a tall page like
    // page_capabilities (30+ items) genuinely overflows.
    //
    // Computing on each layout pass is O(n) where n = number of slots;
    // walking recursively would be O(n^2) in nested-VBox cases. Caching
    // here also lets widget swaps (e.g. setContent) invalidate cleanly:
    // the next layoutChildren call updates _naturalHeight.
    float naturalH = _padding.y + _padding.w;
    for (size_t k = 0; k < _slots.size(); ++k) {
        const auto& slot = _slots[k];
        if (slot.widget == nullptr || !slot.widget->isVisible()
            || slot.widget->getParent() != this) {
            continue;
        }
        const float naturalChildH = slot.isSplitter
            ? SplitterHandle::kDefaultWidth
            : ((slot.size > 0.0f) ? slot.size : walkNaturalHeight(slot.widget));
        naturalH += naturalChildH + _spacing;
    }
    if (visibleSlotCount > 0) {
        naturalH -= _spacing;
    }
    _naturalHeight = naturalH;

    float x = _padding.x;
    float y = _padding.y;

    for (auto& slot : _slots) {
        if (slot.widget == nullptr || !slot.widget->isVisible()
            || slot.widget->getParent() != this) {
            if (slot.widget != nullptr && slot.widget->getParent() == this
                && slot.widget->isLayoutSizeManaged()) {
                slot.widget->setSize(math::FVector2(0.0f, 0.0f));
            }
            continue;
        }

        float childHeight;
        if (slot.isSplitter) {
            childHeight = SplitterHandle::kDefaultWidth;
        } else if (slot.size > 0.0f) {
            childHeight = slot.size;
        } else {
            childHeight = fillHeight;
        }
        float childWidth = availableWidth;

        if (slot.widget->isLayoutPositionManaged()) {
            slot.widget->setPosition(math::FVector2(x, y));
        }
        if (slot.widget->isLayoutSizeManaged()) {
            slot.widget->setSize(math::FVector2(childWidth, childHeight));
        }
        if (!slot.widget->getChildren().empty()) {
            slot.widget->performLayout();
        }

        y += childHeight + _spacing;
    }
}

HBox::HBox() {
}

HBox::~HBox() {
}

void HBox::layoutChildren() {
    math::FVector2 size = getSize();
    const float availableHeight = size.y - _padding.y - _padding.w;

    if (_slots.empty()) {
        return;
    }

    // Repair slot cache: a splitter mis-tagged as fill (old dynamic_cast
    // path / size=0) would own hundreds of px and never receive leave.
    for (Slot& slot : _slots) {
        if (slot.widget != nullptr && slot.widget->isSplitterHandle()) {
            slot.isSplitter = true;
            slot.size = SplitterHandle::kDefaultWidth;
        }
    }

    auto slotOccupiesSpace = [this](const Slot& slot) -> bool {
        return slot.widget != nullptr && slot.widget->isVisible()
            && slot.widget->getParent() == this;
    };

    float totalFixedWidth = 0.0f;
    size_t fillCount = 0;
    size_t visibleSlotCount = 0;
    for (const auto& slot : _slots) {
        if (!slotOccupiesSpace(slot)) {
            continue;
        }
        ++visibleSlotCount;
        if (slot.isSplitter) {
            totalFixedWidth += SplitterHandle::kDefaultWidth;
        } else if (slot.size > 0.0f) {
            totalFixedWidth += slot.size;
        } else {
            fillCount++;
        }
        totalFixedWidth += _spacing;
    }
    if (visibleSlotCount > 0) {
        totalFixedWidth -= _spacing;
    }

    const float availableWidth = size.x - _padding.x - _padding.z;
    const float fillWidth =
        (fillCount > 0) ? std::max(0.0f, (availableWidth - totalFixedWidth) / fillCount) : 0.0f;

    float x = _padding.x;
    const float y = _padding.y;

    for (auto& slot : _slots) {
        if (!slotOccupiesSpace(slot)) {
            if (slot.widget != nullptr && slot.widget->getParent() == this
                && slot.widget->isLayoutSizeManaged()) {
                slot.widget->setSize(math::FVector2(0.0f, 0.0f));
            }
            continue;
        }

        float childWidth = fillWidth;
        if (slot.isSplitter) {
            childWidth = SplitterHandle::kDefaultWidth;
        } else if (slot.size > 0.0f) {
            childWidth = slot.size;
        }

        if (slot.widget->isLayoutPositionManaged()) {
            slot.widget->setPosition(math::FVector2(x, y));
        }
        slot.widget->setSize(math::FVector2(childWidth, availableHeight));
        if (!slot.widget->getChildren().empty()) {
            slot.widget->performLayout();
        }

        x += childWidth + _spacing;
    }
}

} // namespace ayt::ui
