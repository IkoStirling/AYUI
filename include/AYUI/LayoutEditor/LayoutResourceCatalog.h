#pragma once

#include <string>
#include <vector>

namespace ayt::ui {

enum class LayoutResourceState {
    Ready,
    Missing,
    Invalid,
};

struct LayoutTextureResource {
    // Stable, serialized identity. Hosts should prefer a project-relative or
    // virtual resource key here; it must not depend on the current machine.
    std::string key;
    std::wstring displayName;
    // Host-local path used only to decode an authoring preview. Never written
    // to the layout document. Empty means the stable key is directly loadable.
    std::string previewPath;
    std::wstring detail;
    LayoutResourceState state = LayoutResourceState::Ready;
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
    const LayoutTextureResource* find(const std::string& key) const;
    const LayoutTextureResource* findByPreviewPath(
        const std::string& previewPath) const;
    bool contains(const std::string& key) const;
    std::string resolvePreviewPath(const std::string& key) const;

private:
    void rebuildVisible();

    std::vector<LayoutTextureResource> _entries;
    std::vector<size_t> _visible;
    std::wstring _filter;
};

} // namespace ayt::ui
