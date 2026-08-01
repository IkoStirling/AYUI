#include "AYBox.h"
#include "AYSplitterHandle.h"
#include "AYWindow.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

namespace {

constexpr float kWidthChangeEpsilon = 0.01f;

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

VBox::VBox() {
}

VBox::~VBox() {
}

void VBox::addWidget(Widget* widget, float height) {
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
    slot.height = height;
    _slots.push_back(slot);
}

void VBox::insertWidget(int index, Widget* widget, float height) {
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
    slot.height = height;

    if (index >= (int)_slots.size()) {
        _slots.push_back(slot);
    } else {
        _slots.insert(_slots.begin() + index, slot);
    }
}

void VBox::removeWidget(Widget* widget) {
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
}

void VBox::layoutChildren() {
    math::FVector2 size = getSize();
    float availableWidth = size.x - _padding.x - _padding.z;

    size_t childCount = _slots.size();
    if (childCount == 0) return;

    float totalFixedHeight = 0.0f;
    size_t fillCount = 0;
    size_t visibleSlotCount = 0;
    for (const auto& slot : _slots) {
        if (slot.widget == nullptr || !slot.widget->isVisible()
            || slot.widget->getParent() != this) {
            continue;
        }
        ++visibleSlotCount;
        if (slot.height > 0.0f) {
            totalFixedHeight += slot.height;
        } else {
            fillCount++;
        }
        totalFixedHeight += _spacing;
    }
    if (visibleSlotCount > 0) {
        totalFixedHeight -= _spacing;
    }

    float availableHeight = size.y - _padding.y - _padding.w;
    float fillHeight = (fillCount > 0) ? (availableHeight - totalFixedHeight) / fillCount : 0.0f;
    fillHeight = std::max(0.0f, fillHeight);

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

        float childHeight = (slot.height > 0.0f) ? slot.height : fillHeight;
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

void VBox::performLayout() {
    layoutChildren();
}

HBox::HBox() {
}

HBox::~HBox() {
}

void HBox::addWidget(Widget* widget, float width, const BoxSlotLimits& limits) {
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
    slot.width = width;
    slot.limits = limits;
    // Prefer virtual isSplitterHandle() over dynamic_cast — see Widget.h.
    slot.isSplitter = widget->isSplitterHandle();
    if (slot.isSplitter) {
        // Splitters are never fill slots; pin width so a missed loader
        // path (width=0) cannot stretch the hover band across the row.
        slot.width = SplitterHandle::kDefaultWidth;
    }
    _slots.push_back(slot);

    if (slot.isSplitter) {
        rebindSplitters();
    }
}

void HBox::insertWidget(int index, Widget* widget, float width, const BoxSlotLimits& limits) {
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
    slot.width = width;
    slot.limits = limits;
    slot.isSplitter = widget->isSplitterHandle();
    if (slot.isSplitter) {
        slot.width = SplitterHandle::kDefaultWidth;
    }

    if (index >= (int)_slots.size()) {
        _slots.push_back(slot);
        index = static_cast<int>(_slots.size()) - 1;
    } else {
        _slots.insert(_slots.begin() + index, slot);
    }

    if (slot.isSplitter) {
        rebindSplitters();
    }
}

void HBox::removeWidget(Widget* widget) {
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
}

void HBox::setSlotLimits(int slotIndex, const BoxSlotLimits& limits) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return;
    }
    _slots[static_cast<size_t>(slotIndex)].limits = limits;
}

float HBox::slotWidth(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return 0.0f;
    }
    return _slots[static_cast<size_t>(slotIndex)].width;
}

bool HBox::isSplitterSlot(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return false;
    }
    return _slots[static_cast<size_t>(slotIndex)].isSplitter;
}

int HBox::panelSlotBefore(int slotIndex) const {
    for (int i = slotIndex - 1; i >= 0; --i) {
        if (!isSplitterSlot(i)) {
            return i;
        }
    }
    return -1;
}

int HBox::panelSlotAfter(int slotIndex) const {
    for (int i = slotIndex + 1; i < static_cast<int>(_slots.size()); ++i) {
        if (!isSplitterSlot(i)) {
            return i;
        }
    }
    return -1;
}

void HBox::bindSplitter(SplitterHandle* splitter, int splitterSlotIndex) {
    if (splitter == nullptr) {
        return;
    }

    const int leftPanel = panelSlotBefore(splitterSlotIndex);
    const int rightPanel = panelSlotAfter(splitterSlotIndex);
    if (leftPanel < 0 || rightPanel < 0) {
        return;
    }

    splitter->bindPanels(this, leftPanel, rightPanel);
}

void HBox::rebindSplitters() {
    for (size_t i = 0; i < _slots.size(); ++i) {
        if (_slots[i].isSplitter) {
            bindSplitter(static_cast<SplitterHandle*>(_slots[i].widget), static_cast<int>(i));
        }
    }
}

void HBox::layoutChildren() {
    math::FVector2 size = getSize();
    const float availableHeight = size.y - _padding.y - _padding.w;

    if (_slots.empty()) {
        return;
    }

    // Repair slot cache: a splitter mis-tagged as fill (old dynamic_cast
    // path / width=0) would own hundreds of px and never receive leave.
    for (Slot& slot : _slots) {
        if (slot.widget != nullptr && slot.widget->isSplitterHandle()) {
            slot.isSplitter = true;
            slot.width = SplitterHandle::kDefaultWidth;
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
        } else if (slot.width > 0.0f) {
            totalFixedWidth += slot.width;
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
        } else if (slot.width > 0.0f) {
            childWidth = slot.width;
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

void HBox::performLayout() {
    rebindSplitters();
    layoutChildren();
}

void HBox::render(IRenderBackend& renderer) {
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

Widget* HBox::hitTest(const math::FVector2& worldPos) {
    if (!_visible || !getWorldBounds().contains(worldPos)) {
        return nullptr;
    }

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

    return this;
}

void HBox::applySplitterDrag(int leftPanelSlot, int rightPanelSlot, float mouseWorldX,
                             float dragStartMouseX, float dragStartPrimaryWidth, bool adjustLeft) {
    if (leftPanelSlot < 0 || rightPanelSlot < 0) {
        return;
    }

    const float dx = mouseWorldX - dragStartMouseX;
    const int targetIndex = adjustLeft ? leftPanelSlot : rightPanelSlot;
    const float currentWidth = _slots[static_cast<size_t>(targetIndex)].width;
    const float desiredWidth =
        adjustLeft ? (dragStartPrimaryWidth + dx) : (dragStartPrimaryWidth - dx);
    const float clampedWidth = clampSlotWidth(targetIndex, desiredWidth);

    if (std::abs(clampedWidth - currentWidth) < kWidthChangeEpsilon) {
        return;
    }

    _slots[static_cast<size_t>(targetIndex)].width = clampedWidth;
    performLayout();
}

float HBox::contentWidth() const {
    return getWidth() - _padding.x - _padding.z;
}

float HBox::resolveMinSlotWidth(const Slot& slot) const {
    const float content = contentWidth();
    float minW = kMinPanelWidth;

    if (const Window* window = dynamic_cast<const Window*>(slot.widget)) {
        minW = std::max(minW, window->getMinSize().x);
    }
    if (slot.limits.minWidth > 0.0f) {
        minW = std::max(minW, slot.limits.minWidth);
    }
    if (slot.limits.minWidthPercent > 0.0f) {
        minW = std::max(minW, content * slot.limits.minWidthPercent * 0.01f);
    }

    return minW;
}

float HBox::resolveMaxSlotWidth(const Slot& slot) const {
    const float content = contentWidth();
    float maxW = content;

    if (slot.limits.maxWidth > 0.0f) {
        maxW = std::min(maxW, slot.limits.maxWidth);
    }
    if (slot.limits.maxWidthPercent > 0.0f) {
        maxW = std::min(maxW, content * slot.limits.maxWidthPercent * 0.01f);
    }

    return maxW;
}

float HBox::minSlotWidth(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return kMinPanelWidth;
    }
    if (isSplitterSlot(slotIndex)) {
        return SplitterHandle::kDefaultWidth;
    }
    return resolveMinSlotWidth(_slots[static_cast<size_t>(slotIndex)]);
}

float HBox::maxSlotWidth(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(_slots.size())) {
        return contentWidth();
    }
    if (isSplitterSlot(slotIndex)) {
        return SplitterHandle::kDefaultWidth;
    }

    const float total = contentWidth();
    const float spacingTotal = _spacing * static_cast<float>(_slots.size() - 1);
    float reserved = spacingTotal;

    for (int i = 0; i < static_cast<int>(_slots.size()); ++i) {
        if (i == slotIndex) {
            continue;
        }
        if (isSplitterSlot(i)) {
            reserved += SplitterHandle::kDefaultWidth;
        } else if (_slots[static_cast<size_t>(i)].width > 0.0f) {
            reserved += _slots[static_cast<size_t>(i)].width;
        } else {
            reserved += minSlotWidth(i);
        }
    }

    const float neighborLimitedMax = std::max(minSlotWidth(slotIndex), total - reserved);
    const float slotLimitedMax = resolveMaxSlotWidth(_slots[static_cast<size_t>(slotIndex)]);
    return std::max(minSlotWidth(slotIndex), std::min(slotLimitedMax, neighborLimitedMax));
}

float HBox::clampSlotWidth(int slotIndex, float width) const {
    return std::clamp(width, minSlotWidth(slotIndex), maxSlotWidth(slotIndex));
}

} // namespace ayt::ui
