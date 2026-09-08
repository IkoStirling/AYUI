#pragma once

#include <string>
#include <vector>

namespace ayt::ui {

struct LayoutTextureResource {
    std::string key;
    std::wstring displayName;
};

class LayoutResourceCatalog {
public:
    void setEntries(std::vector<LayoutTextureResource> entries);
    void setFilter(std::wstring filter);
    const std::wstring& filter() const { return _filter; }

    const std::vector<LayoutTextureResource>& entries() const {
        return _entries;
    }
    const std::vector<size_t>& visibleIndices() const { return _visible; }
    std::vector<std::wstring> visibleLabels() const;
    const LayoutTextureResource* visibleEntry(size_t index) const;
    bool contains(const std::string& key) const;

private:
    void rebuildVisible();

    std::vector<LayoutTextureResource> _entries;
    std::vector<size_t> _visible;
    std::wstring _filter;
};

} // namespace ayt::ui
