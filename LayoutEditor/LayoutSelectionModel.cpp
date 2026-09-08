#include "AYUI/LayoutEditor/LayoutSelectionModel.h"

#include "AYUI/Widget.h"

#include <algorithm>

namespace ayt::ui {

void LayoutSelectionModel::clear() {
    _primary = nullptr;
    _items.clear();
}

void LayoutSelectionModel::setSingle(Widget* widget) {
    _items.clear();
    if (widget != nullptr) _items.push_back(widget);
    _primary = widget;
}

bool LayoutSelectionModel::add(Widget* widget) {
    if (widget == nullptr || contains(widget)) return false;
    _items.push_back(widget);
    _primary = widget;
    return true;
}

bool LayoutSelectionModel::remove(Widget* widget) {
    const auto it = std::find(_items.begin(), _items.end(), widget);
    if (it == _items.end()) return false;
    _items.erase(it);
    if (_primary == widget) {
        _primary = _items.empty() ? nullptr : _items.back();
    }
    return true;
}

bool LayoutSelectionModel::contains(const Widget* widget) const {
    return std::find(_items.begin(), _items.end(), widget) != _items.end();
}

void LayoutSelectionModel::setPrimary(Widget* widget) {
    _primary = widget;
    if (widget != nullptr && !contains(widget)) _items.push_back(widget);
}

void LayoutSelectionModel::prune(
    const std::function<bool(Widget*)>& keep) {
    _items.erase(std::remove_if(_items.begin(), _items.end(),
        [&](Widget* widget) { return widget == nullptr || !keep(widget); }),
        _items.end());
    if (_primary == nullptr || !contains(_primary)) {
        _primary = _items.empty() ? nullptr : _items.back();
    }
}

std::vector<std::string> LayoutSelectionModel::selectedIds() const {
    std::vector<std::string> result;
    result.reserve(_items.size());
    for (Widget* widget : _items) {
        if (widget != nullptr && !widget->getId().empty()) {
            result.push_back(widget->getId());
        }
    }
    return result;
}

} // namespace ayt::ui
