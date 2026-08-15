#pragma once

#include "AYUI/Widget.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class RadioButton;

// =============================================================================
// G5 — RadioGroup: runtime mutex coordinator for a set of RadioButtons.
// =============================================================================
//
// Members are adopted by raw pointer (non-owning); the host owns the radio
// widgets themselves. RadioGroup fires `setChecked(false)` on peers when one
// is selected, so the host no longer writes per-group arbitration lambdas.
//
// Lifetime contract: a RadioButton added to a group MUST outlive the
// group, OR be removed via group.remove(rb) before destruction.
// Mixing multiple groups that share a radio is undefined (last
// add() wins). This matches the Qt + Win32 RadioGroup contract.
//
// Not a Widget (no rendering, no hitTest); not registered in
// WidgetFactory; not serialized as its own JSON type. The per-button
// `groupId` JSON field already round-trips; groups are reconstructed
// at runtime from that same field, or explicitly constructed in code.
//
// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
//   bool  add(RadioButton* rb);                  // adopt; dedup + null guard
//   void  remove(RadioButton* rb);               // drop + restore callbacks
//   size_t size() const;
//   RadioButton* getSelected() const;
//   int   getSelectedIndex() const;              // -1 if none
//   RadioButton* getMember(size_t index) const;
//   size_t indexOf(RadioButton* rb) const;
//   void  setSelectedIndex(int index);           // <0 clears
//   void  setOnSelectionChanged(std::function<void(int)>);
//   template<class Map> void addByIds(ids, registry);
// =============================================================================

class RadioGroup {
public:
    RadioGroup() = default;
    ~RadioGroup();

    // Adopt an existing RadioButton. Hooks into the radio's
    // _onSelected + _onToggled callbacks so the mutex is enforced
    // regardless of which path (click vs programmatic setChecked)
    // changed the state. Multiple add() of the same radio is a no-op.
    // Returns false if `rb == nullptr`; true otherwise.
    bool add(RadioButton* rb);

    // Drop a radio. Restores its pre-add callbacks. No-op if not
    // a member. After removal the radio stays in its current
    // `_checked` state; subsequent clicks only flip THIS radio.
    void remove(RadioButton* rb);

    // Number of currently adopted members.
    size_t size() const { return _members.size(); }

    // Currently-selected member, or nullptr if none.
    RadioButton* getSelected() const { return _selected; }
    int          getSelectedIndex() const;

    // Index-based access. `index` is into the add() insertion order,
    // which is the public order hosts use in JSON / docs.
    RadioButton* getMember(size_t index) const;
    size_t       indexOf(RadioButton* rb) const;

    // Programmatic selection. Replaces current selection; fires
    // _onSelectionChanged after peers are deselected.
    // index < 0 → clear selection (all peers unchecked).
    void setSelectedIndex(int index);

    // Fires with the NEW selected index after a selection change
    // (-1 if cleared). Mirrors TabControl::setOnSelectionChanged.
    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }

    // Bulk adoption from a JSON-style id list. Looks up each id in
    // `registry` (a host-side map of id → RadioButton*); ids that
    // don't resolve are silently skipped (host bug, not a crash).
    // Mirrors the Widget-by-id walk in AYLayoutLoader.
    template <typename Map>
    void addByIds(const std::vector<std::string>& ids, const Map& registry) {
        for (const auto& id : ids) {
            auto it = registry.find(id);
            if (it != registry.end() && it->second != nullptr) {
                add(it->second);
            }
        }
    }

private:
    // Internal: called by the hooked RadioButton callbacks.
    void onMemberToggled(RadioButton* rb, bool nowChecked);
    void onMemberSelected(RadioButton* rb);

    struct Hook {
        RadioButton* rb = nullptr;
        std::function<void(bool)> prevToggled;
        std::function<void()>     prevSelected;
    };
    std::vector<Hook> _members;
    RadioButton* _selected = nullptr;

    std::function<void(int)> _onSelectionChanged;

    // Re-entrancy guard: setSelectedIndex programmatically calls
    // setChecked on peers, which would re-enter onMemberToggled
    // and re-fire the mutex → infinite loop. _suppressHook tells
    // onMemberToggled to skip the mutex when WE initiated the change.
    // Matches the existing Test_RadioButton.cpp:168/176
    // `if(!v) return;` re-entrancy idiom.
    bool _suppressHook = false;
};

} // namespace ayt::ui