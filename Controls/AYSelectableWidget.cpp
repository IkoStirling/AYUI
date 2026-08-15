#include "AYUI/SelectableWidget.h"
#include "AYUI/IRenderBackend.h"
#include <algorithm>

namespace ayt::ui {

SelectableWidget::SelectableWidget() {
    // Inherits InteractiveWidget's state machine (Normal → Hovered → Pressed).
    // v1 selected-state sits on top: rendered as a different color band
    // beneath text. v1.1 could swap to a dedicated state enum if more
    // interaction states are added.
}

SelectableWidget::~SelectableWidget() = default;

void SelectableWidget::setSelected(bool s) {
    if (_selected == s) return;
    _selected = s;
    fireSelectionChanged();
    markBoundsDirty();
}

void SelectableWidget::fireSelectionChanged() {
    if (_onSelectionChanged) _onSelectionChanged(_selected);
}

void SelectableWidget::setOnSelectionChanged(std::function<void(bool)> cb) {
    _onSelectionChanged = std::move(cb);
}

void SelectableWidget::setOnActivated(std::function<void()> cb) {
    _onActivated = std::move(cb);
}

bool SelectableWidget::handleClick() {
    // Default behavior: invoke onActivated (if any). Subclasses override
    // to also toggle selection (ListView::Row), or fire different callbacks
    // (MenuItem). Selection toggle is NOT in the base — the two-arg
    // semantics don't fit a single class cleanly.
    if (_onActivated) _onActivated();
    if (_onClicked) _onClicked();
    return true;
}

} // namespace ayt::ui
