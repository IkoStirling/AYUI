#pragma once
#include <limits>
#include <unordered_map>
#include <vector>

namespace ayt::ui::authoring {
/** @brief Owner-scoped visible-row to ID mapping, independent of Widget state.
 * Hosts supply filtered IDs and retain their selected ID when refreshing.
 * Duplicate IDs reject replacement atomically. idAt pointers expire on replace;
 * copy the ID before calling an owner callback that may rebuild the list.
 */
template<class Id> class StableListRows {
public:
    bool replace(std::vector<Id> ids) {
        if (ids.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return false;
        std::unordered_map<Id, int> indices;
        indices.reserve(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (!indices.emplace(ids[i], static_cast<int>(i)).second) return false;
        }
        _ids = std::move(ids);
        _indices = std::move(indices);
        return true;
    }
    const Id* idAt(int row) const {
        return row < 0 || static_cast<std::size_t>(row) >= _ids.size() ? nullptr : &_ids[row];
    }
    int indexOf(const Id& id) const {
        const auto found = _indices.find(id);
        return found == _indices.end() ? -1 : found->second;
    }
    std::size_t size() const { return _ids.size(); }
private:
    std::vector<Id> _ids;
    std::unordered_map<Id, int> _indices;
};
} // namespace ayt::ui::authoring
