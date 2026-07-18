#pragma once

// =============================================================================
// C-11 StatusBar: bottom-of-window information strip with text panels.
// =============================================================================
//
// Architecture (v1):
//   StatusBar (CompoundWidget)
//     └─ _panels: TextLabel* x N (left-to-right)
//
// Each panel has its own text + optional width. Panels stretch left-to-
// right inside the bar; the last panel is positioned with right-alignment
// padding for the optional size-grip area.
//
// -----------------------------------------------------------------------------
// v1 design decisions + known limitations + v1.1 upgrade paths
// -----------------------------------------------------------------------------
//
// DECISION 1: panels are TextLabels in v1 — simplified to keep the public
//   API small. To add an icon or progress indicator, replace the panel
//   with a custom Widget.
//   v1.1: status panels become polymorphic (TextPanel | IconPanel |
//   ProgressPanel) behind a StatusPanel interface.
//
// DECISION 2: a single fixed-height bar in v1. Multi-row status bars are
//   v1.1 — most apps don't need them and they complicate grid sizing.

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

    StatusBar();
    ~StatusBar() override;

    // Append a panel that shows the given text. Returns the TextLabel
    // for further customization.
    class TextLabel* addPanel(const std::wstring& text);
    size_t getPanelCount() const { return _panels.size(); }
    class TextLabel* getPanel(size_t index) const;
    void clearPanels();

    // Update a panel's text.
    void setPanelText(size_t index, const std::wstring& text);

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

private:
    std::vector<class TextLabel*> _panels;
};

Widget* createStatusBarWidget();

} // namespace ayt::ui
