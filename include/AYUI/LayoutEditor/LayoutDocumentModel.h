#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ayt::ui {

class Widget;

class LayoutDocumentModel {
public:
    Widget* root() const { return _root; }
    Widget*& rootRef() { return _root; }
    void setRoot(Widget* root);

    const std::string& path() const { return _path; }
    std::string& pathRef() { return _path; }
    void setPath(std::string path) { _path = std::move(path); }

    bool isDirty() const { return _dirty; }
    bool& dirtyRef() { return _dirty; }
    void setDirty(bool dirty) { _dirty = dirty; }

    void rebuildIndex(const std::vector<Widget*>& authoredWidgets);
    Widget* findById(const std::string& id) const;
    bool contains(const Widget* widget) const;

    bool validateId(const std::string& candidate, const Widget* edited,
                    std::wstring* reason = nullptr) const;
    bool hasDuplicateIds() const { return !_duplicateIds.empty(); }
    const std::unordered_set<std::string>& duplicateIds() const {
        return _duplicateIds;
    }

private:
    Widget* _root = nullptr;
    std::string _path;
    bool _dirty = false;
    std::unordered_map<std::string, Widget*> _widgetsById;
    std::unordered_set<const Widget*> _widgets;
    std::unordered_set<std::string> _duplicateIds;
};

} // namespace ayt::ui
