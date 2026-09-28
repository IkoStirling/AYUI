#pragma once
#include <AYUI/Authoring/TimelineModel.h>
#include <utility>
#include <unordered_set>
#include <unordered_map>

namespace ayt::ui::authoring {
/** @brief Normalized selection operations shared by curve and timeline views.
 * Data remains in the existing shared TimelineSelection; no document/history is owned.
 * Changed callbacks run once after an actual change, including empty selection.
 * Remapping is positional and atomic; owners supply opaque, valid IDs.
 */
class TimelineSelectionOps {
public:
    using Changed = std::function<void(const TimelineSelection&)>;
    static bool replace(TimelineSelection& selection, TimelineSelection next, Changed changed = {}) {
        std::vector<std::string> keys;
        std::unordered_set<std::string> seen;
        seen.reserve(next.keyIds.size());
        for (auto& id : next.keyIds) if (!id.empty() && seen.emplace(id).second) keys.push_back(std::move(id));
        next.keyIds = std::move(keys);
        if (std::find(next.keyIds.begin(), next.keyIds.end(), next.primaryKeyId) == next.keyIds.end())
            next.primaryKeyId = next.keyIds.empty() ? std::string{} : next.keyIds.front();
        if (selection.trackId == next.trackId && selection.primaryKeyId == next.primaryKeyId
            && selection.keyIds == next.keyIds && selection.component == next.component) return false;
        selection = std::move(next);
        if (changed) changed(selection);
        return true;
    }
    static bool single(TimelineSelection& selection, std::string track, std::string key,
                       std::size_t component = 0, bool preserveMulti = false, Changed changed = {}) {
        auto keys = preserveMulti && selection.trackId == track
            && std::find(selection.keyIds.begin(), selection.keyIds.end(), key) != selection.keyIds.end()
            ? selection.keyIds : key.empty() ? std::vector<std::string>{} : std::vector<std::string>{key};
        return replace(selection, {std::move(track), std::move(key), std::move(keys), component}, std::move(changed));
    }
    static bool keys(TimelineSelection& selection, std::string track, std::vector<std::string> ids,
                     std::size_t component = 0, Changed changed = {}) {
        const auto primary = ids.empty() ? std::string{} : ids.front();
        return replace(selection, {std::move(track), primary, std::move(ids), component}, std::move(changed));
    }
    static bool clear(TimelineSelection& selection, Changed changed = {}) {
        return keys(selection, selection.trackId, {}, selection.component, std::move(changed));
    }
    static bool remap(TimelineSelection& selection, const std::vector<std::string>& before,
                      const std::vector<std::string>& after, Changed changed = {}) {
        if (before.size() != after.size()) return false;
        auto next = selection;
        std::unordered_map<std::string, std::string> mapping;
        mapping.reserve(before.size());
        for (std::size_t i = 0; i < before.size(); ++i) mapping.try_emplace(before[i], after[i]);
        const auto map = [&](std::string& id) {
            if (const auto found = mapping.find(id); found != mapping.end()) id = found->second;
        };
        map(next.primaryKeyId);
        for (auto& id : next.keyIds) map(id);
        return replace(selection, std::move(next), std::move(changed));
    }
};
} // namespace ayt::ui::authoring
