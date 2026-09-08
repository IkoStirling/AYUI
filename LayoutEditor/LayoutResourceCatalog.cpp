#include "AYUI/LayoutEditor/LayoutResourceCatalog.h"

#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace ayt::ui {
namespace {

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return std::towlower(ch); });
    return value;
}

} // namespace

void LayoutResourceCatalog::setEntries(
    std::vector<LayoutTextureResource> entries) {
    std::unordered_set<std::string> keys;
    _entries.clear();
    _entries.reserve(entries.size());
    for (LayoutTextureResource& entry : entries) {
        if (entry.key.empty() || !keys.insert(entry.key).second) continue;
        if (entry.displayName.empty()) {
            entry.displayName.assign(entry.key.begin(), entry.key.end());
        }
        _entries.push_back(std::move(entry));
    }
    std::sort(_entries.begin(), _entries.end(),
              [](const auto& a, const auto& b) {
                  return lower(a.displayName) < lower(b.displayName);
              });
    rebuildVisible();
}

void LayoutResourceCatalog::setFilter(std::wstring filter) {
    _filter = std::move(filter);
    rebuildVisible();
}

std::vector<std::wstring> LayoutResourceCatalog::visibleLabels() const {
    std::vector<std::wstring> result;
    result.reserve(_visible.size());
    for (size_t index : _visible) result.push_back(_entries[index].displayName);
    return result;
}

const LayoutTextureResource* LayoutResourceCatalog::visibleEntry(
    size_t index) const {
    if (index >= _visible.size()) return nullptr;
    return &_entries[_visible[index]];
}

bool LayoutResourceCatalog::contains(const std::string& key) const {
    return std::any_of(_entries.begin(), _entries.end(),
                       [&key](const auto& entry) { return entry.key == key; });
}

void LayoutResourceCatalog::rebuildVisible() {
    _visible.clear();
    const std::wstring needle = lower(_filter);
    for (size_t i = 0; i < _entries.size(); ++i) {
        const LayoutTextureResource& entry = _entries[i];
        std::wstring searchable = entry.displayName;
        searchable.append(L" ");
        searchable.append(entry.key.begin(), entry.key.end());
        if (needle.empty() || lower(std::move(searchable)).find(needle)
                                  != std::wstring::npos) {
            _visible.push_back(i);
        }
    }
}

} // namespace ayt::ui
