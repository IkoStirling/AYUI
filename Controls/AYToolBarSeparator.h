#pragma once

#include "AYSeparator.h"
#include <functional>

namespace ayt::ui {

// =============================================================================
// G7 — ToolBarSeparator: a Separator pre-styled for toolbars.
// =============================================================================
// A typed Separator subclass so hosts can:
//   1. Distinguish toolbar separators from menu/status-bar separators
//      (factory-registered under "ToolBarSeparator"; serializer
//      round-trips independently of the generic Separator type).
//   2. Apply a toolbar-specific palette (slightly darker than the
//      base Separator gray so the line reads against a typical
//      toolbar background).
//   3. Get a sensible default size (vertical, 1px wide,
//      kDefaultHeight - 2*kPadding tall) without manual setSize
//      bookkeeping at every addSeparator() call site.
//
// ToolBar::addSeparator() instantiates this class instead of the
// bare Separator so the toolbar gets the dedicated palette and the
// host code that walks children by type can find ToolBarSeparator
// specifically.
//
// Not a Widget the factory needs a creator for if ToolBar owns the
// lifecycle (it does — see ToolBar::addSeparator), but we register
// it anyway so JSON-loaded layouts can instantiate it by name.
// =============================================================================

class ToolBarSeparator : public Separator {
public:
    ToolBarSeparator();
    ~ToolBarSeparator() override;

    // Default size matches the typical toolbar inset.
    static constexpr float kDefaultBarWidth = 1.0f;
    static constexpr float kDefaultBarHeight = 24.0f;
    static constexpr float kPadding = 4.0f;

    void onRender(IRenderBackend& renderer) override;
};

Widget* createToolBarSeparatorWidget();

} // namespace ayt::ui