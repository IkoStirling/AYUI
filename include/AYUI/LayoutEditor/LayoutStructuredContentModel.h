#pragma once

#include "AYUI/RichText.h"

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class Widget;

enum class LayoutStructuredKind {
    None,
    ItemList,
    Tree,
    Tabs,
    RichText
};

struct LayoutStructuredEntry {
    std::wstring label;
    int depth = 0;
    int parentIndex = -1;
    bool expanded = false;
    RichRun richRun;
};

// Typed adapter over collection-like widget payloads. It deliberately edits
// the control's public model APIs instead of treating implementation children
// as document nodes.
class LayoutStructuredContentModel {
public:
    using PageFactory = std::function<Widget*()>;

    void bind(Widget* widget);
    Widget* widget() const { return _widget; }
    LayoutStructuredKind kind() const { return _kind; }
    const std::vector<LayoutStructuredEntry>& entries() const {
        return _entries;
    }
    std::vector<std::wstring> displayLabels() const;

    int selectedIndex() const { return _selectedIndex; }
    void setSelectedIndex(int index);

    bool add(const std::wstring& label, bool asChild = false,
             PageFactory pageFactory = {});
    bool removeSelected();
    bool moveSelected(int delta);
    bool setSelectedLabel(const std::wstring& label);
    bool setSelectedRichRun(const RichRun& run);

    void refresh();

private:
    bool replaceItemList(const std::vector<std::wstring>& items);
    int adjacentTreeSibling(int index, int delta) const;

    Widget* _widget = nullptr;
    LayoutStructuredKind _kind = LayoutStructuredKind::None;
    std::vector<LayoutStructuredEntry> _entries;
    int _selectedIndex = -1;
};

} // namespace ayt::ui
