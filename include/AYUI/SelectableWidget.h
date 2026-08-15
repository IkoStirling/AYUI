#pragma once

// =============================================================================
// C-11 SelectableWidget: visual-selectable row mixin.
// =============================================================================
//
// Promoted from the C-5 ListView::Row contract (which was a thin
// InteractiveWidget + bool _selected). C-11 produces 2 more consumers —
// TabItem (C-9 shipped a stand-in; left untouched) and MenuItem (C-11).
// Once the count is ≥ 2, the abstraction earns its place (DESIGN.md §3.2
// note in C-11 row).
//
// Architecture (v1):
//   SelectableWidget : InteractiveWidget
//     - isSelected / setSelected(bool)
//     - onSelectionChanged(bool newValue)
//     - onActivated(void)                        // fires on click
//     - onClick / onActivated helpers            // default impl: just invoke
//
// Subclasses override handleClick() (or the whole onMouseButtonUp) to do
// their specific behavior:
//   - ListView::Row: toggle selection + call list-side _onClickByRow
//   - MenuItem: don't toggle selection (menus are one-shot activate);
//               fire _onMenuActivate
//
// The base class itself does NOT auto-toggle. This keeps ListView's
// single-selection model (the parent calls setSelected on the new row and
// setSelected(false) on the previous one) clean — children never have to
// worry about accidental state toggling when the parent routes clicks.

#include "AYUI/InteractiveWidget.h"
#include <functional>

namespace ayt::ui {

class SelectableWidget : public InteractiveWidget {
public:
    SelectableWidget();
    ~SelectableWidget() override;

    bool isSelected() const { return _selected; }
    void setSelected(bool s);

    void setOnSelectionChanged(std::function<void(bool)> cb);
    void setOnActivated(std::function<void()> cb);

    // Called by the subclass's onMouseButtonUp. Default behavior: invoke
    // _onActivated + _onClicked. Subclasses extend (e.g. setSelected
    // first, then call base).
    virtual bool handleClick();

protected:
    void fireSelectionChanged();

    bool _selected = false;
    std::function<void(bool)> _onSelectionChanged;
    std::function<void()> _onActivated;  // fires on click
};

} // namespace ayt::ui
