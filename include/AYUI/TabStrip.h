#pragma once

#include "AYUI/CompoundFocusableWidget.h"
#include "AYUI/Button.h"

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// =============================================================================
// Phase D (D4) — TabStrip: horizontal tab header.
// =============================================================================
//
// Phase D PR-3 re-architects the old TabControl header (which was a vertical
// ListView, visually wrong — see original TabControl.h DECISION 1). TabStrip
// is a new widget that lays a row of `Button` (C-1) tabs out left-to-right;
// the active tab gets a sky-blue accent underline.
//
// Q9: New widget, TabControl becomes "strip + body Panel". Public API:
//   `getHeaderListView()` is replaced by `getTabStrip()`. The 1 existing
//   internal call site is rewritten. JSON loader/serializer is unchanged —
//   the legacy `tabs[]` array format round-trips into the new path because
//   TabControl itself didn't change its serializer payload in Phase C PR-3.
//
// Q10: Tabs are plain `Button : InteractiveWidget` (NOT a new TabButton
// subclass). Each tab's `_onClicked` calls `TabStrip::setSelectedIndex(idx)`.
// Selection visual: TabStrip::onRender draws the accent underline beneath
// the active tab. This bypasses the Button's own visual slots and lets
// hosts theme Buttons separately.
//
// Q11: No close × button on tabs (DECISION 5 of original, still deferred).
//
// Q12: v1 clips overflow — tabs longer than TabStrip width draw past the
// right edge without scrolling. v1.1 wraps the whole strip in a
// ScrollView (shared future work with D3 ToolBar overflow).
//
// Caller-owned content (mirror original DECISION 2): the strip only owns
// its labels and the Button children. Body content is owned by the host
// (typically a parent TabControl). Detach / destroy are the host's job.
// =============================================================================
class TabStrip : public CompoundFocusableWidget {
public:
    // Default row height in logical pixels — same convention as
    // ListView::kDefaultRowHeight (24.0f). TabStrip uses a slightly taller
    // 28.0f default to leave room for the accent underline without
    // shrinking the button label.
    static constexpr float kDefaultTabHeight = 28.0f;

    TabStrip();
    ~TabStrip() override;

    // Tab data management. The strip owns the Button it creates; the
    // host's "content" is NOT touched here — that's TabControl's job.
    void addTab(const std::wstring& label);
    void removeTab(int index);
    void clearTabs();

    int                       getTabCount() const { return static_cast<int>(_labels.size()); }
    const std::wstring&       getTabLabel(int index) const;
    size_t                    getLabelCount() const { return _labels.size(); }

    // Selection. -1 = no tab. setSelectedIndex is idempotent + clamps
    // out-of-range to -1 (matches original TabControl behavior).
    int  getSelectedIndex() const { return _selectedIndex; }
    void setSelectedIndex(int index);
    const std::wstring& getSelectedLabel() const { return getTabLabel(_selectedIndex); }

    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }

    // Tab strip styling.
    void setTabHeight(float h) {
        if (_tabHeight == h) return;
        _tabHeight = h;
        markBoundsDirty();
        markDirty();
    }
    float getTabHeight() const { return _tabHeight; }
    void setSpacing(float s) {
        if (_spacing == s) return;
        _spacing = s;
        markBoundsDirty();
        markDirty();
    }
    float getSpacing() const { return _spacing; }

    // Q12 — total preferred width vs current strip width. Hosts that
    // want to detect overflow before v1.1 ScrollView wrap can poll this.
    bool isOverflown() const;

    // Layout & render.
    void performLayout() override;

    // UI-anim cut 2: selection underline slides instead of hard-swapping.
    // tween ms default 120; setIndicatorTweenMs(0) restores the instant
    // swap (byte-identical to the pre-animation render path).
    void setIndicatorTweenMs(float ms) {
        _indicatorTweenMs = ms;
        if (ms <= 0.0f) _indicatorAnim.active = false;
    }
    float getIndicatorTweenMs() const { return _indicatorTweenMs; }
    bool isIndicatorAnimating() const { return _indicatorAnim.active; }

    void tick(float dt) override;

    // Phase B keyboard nav mirror (B4 had the same on TabControl).
    // Left/Right cycle _selectedIndex. The host sets focus into the
    // strip for keyboard cycling (a Tab traversal host typically lands
    // focus inside the body content, not the strip, so this is rare).
    bool onKeyDown(int keyCode) override;

protected:
    void layoutChildren() override;
    void onRender(IRenderBackend& renderer) override;

private:
    // Recreate the per-tab Button children from _labels. Called when the
    // label vector changes (add/remove/clear).
    void ensureButtonsCreated();

    // Remove all buttons (used during clearTabs + when rebuilding after
    // label-vector changes). R6 landmine-pattern: clear stale children
    // before destroying.
    void destroyAllButtons();

    std::vector<std::wstring> _labels;
    std::vector<Button*>      _tabButtons;   // owned (children of `this`)

    int    _selectedIndex = -1;
    float  _tabHeight = kDefaultTabHeight;
    float  _spacing = 0.0f;   // default 0 → tabs touch for the
                              //            "tabs-with-underline" look

    std::function<void(int)> _onSelectionChanged;

    // UI-anim cut 2: indicator slide. _indicatorRect packs (x, width) of
    // the underline; tweened via the same render-driven retarget pattern
    // as InteractiveWidget::resolveTransitionColor.
    AnimState<math::FVector2> _indicatorAnim;
    math::FVector2 _indicatorRect{0.0f, 0.0f};
    float _indicatorTweenMs = 120.0f;
    bool  _indicatorInitialized = false;

    math::FVector2 indicatorTargetRect() const;
    math::FVector2 resolveIndicatorRect(const math::FVector2& target);
};

Widget* createTabStripWidget();

} // namespace ayt::ui
