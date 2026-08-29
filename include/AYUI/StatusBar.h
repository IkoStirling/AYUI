#pragma once

// Bottom information strip with polymorphic panels laid out left-to-right.
// The text overload creates a TextLabel; getPanelWidget() exposes arbitrary
// content. Height grows to the tallest panel plus vertical padding.
//
// StatusBar owns its panel widgets. Caller passes either:
//     - addPanel(Widget* w) → StatusBar takes ownership, will delete
//       in ~StatusBar via clearPanels.
//     - addPanel(const std::wstring&) → StatusBar creates + owns the
//       TextLabel internally.
// External widgets become StatusBar's responsibility after handoff.

#include "AYUI/Widget.h"
#include "AYUI/TextLabel.h"
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

    // ---- polymorphic panel API ----

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
