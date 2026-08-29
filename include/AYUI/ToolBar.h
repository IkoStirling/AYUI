#pragma once

// Horizontal strip of Buttons and separators. Items live in an internal
// content strip wrapped by ScrollView; overflow enables horizontal scrolling
// instead of painting past the ToolBar bounds. ToolBar owns all items and the
// internal scroll widgets. Vertical orientation is not currently exposed.

#include "AYUI/Widget.h"
#include <string>
#include <vector>

namespace ayt::ui {

class ToolBar : public CompoundWidget {
public:
    static constexpr float kDefaultWidth  = 400.0f;
    static constexpr float kDefaultHeight = 32.0f;
    static constexpr float kItemSpacing   = 4.0f;
    static constexpr float kPadding       = 4.0f;

    ToolBar();
    ~ToolBar() override;

    // Add a button with the given text + click callback. Returns the
    // Button so callers can skin / disable / wire more callbacks.
    class Button* addButton(const std::wstring& text,
                            std::function<void()> onClick = nullptr);

    // Insert a vertical separator.
    void addSeparator();

    // Index helpers.
    size_t getItemCount() const { return _items.size(); }
    Widget* getItem(size_t index) const;
    void clearItems();

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

    // G3 — overflow accessor. Exposed so the host (or tests) can inspect
    // whether the horizontal scrollbar is currently shown.
    class ScrollView* getScrollView() const { return _scrollView; }
    class ScrollBar*  getHorizontalScrollBar() const;

private:
    struct ItemRecord {
        enum class Kind { Button, Separator };
        Kind kind;
        Widget* widget;
    };
    std::vector<ItemRecord> _items;

    // Items are children of _contentStrip.
    // (an internal CompoundWidget); ToolBar owns the ScrollView +
    // _contentStrip and is responsible for deleting them in ~ToolBar.
    // When items' total width exceeds ToolBar width, the ScrollView
    // shows a horizontal scrollbar.
    void ensureScrollWrap();
    void layoutItems();
    void clearOwnedItems();

    class ScrollView* _scrollView  = nullptr;
    class Widget*     _contentStrip = nullptr;   // owning child of _scrollView
};

Widget* createToolBarWidget();

} // namespace ayt::ui
