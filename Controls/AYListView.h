#pragma once

#include "AYWidget.h"
#include "AYInteractiveWidget.h"
#include "AYSelectableWidget.h"
#include "AYScrollBar.h"
#include "AYScrollableWidget.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// =============================================================================
// C-5 ListView: a single/multi-select list of text rows.
// =============================================================================
//
// Architecture (v1 — no virtualization):
//   ListView (CompoundWidget)
//     └─ vScrollBar: ScrollBar* (auto-managed)
//     └─ row widgets: InteractiveWidget* x N (one per item)
//
// Each row is a real InteractiveWidget child — not a virtualized stub. v1
// targets lists up to ~1000 rows; for larger lists a virtualization pass
// (R-11-ish) will swap the row pool in without changing the public API.
//
// Scroll behavior is delegated to the embedded ScrollBar (re-uses the
// C-4 ScrollBar contract: setRange / setValue / setViewportSize /
// setOnValueChanged → scrollBy). The ScrollableWidget mixin stores the
// offset + content size.
//
// Selection model lives here (mirrors the SelectableWidget.h contract):
//   - Single mode in v1; _selectedIndex == -1 means no selection.
//   - setSelectedIndex fires _onSelectionChanged when the value actually
//     changes (idempotent).
//   - setItems replaces the row pool; the selection moves to track the
//     same item text if present, else -1.
//
// -----------------------------------------------------------------------------
// Virtualization boundary — when to upgrade + what to change
// -----------------------------------------------------------------------------
// (This is the "future implementer" note. The current implementation makes
// an explicit trade: 1 item = 1 real widget child. This is simple and
// correct but does not scale beyond ~1000 rows. The points below pin down
// exactly where the upgrade boundary sits so the swap-in doesn't ripple
// across ComboBox / TabControl / other consumers.)
//
// TRIGGER conditions (any one is enough to justify swapping in a row pool):
//   - Item count > 1000 AND user-visible scroll jank (frame time spike
//     when scrolling). Empirically the cost is per-frame layout of N
//     children + per-frame render of N row quads.
//   - Per-item state cost too high (each row is a full InteractiveWidget
//     with a hover/press state machine — ~100 bytes of state + vtable
//     pointer × N rows).
//   - Future virtualization frameworks (imgui-style immediate-mode list,
//     TableView with sticky headers, etc.) require a different access
//     pattern than "widget child per row".
//
// WHAT MUST STAY (public API contract — do NOT change):
//   - setItems / addItem / clearItems / getItem / getItemCount
//   - getSelectedIndex / setSelectedIndex / getSelectedItem
//   - setOnSelectionChanged / setOnItemActivated
//   - setItemHeight / getItemHeight
//   - getScrollOffset / setScrollOffset
//   - getVerticalScrollBar() (caller may want to skin or hide it)
//   - JSON round-trip fields (items, selectedIndex, itemHeight)
//
// WHAT WILL CHANGE (internal — these are the upgrade seams):
//   1. _rows vector → a small pool of reused row widgets (typically
//      ceil(viewport / itemHeight) + a few). Track pool position →
//      item index via a small mapping.
//   2. `rebuildRows()` becomes `rebuildVisibleRows()` — runs on
//      layout pass + on scroll offset change, NOT on every setItems.
//   3. _items stays (it's the model). The pool just renders whatever
//      items intersect the current viewport + scrollOffset.
//   4. hitTest and onMouseButtonUp need to convert a clicked row pool
//      position BACK to a logical item index before calling
//      setSelectedIndex / handleRowClick.
//   5. scrollToIndex (currently O(1) clamping) may need to animate or
//      snap; keep the public signature, allow callers to be unaffected.
//
// WHAT BREAKS the public API and must be guarded:
//   - Row* pointers handed out via internal callbacks (only the
//     `_onClickByRow` callback uses int index, so we're already safe —
//     confirm by grep before changing the Row::onMouseButtonUp signature).
//   - getChildren() returning N rows: any caller that walks children
//     expecting row widgets will see only the pool size after the
//     upgrade. Today the only such caller is ComboBox::onMouseButtonUp
//     via _popup->getChildren().back() — that one grabs the popup
//     ListView itself, not its rows, so it's safe; verify before any
//     further ComboBox change.
//
// KEYBOARD NAVIGATION (also deferred, same v1.1 window as ComboBox):
//   ListView currently does NOT override onKeyDown. Up/Down/Home/End/
//   PageUp/PageDown navigation is the host's responsibility (e.g.
//   ComboBox's popup will need it for v1.1 keyboard support).
//   The future implementer should add ListView::onKeyDown that routes
//   arrow keys to setSelectedIndex(current + delta), capped to the
//   visible window, with scrollToIndex so the new selection is visible.
//   Test that ListView does NOT inherit FocusableWidget by itself —
//   keyboard nav is on the host side, not the list side, in v1.
//
// =============================================================================
//
// Click on a row selects it; double-click selects + fires _onItemActivated.
// Keyboard navigation (Up / Down / Home / End / PageUp / PageDown) is
// delegated to whoever holds focus — v1 ships only mouse + double-click.
// Hosts that want keyboard navigation wire onKeyDown externally (e.g.
// ComboBox's popup).

class ListView : public CompoundWidget {
public:
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
        // Callback set by the owning ListView — fires with the row index
        // when the row is clicked (single click). The list uses this to
        // route selection changes through its single _onSelectionChanged
        // callback, and to track double-click for _onItemActivated.
        std::function<void(int)> _onClickByRow;
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

    // Selection — single mode in v1.
    int  getSelectedIndex() const { return _selectedIndex; }
    void setSelectedIndex(int index);
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

protected:
    void layoutChildren() override;
    void rebuildRows();

    math::FVector2 getViewportSize() const;

private:
    void ensureBarCreated();
    void syncBarToOffset();
    void handleRowClick(int index);    // row callback → selection update
    void handleRowDouble(int index);   // row callback → activation
    void scrollToIndex(int index);     // ensure row is visible

    std::vector<std::wstring> _items;
    std::vector<Row*>         _rows;     // sized == _items.size()

    int _selectedIndex = -1;

    float _itemHeight = 24.0f;

    ScrollBar* _vbar = nullptr;
    ScrollableWidget _scrollState;
    math::FVector2 _contentSize{0.0f, 0.0f};

    std::function<void(int)> _onSelectionChanged;
    std::function<void(int)> _onItemActivated;
};

Widget* createListViewWidget();

} // namespace ayt::ui
