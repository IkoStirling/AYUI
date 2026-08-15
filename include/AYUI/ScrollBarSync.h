#pragma once

// =============================================================================
// PR-SyncVerticalBar: header-only inline helper that collapses the
// `setRange + setViewportSize + setValue` triple that 4 container
// owners (ScrollView, ListView, TreeView, Window) all wrote by hand.
// Direction-agnostic: ScrollBar::setRange/setViewportSize/setValue do
// not consult orientation, so the same helper serves vbar AND hbar
// (ScrollView is the only H consumer and just passes .x directly).
//
// Order is locked (range → viewport → value) because ScrollBar::setRange
// internally calls setValue; reversing would re-enter the onValueChanged
// callback twice with stale data.
// =============================================================================

#include "AYUI/ScrollBar.h"

namespace ayt::ui {

inline void syncVerticalBar(ScrollBar* bar,
                            float contentExtent,
                            float viewportSize,
                            float scrollOffset) {
    if (bar == nullptr) return;
    bar->setRange(0.0f, contentExtent);
    bar->setViewportSize(viewportSize);
    bar->setValue(scrollOffset);
}

} // namespace ayt::ui