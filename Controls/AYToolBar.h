#pragma once

// =============================================================================
// C-11 ToolBar: a horizontal strip of ToolButtons + optional separators.
// =============================================================================
//
// Architecture (v1):
//   ToolBar (CompoundWidget)
//     └─ horizontally-laid-out children: Button, Separator
//
// Each ToolButton is just a regular `Button` (already in C-1 lane). We do
// not introduce a new "ToolButton" class — the only difference (no padding)
// is a host-side styling choice. ToolBar lays them out left-to-right with
// optional visual separation between groups.
//
// -----------------------------------------------------------------------------
// v1 design decisions + known limitations + v1.1 upgrade paths
// -----------------------------------------------------------------------------
//
// DECISION 1: ToolBar's children can be Button, Separator, or any other
//   widget. We do not enforce a particular type — the container just
//   lays them out in order with kItemSpacing.
//
// DECISION 2: ToolBar is non-scrolling in v1. When too many items are
//   added, they spill past the right edge.
//   v1.1 upgrade: horizontal scroll mode (similar to ScrollView) toggled
//   when content width exceeds viewport width.
//
// DECISION 3: ToolBar supports vertical orientation in v1.1; v1 ships
//   horizontal-only.

#include "AYWidget.h"
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

private:
    struct ItemRecord {
        enum class Kind { Button, Separator };
        Kind kind;
        Widget* widget;
    };
    std::vector<ItemRecord> _items;
};

Widget* createToolBarWidget();

} // namespace ayt::ui
