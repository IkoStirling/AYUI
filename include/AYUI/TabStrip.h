#pragma once

#include "AYUI/CompoundFocusableWidget.h"
#include "AYUI/Button.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// Horizontal Button-based tab header with an animated active indicator.
// Product overflow policy: Scroll (default) clips and wheel-scrolls a
// logical horizontal viewport while keeping selection visible; Compress
// distributes available width down to minTabWidth; Clip preserves the
// legacy fixed-width behavior. Children never paint outside the strip.
//
// The strip owns labels and Button children only; TabControl or another host
// owns body content. Close buttons, drag reorder and a built-in overflow menu
// are not currently exposed.
class TabStrip : public CompoundFocusableWidget {
public:
    enum class OverflowMode { Scroll, Compress, Clip };
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
    bool setTabLabel(int index, const std::wstring& label);
    void removeTab(int index);
    void clearTabs();

    int                       getTabCount() const { return static_cast<int>(_labels.size()); }
    const std::wstring&       getTabLabel(int index) const;
    size_t                    getLabelCount() const { return _labels.size(); }

    // Selection. -1 only while empty. With tabs present setSelectedIndex is
    // idempotent and clamps to the nearest valid tab (TabControl behavior).
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

    // Total preferred width vs current strip width. Hosts can use this for
    // a supplementary overflow affordance even when Scroll handles input.
    bool isOverflown() const;
    void setOverflowMode(OverflowMode mode) {
        if (_overflowMode == mode) return;
        _overflowMode = mode;
        _scrollOffset = 0.0f;
        markBoundsDirty();
        markDirty();
    }
    OverflowMode getOverflowMode() const { return _overflowMode; }
    // Preferred floor for natural/scroll layout. Compress may go below it
    // to fit the viewport, but never below the 24-DIP interactive hard floor.
    void setMinTabWidth(float width) {
        _minTabWidth = std::max(24.0f, width);
        markBoundsDirty();
        markDirty();
    }
    float getMinTabWidth() const { return _minTabWidth; }
    void setScrollOffset(float offset);
    float getScrollOffset() const { return _scrollOffset; }
    float getMaxScrollOffset() const { return _maxScrollOffset; }
    void scrollBy(float delta) { setScrollOffset(_scrollOffset + delta); }
    void ensureSelectedVisible();

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

    // Keyboard navigation mirrors TabControl.
    // Left/Right cycle _selectedIndex. The host sets focus into the
    // strip for keyboard cycling (a Tab traversal host typically lands
    // focus inside the body content, not the strip, so this is rare).
    bool onKeyDown(int keyCode) override;
    bool onMouseWheel(const UIMouseWheelEvent& e) override;
    void renderChildren(IRenderBackend& renderer) override;

protected:
    void layoutChildren() override;
    void layoutChildrenWithSelectionPolicy(bool ensureSelection);
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
    OverflowMode _overflowMode = OverflowMode::Scroll;
    float _minTabWidth = 80.0f;
    float _scrollOffset = 0.0f;
    float _maxScrollOffset = 0.0f;

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
