#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class Widget;

class LayoutSelectionModel {
public:
    Widget* primary() const { return _primary; }
    Widget*& primaryRef() { return _primary; }
    const std::vector<Widget*>& items() const { return _items; }
    std::vector<Widget*>& items() { return _items; }

    void clear();
    void setSingle(Widget* widget);
    bool add(Widget* widget);
    bool remove(Widget* widget);
    bool contains(const Widget* widget) const;
    void setPrimary(Widget* widget);
    void prune(const std::function<bool(Widget*)>& keep);
    std::vector<std::string> selectedIds() const;

private:
    Widget* _primary = nullptr;
    std::vector<Widget*> _items;
};

} // namespace ayt::ui
