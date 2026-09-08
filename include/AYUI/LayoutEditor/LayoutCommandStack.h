#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ayt::ui {

enum class LayoutEditKind {
    SnapshotFallback,
    Property,
    Insert,
    Delete,
    Reorder,
    Transform,
    Clipboard
};

struct LayoutEditorSnapshot {
    std::string json;
    std::vector<std::string> selectedIds;
    std::string primaryId;
    bool dirty = false;
};

// Transitional command history. Entries carry edit intent now, while their
// payload remains a whole-document snapshot until each operation gets a safe
// incremental command implementation.
class LayoutCommandStack {
public:
    explicit LayoutCommandStack(std::size_t maxDepth = 64)
        : _maxDepth(maxDepth) {}

    void clear();
    void push(LayoutEditorSnapshot before,
              LayoutEditKind kind = LayoutEditKind::SnapshotFallback,
              std::string label = {});
    void begin(LayoutEditorSnapshot before,
               LayoutEditKind kind = LayoutEditKind::Property,
               std::string label = {});
    void end();
    void discardLastUndo();

    std::optional<LayoutEditorSnapshot> undo(LayoutEditorSnapshot current);
    std::optional<LayoutEditorSnapshot> redo(LayoutEditorSnapshot current);

    bool canUndo() const { return !_undo.empty(); }
    bool canRedo() const { return !_redo.empty(); }
    bool transactionOpen() const { return _transactionOpen; }
    std::size_t undoDepth() const { return _undo.size(); }
    std::size_t redoDepth() const { return _redo.size(); }
    LayoutEditKind nextUndoKind() const;

private:
    struct Entry {
        LayoutEditorSnapshot snapshot;
        LayoutEditKind kind = LayoutEditKind::SnapshotFallback;
        std::string label;
    };

    void appendUndo(Entry entry);

    std::size_t _maxDepth = 64;
    std::vector<Entry> _undo;
    std::vector<Entry> _redo;
    bool _transactionOpen = false;
};

} // namespace ayt::ui
