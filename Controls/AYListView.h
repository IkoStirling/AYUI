#pragma once

#include "AYWidget.h"
#include "AYInteractiveWidget.h"
#include "AYScrollBar.h"
#include "AYScrollableWidget.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// C-5 ListView: a single/multi-select list of text rows.
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
// Click on a row selects it; double-click selects + fires _onItemActivated.
// Keyboard navigation (Up / Down / Home / End / PageUp / PageDown) is
// delegated to whoever holds focus — v1 ships only mouse + double-click.
// Hosts that want keyboard navigation wire onKeyDown externally (e.g.
// ComboBox's popup).

class ListView : public CompoundWidget {
public:
    // Row widget — InteractiveWidget so it picks up the hover/press state
    // machine for free. Each row holds a single string. The list controls
    // selection by toggling _selected on the row widgets.
    class Row : public InteractiveWidget {
    public:
        Row();
        ~Row() override;

        const std::wstring& getText() const { return _text; }
        void setText(const std::wstring& text) { _text = text; }

        int getIndex() const { return _index; }
        void setIndex(int idx) { _index = idx; }

        bool isSelected() const { return _selected; }
        void setSelected(bool s);

        bool onMouseButtonUp(const UIMouseEvent& e) override;

    protected:
        void onRender(IRenderBackend& renderer) override;

    private:
        std::wstring _text;
        int _index = -1;
        bool _selected = false;
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
