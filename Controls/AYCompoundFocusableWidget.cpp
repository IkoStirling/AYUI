#include "AYCompoundFocusableWidget.h"

#include "AYWidget.h"   // for compoundDescendLayout/Tick/HitTest/Leave helpers

namespace ayt::ui {

CompoundFocusableWidget::CompoundFocusableWidget() = default;

CompoundFocusableWidget::~CompoundFocusableWidget() = default;

void CompoundFocusableWidget::performLayout() {
    compoundDescendLayout(this);
}

void CompoundFocusableWidget::tick(float dt) {
    compoundDescendTick(this, dt);
}

Widget* CompoundFocusableWidget::hitTest(const math::FVector2& worldPos) {
    return compoundDescendHitTest(this, worldPos);
}

void CompoundFocusableWidget::onMouseLeave() {
    compoundDescendLeave(this);
}

void CompoundFocusableWidget::onChildAdded(Widget* child) {
    AYUNREFERENCED_PARAM(child);
}

void CompoundFocusableWidget::onChildRemoved(Widget* child) {
    AYUNREFERENCED_PARAM(child);
}

} // namespace ayt::ui
