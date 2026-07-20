#pragma once

// =============================================================================
// C-11 StatusBar: bottom-of-window information strip with panels.
// =============================================================================
//
// Architecture (v1.1):
//   StatusBar (CompoundWidget)
//     └─ _panels: Widget* x N (left-to-right)
//
// v1 stored TextLabel* directly. v1.1 stores Widget* so any widget
// (TextLabel, ProgressBar, custom StatusIcon, etc.) can be a panel.
// Legacy text-panel API is preserved via addPanel(wstring) overload
// + getPanelText(size_t) + getPanel(size_t) returning TextLabel* (the
// legacy convenience accessor; for non-text panels use getPanelWidget).
//
// Multi-line support (G8):
//   Each panel may declare its preferred height via setSize(). StatusBar
//   uses MAX(panel heights) + 2*padding as its own height. This means
//   hosts wanting a 2-row status bar just call `panel->setSize(w, 38)`
//   and StatusBar grows to fit. No grid needed.
//
// -----------------------------------------------------------------------------
// v1 design decisions + v1.1 upgrade paths
// -----------------------------------------------------------------------------
//
// DECISION 1: panels are polymorphic in v1.1 (any Widget). Use
//   addPanel(Widget*) for custom content; the wstring overload wraps
//   in a TextLabel for the common case.
//
// DECISION 2 (v1): single fixed-height bar. v1.1 implements multi-line
//   by sizing the bar to its tallest panel's height. Most apps still
//   use single-row; this is a no-op for them (default 22px).
//
// DECISION 3: StatusBar OWNS its panel widgets (per owner decision,
//   matches ComboBox/TreeNode precedent). Caller passes either:
//     - addPanel(Widget* w) → StatusBar takes ownership, will delete
//       in ~StatusBar via clearPanels.
//     - addPanel(const std::wstring&) → StatusBar creates + owns the
//       TextLabel internally.
//   External widgets (e.g. a ProgressBar the host built) can be added
//   but then become StatusBar's responsibility; host should not delete
//   them after handing over.

#include "AYWidget.h"
#include "AYTextLabel.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class StatusBar : public CompoundWidget {
public:
    static constexpr float kDefaultWidth  = 800.0f;
    static constexpr float kDefaultHeight = 22.0f;
    static constexpr float kPadding       = 4.0f;
    static constexpr float kPanelSpacing  = 8.0f;
    // G8 — single-row height when no panel sets a custom size.
    static constexpr float kRowHeight     = kDefaultHeight - 2.0f * kPadding;

    StatusBar();
    ~StatusBar() override;

    // ---- v1.1 polymorphic panel API ----

    // Append any widget as a panel. StatusBar takes ownership and will
    // delete it in ~StatusBar (via clearPanels). Caller must NOT delete
    // `w` after passing it in. Returns the same pointer for chaining.
    class Widget* addPanel(class Widget* w);

    // Convenience overload: wrap text in a new TextLabel and own it.
    // Returns the TextLabel for legacy callers.
    class TextLabel* addPanel(const std::wstring& text);

    size_t getPanelCount() const { return _panels.size(); }

    // Get the underlying widget for any panel index. Works for both
    // text panels (returns the TextLabel) and custom panels.
    class Widget* getPanelWidget(size_t index) const;

    // Legacy text-only accessor. Returns the panel as TextLabel* if
    // it IS one, else nullptr. Used by v1 tests + serializer.
    class TextLabel* getPanel(size_t index) const;

    // Update text of a text panel (no-op if index is not a TextLabel).
    void setPanelText(size_t index, const std::wstring& text);

    // Remove all panels and DELETE them (StatusBar-owned lifetime).
    void clearPanels();

    // ---- layout ----

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

private:
    std::vector<class Widget*> _panels;

    // Compute desired height from max panel height.
    float computeDesiredHeight() const;
};

Widget* createStatusBarWidget();

} // namespace ayt::ui