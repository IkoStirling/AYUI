#pragma once

#include "AYWidget.h"

namespace ayt::ui {

// C-5 SelectableWidget: helper-mixin contract for any widget that owns a
// single selection model (ListView row, TabItem, TreeNode — C-11/C-12 will
// also reuse it).
//
// Mirrors the AYScrollableWidget / AYValueWidget pattern: the contract is
// documented here as comments + a free helper so the 2nd consumer
// (ComboBox popup = ListView) doesn't need to re-derive the API. v1
// deliberately does NOT promote to a polymorphic base — the consumer is
// ListView (C-5) plus ComboBox's popup (C-6) which is itself a ListView,
// so they all share the same code path. Promote when a 3rd consumer (e.g.
// MenuItem shortcut routing) needs the contract independently.
//
// Selection model contract:
//   - Single mode: _selectedIndex = -1 means "no selection". setSelectedIndex
//     clamps to [-1, count) and silently ignores out-of-range inputs (same
//     as setValue on Slider).
//   - Multi mode: out of scope for v1; declared but not implemented.
//   - Selection transitions fire _onSelectionChanged with the NEW index.
//   - Idempotent setSelectedIndex (same value) does NOT fire the callback.
//   - Adding/removing items adjusts the selected index so it still points
//     at the same item by content (i.e. setItems(["a","b","c"], sel=1) then
//     remove "a" → sel becomes 0, not "index of 'b' after the shift" which
//     is what users expect on native list boxes).
//
// Free helper kept here so callers can derive row rects without re-deriving
// the layout math.

} // namespace ayt::ui
