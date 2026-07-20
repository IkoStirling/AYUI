#pragma once

#include "AYCompoundFocusableWidget.h"
#include "AYWidget.h"
#include "AYInteractiveWidget.h"
#include "AYSelectableWidget.h"
#include "AYScrollBar.h"
#include "AYScrollableWidget.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// =============================================================================
// C-5 ListView: a single/multi-select list of text rows.
// =============================================================================
//
// Architecture (v1.1 — Phase E):
//   ListView (CompoundWidget)
//     └─ vScrollBar: ScrollBar* (auto-managed)
//     └─ row widgets: InteractiveWidget* x N (one per item — single mode)
//                     OR row pool K (visible + buffer — post-G2)
//
// Selection model (v1.1 — Phase E):
//   - Single mode (v1 default): _selectedIndex == -1 means no selection.
//   - Extended mode (G1): _selectedIndices is the authoritative vector;
//     _selectedIndex mirrors _selectedIndices.back() for v1 callers.
//   - setSelectedIndex(int) is the v1 single-int front; in Extended mode
//     it routes through setSelectedIndices.
//   - setItems replaces the row pool; the selection moves to track the
//     same item text if present, else -1 (single-mode behavior).
//
// -----------------------------------------------------------------------------
// G1 multi-select API surface
// -----------------------------------------------------------------------------
// Public:
//   enum class SelectionMode { Single, Extended }
//   void  setSelectionMode(SelectionMode m);
//   int   getSelectedIndex() const;             // -1 or last selected
//   void  setSelectedIndex(int index);          // routes via setSelectedIndices
//   const std::vector<int>& getSelectedIndices() const;
//   void  setSelectedIndices(const std::vector<int>& indices);
//   bool  isSelected(int index) const;
//   void  clearSelection();
//   int   getAnchorIndex() const;               // for Shift+click range
//   void  setAnchorIndex(int idx);
//   void  setOnSelectionIndicesChanged(std::function<void(const std::vector<int>&)>);
//   const std::wstring& getSelectedItem() const;
//
// Backwards-compat (v1 consumers):
//   - ComboBox::ensurePopupCreated locks popup to SelectionMode::Single
//   - TabStrip / TreeView: single-int paths still work (extended vector
//     holds 0 or 1 entries in single mode)
//   - JSON round-trip: `selectedIndex` (singular) preserved in single mode;
//     `selectedIndices` (array) + `selectionMode` (int) added in extended
// -----------------------------------------------------------------------------
//
// KEYBOARD NAVIGATION (Phase B — B1, S3):
//   ListView extends CompoundFocusableWidget so the focus traversal in
//   UIManager::focusNext/focusPrev can land on a ListView directly.
//   ListView::onKeyDown routes Up/Down/Home/End/PageUp/PageDown/Enter
//   to setSelectedIndex + scrollToIndex.
//
//   In Extended mode (G1):
//   - Shift+Up/Down extends the range from anchorIndex
//   - Ctrl+A selects all
//   - Esc clears selection
// =============================================================================

class ListView : public CompoundFocusableWidget {
public:
    // G1 — selection mode. v1 was always Single; v1.1 adds Extended
    // (multi-select with Ctrl/Shift click semantics + range select).
    // ComboBox's popup ListView is locked to Single (via
    // ensurePopupCreated's explicit setSelectionMode(Single)) so the
    // popup's existing single-select contract is unchanged.
    enum class SelectionMode {
        Single,        // exactly 0 or 1 selected (v1 behavior)
        Extended,      // 0..N selected; Ctrl+click toggle, Shift+click range
    };

    // Row widget — C-11 promoted from InteractiveWidget to
    // SelectableWidget. Holds a single string + index. List controls
    // selection externally via setSelected on the new row + setSelected
    // (false) on the previous one — single-selection model.
    class Row : public SelectableWidget {
    public:
        Row();
        ~Row() override;

        using SelectableWidget::setSelected;
        using SelectableWidget::isSelected;

        const std::wstring& getText() const { return _text; }
        void setText(const std::wstring& text) { _text = text; }

        int getIndex() const { return _index; }
        void setIndex(int idx) { _index = idx; }

        bool onMouseButtonUp(const UIMouseEvent& e) override;

    protected:
        void onRender(IRenderBackend& renderer) override;

    private:
        std::wstring _text;
        int _index = -1;
        // G1 — callback now carries (index, modifiers). Single-row code
        // paths that ignore modifiers still work; modifier-aware routing
        // (Ctrl/Shift) lives in ListView::handleRowClick. Pre-G1 the
        // signature was `void(int)`, but the only known wiring is the
        // row lambda ListView sets in rebuildRows / rebuildVisibleRows,
        // so this is internal.
        std::function<void(int, uint32_t)> _onClickByRow;
        friend class ListView;
    };

    ListView();
    ~ListView() override;

    // Data
    void setItems(const std::vector<std::wstring>& items);
    void addItem(const std::wstring& item);
    void clearItems();
    size_t getItemCount() const { return _items.size(); }
    const std::wstring& getItem(size_t index) const;
    const std::vector<std::wstring>& getItemsRef() const { return _items; }

    // G1 — selection mode. Default Single (v1). Switching modes does
    // NOT clear existing selection (host can do that explicitly via
    // clearSelection); only changes the dispatch semantics of subsequent
    // clicks / key events.
    void          setSelectionMode(SelectionMode m) { _selectionMode = m; }
    SelectionMode getSelectionMode() const          { return _selectionMode; }

    // Selection — single-mode shortcut (preserves v1 API).
    //   - In Single mode: returns -1 if empty, else first (and only).
    //   - In Extended mode: returns the last selected index (single-int
    //     summary; multi-mode callers should use getSelectedIndices).
    int  getSelectedIndex() const;
    void setSelectedIndex(int index);

    // G1 — extended-mode API. Sorted ascending (host can rely on this).
    const std::vector<int>& getSelectedIndices() const { return _selectedIndices; }
    void setSelectedIndices(const std::vector<int>& indices);
    bool isSelected(int index) const;
    void clearSelection();

    // G1 — anchor for Shift+click range selection.
    // In Single mode _anchorIndex tracks getSelectedIndex().
    // In Extended mode anchors on click (Ctrl OR plain) and stays put
    // during Shift+click range select.
    int  getAnchorIndex() const { return _anchorIndex; }
    void setAnchorIndex(int idx);

    // G1 — multi-mode callback. Fires on any setSelectedIndices()
    // call that actually changed the selection.
    void setOnSelectionIndicesChanged(std::function<void(const std::vector<int>&)> cb) {
        _onSelectionIndicesChanged = std::move(cb);
    }

    const std::wstring& getSelectedItem() const;

    // Callbacks
    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }
    void setOnItemActivated(std::function<void(int)> cb) {
        _onItemActivated = std::move(cb);
    }

    // Layout knobs — exposed for the host (e.g. ComboBox popup height).
    void  setItemHeight(float h) { _itemHeight = h; }
    float getItemHeight() const { return _itemHeight; }

    // G4 — visible row count cap. -1 = no cap (v1 behavior; vbar always
    // shows when content > viewport). When set > 0, used as a hint to
    // auto-hide the vertical scrollbar when items * itemHeight fits
    // within the viewport.
    void  setVisibleRowCount(int rows);
    int   getVisibleRowCount() const { return _visibleRowCount; }

    // Item-level visual offset (scroll position). Mirrors ScrollableWidget
    // contract; here the host reads it for tests + combo popup syncing.
    const math::FVector2& getScrollOffset() const { return _scrollState.getScrollOffset(); }
    void setScrollOffset(const math::FVector2& offset);

    ScrollBar* getVerticalScrollBar() const { return _vbar; }

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

    // Forward clicks to the row under the cursor. CompoundWidget default
    // doesn't descend into children when handed a mouse event directly
    // (only the hit-test descent path used by UIManager::pickWidgetAt
    // does). Tests + host code that calls ListView directly rely on this
    // forwarding so clicks on a list area still trigger row selection.
    bool onMouseButtonUp(const UIMouseEvent& e) override;

    // Phase B (B1): onMouseButtonDown grabs keyboard focus. ListView
    // owns its own keyboard state — Tab into a list, arrows cycle
    // selection, Enter activates. Returning false lets the click fall
    // through to onMouseButtonUp which routes to the row hit test.
    bool onMouseButtonDown(const UIMouseEvent& e) override;

    // Phase B (B1): keyboard navigation. Up/Down wrap; Home/End jump;
    // PageUp/PageDown step by viewport rows; Enter fires
    // _onItemActivated. Tab is NOT handled here — UIManager intercepts
    // it before delegating (R2 contract). Returns true when the key
    // was consumed.
    //
    // G1 Extended mode additions: Shift+Up/Down extends selection from
    // _anchorIndex; Ctrl+A selects all; Esc clears.
    bool onKeyDown(int keyCode) override;

protected:
    void layoutChildren() override;
    void rebuildRows();

    math::FVector2 getViewportSize() const;

    // Promoted from private → protected so Phase B (B1) ListView::onKeyDown
    // can ensure the newly-selected row is visible after a keyboard move.
    // R7 in recursive-squishing-turing.md. Subclasses / self call this
    // to keep selection in the viewport.
    void scrollToIndex(int index);

private:
    void ensureBarCreated();
    void syncBarToOffset();
    // G1 — handleRowClick now takes modifiers (Ctrl/Shift bits from
    // UIManager::_modifiers bitmask). Plain click = v1 single behavior.
    void handleRowClick(int index, uint32_t mods);

    std::vector<std::wstring> _items;
    std::vector<Row*>         _rows;     // sized == _items.size()

    // G1 — selection state. _selectedIndex is the v1 single-mode primary
    // (kept as a cache for getSelectedIndex's O(1) read; derived from
    // _selectedIndices in Extended mode). _selectedIndices is the
    // authoritative multi-mode state; empty = no selection. Anchor
    // tracks the last "click origin" for Shift+click range select.
    SelectionMode    _selectionMode = SelectionMode::Single;
    std::vector<int> _selectedIndices;
    int              _selectedIndex  = -1;   // mirror of back()/empty
    int              _anchorIndex    = -1;
    int              _rangeEndIndex  = -1;

    float _itemHeight = 24.0f;

    ScrollBar* _vbar = nullptr;
    ScrollableWidget _scrollState;
    math::FVector2 _contentSize{0.0f, 0.0f};

    std::function<void(int)> _onSelectionChanged;
    std::function<void(const std::vector<int>&)> _onSelectionIndicesChanged;
    std::function<void(int)> _onItemActivated;

    // G4 — visible row count cap. -1 = no cap (v1 behavior).
    int _visibleRowCount = -1;

    // G4 — derive vbar visibility from content vs viewport. Single
    // helper called from layoutChildren so the rule stays in one place.
    bool needsVerticalScrollBar() const;
};

Widget* createListViewWidget();

} // namespace ayt::ui