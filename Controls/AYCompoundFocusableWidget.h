#pragma once

#include "AYFocusableWidget.h"

namespace ayt::ui {

// CompoundFocusableWidget — Phase B (S3 keyboard nav) base for compound
// (container) widgets that also own keyboard focus: ListView, ComboBox,
// Menu, TabControl.
//
// Single inheritance from FocusableWidget. NOT multi-inheritance of
// FocusableWidget + CompoundWidget — that would be a diamond under
// Widget (both inherit Widget directly). The four CompoundWidget
// overrides (performLayout, tick, hitTest, onMouseLeave) call the same
// anonymous-namespace helpers in AYWidget.cpp that CompoundWidget uses
// itself, so behavior is byte-identical today.
//
// Consumers that DON'T need focus (Panel, ScrollView, MenuBar, ToolBar,
// Tooltip, TreeView, Window, StatusBar) stay on CompoundWidget — no
// migration cost.
class CompoundFocusableWidget : public FocusableWidget {
public:
    CompoundFocusableWidget();
    ~CompoundFocusableWidget() override;

    void performLayout() override;
    void tick(float dt) override;
    Widget* hitTest(const math::FVector2& worldPos) override;
    void onMouseLeave() override;

    // Subclasses override to position/size their own children. Mirrors
    // CompoundWidget::layoutChildren. Default empty (like CompoundWidget).
    virtual void layoutChildren() {}

protected:
    // Mirrors CompoundWidget hooks. Default empty.
    void onChildAdded(Widget* child);
    void onChildRemoved(Widget* child);
};

} // namespace ayt::ui
