#include "AYBox.h"

namespace ayt::ui {

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
    Slot slot;
    slot.widget = widget;
    slot.height = height;
    _slots.push_back(slot);
    addChild(widget);
}

void VBox::insertWidget(int index, Widget* widget, float height) {
    Slot slot;
    slot.widget = widget;
    slot.height = height;

    if (index >= (int)_slots.size()) {
        _slots.push_back(slot);
    } else {
        _slots.insert(_slots.begin() + index, slot);
    }
    addChild(widget);
}

void VBox::layoutChildren() {
    math::FVector2 size = getSize();
    float availableWidth = size.x - _padding.x - _padding.z;

    size_t childCount = _slots.size();
    if (childCount == 0) return;

    float totalFixedHeight = 0.0f;
    size_t fillCount = 0;
    for (const auto& slot : _slots) {
        if (slot.height > 0.0f) {
            totalFixedHeight += slot.height;
        } else {
            fillCount++;
        }
        totalFixedHeight += _spacing;
    }
    totalFixedHeight -= _spacing;

    float availableHeight = size.y - _padding.y - _padding.w;
    float fillHeight = (fillCount > 0) ? (availableHeight - totalFixedHeight) / fillCount : 0.0f;

    float x = _padding.x;
    float y = _padding.y;

    for (auto& slot : _slots) {
        float childHeight = (slot.height > 0.0f) ? slot.height : fillHeight;
        float childWidth = availableWidth;

        childWidth -= _padding.z;

        slot.widget->setPosition(math::FVector2(x, y));
        slot.widget->setSize(math::FVector2(childWidth, childHeight));
        slot.widget->performLayout();

        y += childHeight + _spacing;
    }
}

HBox::HBox() {
}

HBox::~HBox() {
}

void HBox::addWidget(Widget* widget, float width) {
    Slot slot;
    slot.widget = widget;
    slot.width = width;
    _slots.push_back(slot);
    addChild(widget);
}

void HBox::insertWidget(int index, Widget* widget, float width) {
    Slot slot;
    slot.widget = widget;
    slot.width = width;

    if (index >= (int)_slots.size()) {
        _slots.push_back(slot);
    } else {
        _slots.insert(_slots.begin() + index, slot);
    }
    addChild(widget);
}

void HBox::layoutChildren() {
    math::FVector2 size = getSize();
    float availableHeight = size.y - _padding.y - _padding.w;

    size_t childCount = _slots.size();
    if (childCount == 0) return;

    float totalFixedWidth = 0.0f;
    size_t fillCount = 0;
    for (const auto& slot : _slots) {
        if (slot.width > 0.0f) {
            totalFixedWidth += slot.width;
        } else {
            fillCount++;
        }
        totalFixedWidth += _spacing;
    }
    totalFixedWidth -= _spacing;

    float availableWidth = size.x - _padding.x - _padding.z;
    float fillWidth = (fillCount > 0) ? (availableWidth - totalFixedWidth) / fillCount : 0.0f;

    float x = _padding.x;
    float y = _padding.y;

    for (auto& slot : _slots) {
        float childWidth = (slot.width > 0.0f) ? slot.width : fillWidth;
        float childHeight = availableHeight - _padding.w;

        slot.widget->setPosition(math::FVector2(x, y));
        slot.widget->setSize(math::FVector2(childWidth, childHeight));
        slot.widget->performLayout();

        x += childWidth + _spacing;
    }
}

} // namespace ayt::ui