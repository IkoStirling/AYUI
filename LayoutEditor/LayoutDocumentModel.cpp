#include "AYUI/LayoutEditor/LayoutDocumentModel.h"

#include "AYUI/Widget.h"

#include <cctype>

namespace ayt::ui {

void LayoutDocumentModel::setRoot(Widget* root) {
    _root = root;
    _widgetsById.clear();
    _widgets.clear();
    _duplicateIds.clear();
}

void LayoutDocumentModel::rebuildIndex(
    const std::vector<Widget*>& authoredWidgets) {
    _widgetsById.clear();
    _widgets.clear();
    _duplicateIds.clear();
    for (Widget* widget : authoredWidgets) {
        if (widget == nullptr) continue;
        _widgets.insert(widget);
        const std::string& id = widget->getId();
        if (id.empty()) continue;
        const auto [it, inserted] = _widgetsById.emplace(id, widget);
        if (!inserted && it->second != widget) {
            _duplicateIds.insert(id);
        }
    }
}

Widget* LayoutDocumentModel::findById(const std::string& id) const {
    const auto it = _widgetsById.find(id);
    return it != _widgetsById.end() ? it->second : nullptr;
}

bool LayoutDocumentModel::contains(const Widget* widget) const {
    return widget != nullptr && _widgets.find(widget) != _widgets.end();
}

bool LayoutDocumentModel::validateId(const std::string& candidate,
                                     const Widget* edited,
                                     std::wstring* reason) const {
    const auto reject = [&](const wchar_t* message) {
        if (reason != nullptr) *reason = message;
        return false;
    };
    if (candidate.empty()) return reject(L"ID cannot be empty");
    if (candidate.rfind("__le", 0) == 0) {
        return reject(L"the __le prefix is reserved for editor chrome");
    }
    const auto first = static_cast<unsigned char>(candidate.front());
    if (!(std::isalpha(first) || first == '_')) {
        return reject(L"ID must start with a letter or underscore");
    }
    for (const unsigned char ch : candidate) {
        if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' ||
              ch == ':')) {
            return reject(L"use letters, digits, _, -, . or : only");
        }
    }
    const auto found = _widgetsById.find(candidate);
    if ((found != _widgetsById.end() && found->second != edited) ||
        _duplicateIds.find(candidate) != _duplicateIds.end()) {
        return reject(L"another widget already uses this ID");
    }
    return true;
}

} // namespace ayt::ui
