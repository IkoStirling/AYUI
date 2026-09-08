#include "AYUI/LayoutEditor/LayoutCommandStack.h"

#include <utility>

namespace ayt::ui {

void LayoutCommandStack::clear() {
    _undo.clear();
    _redo.clear();
    _transactionOpen = false;
}

void LayoutCommandStack::appendUndo(Entry entry) {
    _undo.push_back(std::move(entry));
    if (_undo.size() > _maxDepth) _undo.erase(_undo.begin());
}

void LayoutCommandStack::push(LayoutEditorSnapshot before,
                              LayoutEditKind kind,
                              std::string label) {
    _transactionOpen = false;
    appendUndo({std::move(before), kind, std::move(label)});
    _redo.clear();
}

void LayoutCommandStack::begin(LayoutEditorSnapshot before,
                               LayoutEditKind kind,
                               std::string label) {
    if (_transactionOpen) return;
    appendUndo({std::move(before), kind, std::move(label)});
    _redo.clear();
    _transactionOpen = true;
}

void LayoutCommandStack::end() {
    _transactionOpen = false;
}

void LayoutCommandStack::discardLastUndo() {
    if (!_undo.empty()) _undo.pop_back();
    _transactionOpen = false;
}

std::optional<LayoutEditorSnapshot> LayoutCommandStack::undo(
    LayoutEditorSnapshot current) {
    if (_undo.empty()) return std::nullopt;
    Entry entry = std::move(_undo.back());
    _undo.pop_back();
    _redo.push_back({std::move(current), entry.kind, entry.label});
    _transactionOpen = false;
    return std::move(entry.snapshot);
}

std::optional<LayoutEditorSnapshot> LayoutCommandStack::redo(
    LayoutEditorSnapshot current) {
    if (_redo.empty()) return std::nullopt;
    Entry entry = std::move(_redo.back());
    _redo.pop_back();
    appendUndo({std::move(current), entry.kind, entry.label});
    _transactionOpen = false;
    return std::move(entry.snapshot);
}

LayoutEditKind LayoutCommandStack::nextUndoKind() const {
    return _undo.empty() ? LayoutEditKind::SnapshotFallback
                         : _undo.back().kind;
}

} // namespace ayt::ui
