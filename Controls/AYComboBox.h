#pragma once

#include "AYWidget.h"
#include "AYListView.h"
#include "AYTextLabel.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// C-6 ComboBox: a single-selection popup picker.
//
// Architecture (v1):
//   ComboBox (CompoundWidget)
//     ├─ _display: TextLabel  (shows selected item text + arrow chevron)
//     └─ _popup:   ListView*  (created on first openPopup; lives as a
//                             CompoundWidget child while visible; hidden
//                             when closed; destroyed in ~ComboBox)
//
// The popup sits BELOW the main ComboBox bounds. Because popup bounds
// extend past ComboBox's own bounds, ComboBox::hitTest is overridden to
// check the popup first when it is open — otherwise CompoundWidget's
// default self-bounds check would reject clicks on the popup that lie
// outside the main ComboBox rect.
//
// Selection model: ComboBox owns its own _selectedIndex (-1 = nothing
// selected). Setting it programmatically updates both the display label
// and the popup's selection so they stay in sync. When the user picks
// from the popup, ComboBox's _onSelectionChanged fires AFTER the popup
// is closed (so listeners that dismiss further UI on selection change
// don't fight the popup).
//
// Popup height: bounded by _maxPopupItems * itemHeight. When item count
// is less than _maxPopupItems the popup shrinks accordingly.
//
// v1 non-goals: typeahead, icon column, dropdown opens UP, animated
// open/close.

class ComboBox : public CompoundWidget {
public:
    static constexpr float kDefaultWidth = 160.0f;
    static constexpr float kDefaultHeight = 28.0f;
    static constexpr int   kDefaultMaxPopupItems = 8;
    static constexpr float kPopupGap = 2.0f;
    static constexpr float kArrowWidth = 18.0f;

    ComboBox();
    ~ComboBox() override;

    // Enable / disable click + popup. ComboBox is a CompoundWidget (not
    // InteractiveWidget) because it hosts the popup ListView as a child —
    // so we maintain a simple enabled flag here rather than inheriting
    // the whole InteractiveWidget state machine.
    void setEnabled(bool enabled) { _enabled = enabled; }
    bool isEnabled() const { return _enabled; }

    // Data — wraps the inner ListView (popup) + a parallel vector kept
    // here so getSelectedItem works even when the popup is closed.
    void setItems(const std::vector<std::wstring>& items);
    void addItem(const std::wstring& item);
    void clearItems();
    size_t getItemCount() const { return _items.size(); }
    const std::wstring& getItem(size_t index) const;
    const std::vector<std::wstring>& getItemsRef() const { return _items; }

    int  getSelectedIndex() const { return _selectedIndex; }
    void setSelectedIndex(int index);
    const std::wstring& getSelectedItem() const;

    // Popup control.
    bool isPopupOpen() const { return _popupOpen; }
    void openPopup();
    void closePopup();
    void togglePopup() { if (_popupOpen) closePopup(); else openPopup(); }

    // Layout knobs.
    void setMaxPopupItems(int n) { _maxPopupItems = (n > 0 ? n : 1); }
    int  getMaxPopupItems() const { return _maxPopupItems; }

    // Callbacks.
    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }

    // Hit-test + layout override.
    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onMouseLeave() override;

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

protected:
    void layoutChildren() override;

private:
    void ensurePopupCreated();
    void syncPopupSelection();
    math::FRectangle computePopupBounds() const;

    std::vector<std::wstring> _items;
    int _selectedIndex = -1;
    int _maxPopupItems = kDefaultMaxPopupItems;
    bool _popupOpen = false;

    TextLabel* _display = nullptr;
    ListView*  _popup   = nullptr;

    bool _enabled = true;

    std::function<void(int)> _onSelectionChanged;
};

Widget* createComboBoxWidget();

} // namespace ayt::ui
