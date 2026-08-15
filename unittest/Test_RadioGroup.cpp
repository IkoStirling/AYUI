#include "AYTest.h"
#include "AYUI/RadioGroup.h"
#include "AYUI/RadioButton.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/MockRenderer.h"

#include <iostream>
#include <vector>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_RadioGroup)

// =============================================================================
// G5 — RadioGroup manager (Tier 2 polish, item 1/5)
// =============================================================================
// v1 forced hosts to write per-group arbitration lambdas (see
// Test_RadioButton.cpp:146 radiobutton_group_mutex_host_lambda). G5 adds
// a RadioGroup coordinator that owns the mutex: adopt radios via add(),
// click or setSelectedIndex → peers auto-deselect.
//
// RadioGroup is NOT a Widget, NOT factory-registered, NOT serialized as
// its own JSON type. The per-button `groupId` JSON field still round-trips
// (regression-tested by Test_RadioButton.cpp:293 factory_and_serializer
// _round_trip). Hosts reconstruct groups at runtime from a host-side
// id→RadioButton map, or build them imperatively.

// 1. Default ctor — empty group, no selection.
TEST_CASE(radiogroup_initial_state_empty) {
    RadioGroup g;
    CHECK(g.size() == 0u);
    CHECK(g.getSelected() == nullptr);
    CHECK(g.getSelectedIndex() == -1);
}

// 2. add() increments size; members are reachable by index in add-order.
TEST_CASE(radiogroup_add_increments_size) {
    RadioGroup g;
    RadioButton a, b, c;
    CHECK(g.add(&a));
    CHECK(g.add(&b));
    CHECK(g.add(&c));
    CHECK(g.size() == 3u);
    CHECK(g.getMember(0) == &a);
    CHECK(g.getMember(1) == &b);
    CHECK(g.getMember(2) == &c);
    CHECK(g.indexOf(&a) == 0u);
    CHECK(g.indexOf(&b) == 1u);
    CHECK(g.indexOf(&c) == 2u);
    // Out-of-range index returns nullptr / sentinel.
    CHECK(g.getMember(99) == nullptr);
    CHECK(g.indexOf(nullptr) == static_cast<size_t>(-1));
}

// 3. add(nullptr) is a no-op + returns false.
TEST_CASE(radiogroup_add_null_returns_false) {
    RadioGroup g;
    CHECK_FALSE(g.add(nullptr));
    CHECK(g.size() == 0u);
}

// 4. add() of the same radio twice is deduped.
TEST_CASE(radiogroup_add_same_radio_twice_is_noop) {
    RadioGroup g;
    RadioButton a;
    CHECK(g.add(&a));
    CHECK(g.add(&a));
    CHECK(g.add(&a));
    CHECK(g.size() == 1u);
}

// 5. remove() restores the pre-add callbacks. Host can rewire
//    setOnToggled after remove without leaking the group's hook.
TEST_CASE(radiogroup_remove_restores_callbacks) {
    RadioGroup g;
    RadioButton a;
    int hostHits = 0;
    a.setOnToggled([&](bool) { ++hostHits; });
    g.add(&a);
    // Trigger the group's hook (we have to drive _checked via
    // setChecked; click path uses setChecked internally too).
    a.setChecked(true);
    // Group hook fires + re-fires host's prevToggled → hostHits++.
    CHECK(hostHits == 1);
    g.remove(&a);
    // Now only host's callback is wired; group's hook is gone.
    a.setChecked(false);
    CHECK(hostHits == 2);
    // And the group's selection pointer is cleared.
    CHECK(g.getSelected() == nullptr);
}

// 6. Click-equivalent (setChecked(true)) on one member deselects peers.
//    This is the headline G5 contract — replaces the host arbitration
//    lambda with auto-mutex.
TEST_CASE(radiogroup_click_deselects_peers) {
    RadioGroup g;
    RadioButton a, b, c;
    g.add(&a);
    g.add(&b);
    g.add(&c);
    // Click rb_b — emulate the click pipeline: setChecked(true)
    // bypasses onMouseButtonUp but the result is the same boolean
    // transition.
    b.setChecked(true);
    CHECK(b.isChecked());
    CHECK_FALSE(a.isChecked());
    CHECK_FALSE(c.isChecked());
    CHECK(g.getSelected() == &b);
    CHECK(g.getSelectedIndex() == 1);
}

// 7. setSelectedIndex() programmatically selects one and deselects peers.
TEST_CASE(radiogroup_programmatic_set_selected_index_deselects_peers) {
    RadioGroup g;
    RadioButton a, b, c;
    g.add(&a);
    g.add(&b);
    g.add(&c);
    // Start: a selected via click path.
    a.setChecked(true);
    CHECK(g.getSelectedIndex() == 0);
    // Now jump to index 2 programmatically.
    g.setSelectedIndex(2);
    CHECK(c.isChecked());
    CHECK_FALSE(a.isChecked());
    CHECK_FALSE(b.isChecked());
    CHECK(g.getSelectedIndex() == 2);
}

// 8. setSelectedIndex(-1) clears all selection.
TEST_CASE(radiogroup_set_selected_index_negative_clears) {
    RadioGroup g;
    RadioButton a, b;
    g.add(&a);
    g.add(&b);
    a.setChecked(true);
    CHECK(g.getSelectedIndex() == 0);
    g.setSelectedIndex(-1);
    CHECK_FALSE(a.isChecked());
    CHECK_FALSE(b.isChecked());
    CHECK(g.getSelectedIndex() == -1);
    CHECK(g.getSelected() == nullptr);
}

// 9. _onSelectionChanged fires on real changes with the NEW index,
//    and is NOT re-fired when the same index is set twice.
TEST_CASE(radiogroup_callback_fires_with_new_index) {
    RadioGroup g;
    RadioButton a, b, c;
    g.add(&a);
    g.add(&b);
    g.add(&c);
    std::vector<int> fired;
    g.setOnSelectionChanged([&](int idx) { fired.push_back(idx); });

    // Path 1: click a → +0
    a.setChecked(true);
    // Path 2: click b → +1 (mutex also fires -1 internally for a? No —
    // the prev deselect happens via prev->setChecked(false) inside the
    // suppressHook block, so the host's _onSelectionChanged only sees
    // the net result.)
    b.setChecked(true);
    // Path 3: programmatic clear → -1
    g.setSelectedIndex(-1);
    // Path 4: redundant set → no callback (early-return)
    g.setSelectedIndex(-1);
    g.setSelectedIndex(-1);
    CHECK(fired.size() == 3u);
    CHECK(fired[0] == 0);
    CHECK(fired[1] == 1);
    CHECK(fired[2] == -1);
}

// 10. Re-entrancy guard: programmatic setSelectedIndex from one
//     selected member to another does NOT cause an infinite loop
//     or spurious extra callback firings. We verify by counting
//     callback fires.
TEST_CASE(radiogroup_re_entrancy_guard_prevents_infinite_loop) {
    RadioGroup g;
    RadioButton a, b, c;
    g.add(&a);
    g.add(&b);
    g.add(&c);
    int callbackCount = 0;
    g.setOnSelectionChanged([&](int) { ++callbackCount; });
    a.setChecked(true);
    CHECK(callbackCount == 1);
    // Jump from a → c. Without _suppressHook, c.setChecked(true) would
    // re-enter onMemberToggled for c, which would call
    // a.setChecked(false) (causing a's _onToggled to fire and
    // re-enter...), potentially looping or firing multiple host
    // callbacks. With _suppressHook, the internal deselect of `prev`
    // is a no-op from the host's perspective.
    g.setSelectedIndex(2);
    // Exactly one additional callback for the new selection index.
    CHECK(callbackCount == 2);
    // And no radio other than `c` is checked.
    CHECK(c.isChecked());
    CHECK_FALSE(a.isChecked());
    CHECK_FALSE(b.isChecked());
}

// 11. remove() on the currently-selected member clears _selected
//     (no dangling pointer).
TEST_CASE(radiogroup_remove_selected_clears_pointer) {
    RadioGroup g;
    RadioButton a, b;
    g.add(&a);
    g.add(&b);
    a.setChecked(true);
    CHECK(g.getSelected() == &a);
    g.remove(&a);
    CHECK(g.getSelected() == nullptr);
    CHECK(g.getSelectedIndex() == -1);
    CHECK(g.size() == 1u);
}

TEST_SUITE_END