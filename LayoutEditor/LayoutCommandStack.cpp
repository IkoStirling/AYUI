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

void LayoutCommandStack::pushTyped(std::function<void()> undoAction,
                                   std::function<void()> redoAction,
                                   LayoutEditKind kind,
                                   std::string label) {
    _transactionOpen = false;
    Entry entry;
    entry.kind = kind;
    entry.label = std::move(label);
    entry.undoAction = std::move(undoAction);
    entry.redoAction = std::move(redoAction);
    appendUndo(std::move(entry));
    _redo.clear();
}

bool LayoutCommandStack::nextUndoIsTyped() const {
    return !_undo.empty() && _undo.back().isTyped();
}

bool LayoutCommandStack::nextRedoIsTyped() const {
    return !_redo.empty() && _redo.back().isTyped();
}

bool LayoutCommandStack::undoTyped() {
    if (!nextUndoIsTyped()) return false;
    Entry entry = std::move(_undo.back());
    _undo.pop_back();
    entry.undoAction();
    _redo.push_back(std::move(entry));
    _transactionOpen = false;
    return true;
}

bool LayoutCommandStack::redoTyped() {
    if (!nextRedoIsTyped()) return false;
    Entry entry = std::move(_redo.back());
    _redo.pop_back();
    entry.redoAction();
    appendUndo(std::move(entry));
    _transactionOpen = false;
    return true;
}

std::optional<LayoutEditorSnapshot> LayoutCommandStack::undo(
    LayoutEditorSnapshot current) {
    if (_undo.empty()) return std::nullopt;
    if (_undo.back().isTyped()) return std::nullopt;
    Entry entry = std::move(_undo.back());
    _undo.pop_back();
    _redo.push_back({std::move(current), entry.kind, entry.label});
    _transactionOpen = false;
    return std::move(entry.snapshot);
}

std::optional<LayoutEditorSnapshot> LayoutCommandStack::redo(
    LayoutEditorSnapshot current) {
    if (_redo.empty()) return std::nullopt;
    if (_redo.back().isTyped()) return std::nullopt;
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
