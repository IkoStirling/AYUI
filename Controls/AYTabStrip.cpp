#include "AYUI/TabStrip.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/UIKeyCode.h"
#include <algorithm>

namespace ayt::ui {

TabStrip::TabStrip() {
    setSize(math::FVector2(400.0f, kDefaultTabHeight));
}

TabStrip::~TabStrip() {
    // Buttons were added via addChildExternal (host-lifetime semantics
    // so a parent TabControl can hold us without re-owning them). But
    // when *we* are destroyed, destroyWidgetTree SKIPS externally-owned
    // children — which would leak every tab button. destroyAllButtons()
    // already does the right detach+delete + clear; call it as the first
    // action here so a `delete tabStrip` (or a host-driven stack unwind)
    // releases them. removeTab/clearTabs call destroyAllButtons() too,
    // making this idempotent.
    destroyAllButtons();
}

void TabStrip::addTab(const std::wstring& label) {
    const int oldCount = static_cast<int>(_labels.size());
    _labels.push_back(label);
    ensureButtonsCreated();
    // First tab auto-selects (matches original TabControl behavior).
    if (_selectedIndex < 0) {
        setSelectedIndex(0);
    } else {
        // Keep selection valid; ensureButtonsCreated recreated buttons if
        // needed, so the active button still gets the underline next render.
        (void)oldCount;
    }
}

void TabStrip::removeTab(int index) {
    if (index < 0 || index >= static_cast<int>(_labels.size())) return;
    _labels.erase(_labels.begin() + index);
    // ensureButtonsCreated will recreate, but the index of the active
    // selection may need repair — slide-down for an index BEFORE the
    // active one, or pick a neighbor for the active index.
    int newSel = _selectedIndex;
    if (_labels.empty()) {
        newSel = -1;
    } else if (index < _selectedIndex) {
        --newSel;
    } else if (index == _selectedIndex) {
        const int n = static_cast<int>(_labels.size());
        newSel = (index < n) ? index : (n - 1);
    }
    ensureButtonsCreated();
    setSelectedIndex(newSel);
}

void TabStrip::clearTabs() {
    destroyAllButtons();
    _labels.clear();
    _selectedIndex = -1;
}

const std::wstring& TabStrip::getTabLabel(int index) const {
    static const std::wstring kEmpty;
    if (index < 0 || static_cast<size_t>(index) >= _labels.size()) return kEmpty;
    return _labels[index];
}

void TabStrip::setSelectedIndex(int index) {
    if (_labels.empty()) {
        _selectedIndex = -1;
        // Clearing selection changes the cached visual state.
        markDirty();
        return;
    }
    int clamped = std::clamp(index, 0, static_cast<int>(_labels.size()) - 1);
    if (clamped == _selectedIndex) return;
    _selectedIndex = clamped;
    if (_onSelectionChanged) _onSelectionChanged(clamped);
    // Selection changes invalidate any retained presentation even when the
    // indicator tween is disabled.
    markDirty();
}

bool TabStrip::isOverflown() const {
    float totalW = 0.0f;
    for (Button* b : _tabButtons) {
        if (b == nullptr) continue;
        totalW += b->getPreferredSize().x;
    }
    if (_tabButtons.size() > 1) {
        totalW += _spacing * static_cast<float>(_tabButtons.size() - 1);
    }
    return totalW > getWidth();
}

void TabStrip::performLayout() {
    CompoundFocusableWidget::performLayout();
    layoutChildren();
}

void TabStrip::layoutChildren() {
    const float w = getWidth();
    const float h = getHeight();
    if (_tabButtons.empty()) return;

    // Q12 — v1 clips overflow. Lay out left-to-right starting at x=0 with
    // the configured spacing; tabs that exceed the strip width draw past
    // the right edge without scrolling. v1.1 wraps with ScrollView.
    constexpr float kMinTabWidth = 80.0f;
    constexpr float kPaddingX = 12.0f;

    float x = 0.0f;
    for (Button* btn : _tabButtons) {
        if (btn == nullptr) continue;
        const float tabW = std::max(kMinTabWidth, btn->getPreferredSize().x + kPaddingX * 2.0f);
        btn->setSize(math::FVector2(tabW, h));
        btn->setPosition(math::FVector2(x, 0.0f));
        x += tabW + _spacing;
    }
}

// UI-anim cut 2: target underline geometry = (x, width) of the active
// tab's button, inset 2px on each side. Height is constant (button bottom
// edge), so only x + width need to tween.
math::FVector2 TabStrip::indicatorTargetRect() const {
    if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(_tabButtons.size())) {
        return _indicatorRect;
    }
    Button* active = _tabButtons[_selectedIndex];
    if (active == nullptr) return _indicatorRect;
    constexpr float kInsetX = 2.0f;
    const math::FVector2 pos = active->getPosition();
    const math::FVector2 sz  = active->getSize();
    return math::FVector2(pos.x + kInsetX, sz.x - 2.0f * kInsetX);
}

// Render-driven retarget — mirror of InteractiveWidget::resolveTransitionColor:
//   - never rendered / tween disabled → snap (first frame can't flash a
//     zero-width underline, and tween-off is byte-identical to old render)
//   - running toward the same target → return the interpolated rect
//   - running and the target changed (selection moved mid-tween / layout
//     re-ran) → retarget from the current rect
//   - idle and the target moved → start the slide from here
math::FVector2 TabStrip::resolveIndicatorRect(const math::FVector2& target) {
    if (!_indicatorInitialized || _indicatorTweenMs <= 0.0f) {
        _indicatorInitialized = true;
        _indicatorAnim.snap(target);
        _indicatorRect = target;
        return target;
    }
    if (_indicatorAnim.active) {
        if (target == _indicatorAnim.to) {
            return _indicatorRect;
        }
        _indicatorAnim.start(_indicatorRect, target, _indicatorTweenMs,
                             AnimationCurve::EaseOut);
        return _indicatorRect;
    }
    if (target != _indicatorRect) {
        _indicatorAnim.start(_indicatorRect, target, _indicatorTweenMs,
                             AnimationCurve::EaseOut);
    }
    return _indicatorRect;
}

void TabStrip::tick(float dt) {
    // Chain the base cascade first (opacity/position tweens of this widget
    // + virtual tick of the tab buttons). Then advance our indicator.
    CompoundFocusableWidget::tick(dt);
    const math::FVector2 prevRect = _indicatorRect;
    const bool wasActive = _indicatorAnim.active;
    float t;
    if (_indicatorAnim.advance(dt, t)) {
        _indicatorRect = tweenLerp(_indicatorAnim.from, _indicatorAnim.to, t);
    } else if (wasActive) {
        _indicatorRect = _indicatorAnim.to;
    }
    // The underline moves without going through a setter. Invalidate a
    // future cached presentation only when it actually moved.
    if (_indicatorRect.x != prevRect.x || _indicatorRect.y != prevRect.y) {
        markDirty();
    }
}

void TabStrip::onRender(IRenderBackend& renderer) {
    // Selection visual: accent underline beneath the active tab. Drawn
    // AFTER the inherited render cascade so the buttons already painted
    // themselves; we lay a thin (2px) sky-blue strip across the bottom
    // edge of the active button.
    CompoundFocusableWidget::onRender(renderer);
    if (_selectedIndex < 0) return;
    if (_selectedIndex >= static_cast<int>(_tabButtons.size())) return;
    Button* active = _tabButtons[_selectedIndex];
    if (active == nullptr) return;
    const math::FVector2 r = resolveIndicatorRect(indicatorTargetRect());
    const float thickness = 2.0f;
    const float bottom = active->getPosition().y + active->getSize().y;
    renderer.drawRect(
        math::FRectangle(r.x,
                         bottom - thickness,
                         r.x + r.y,
                         bottom),
        math::FVector4(0.18f, 0.45f, 0.78f, 1.0f));
}

bool TabStrip::onKeyDown(int keyCode) {
    if (_labels.empty()) return false;
    const int n = static_cast<int>(_labels.size());
    int next = _selectedIndex;
    switch (keyCode) {
    case UIKey_Right:
        next = (_selectedIndex < 0) ? 0 : (_selectedIndex + 1) % n;
        break;
    case UIKey_Left:
        next = (_selectedIndex < 0) ? n - 1
                                    : (_selectedIndex <= 0 ? n - 1
                                                            : _selectedIndex - 1);
        break;
    default:
        return false;
    }
    setSelectedIndex(next);
    return true;
}

void TabStrip::ensureButtonsCreated() {
    // Sync button count with label count. We rebuild from scratch — v1
    // doesn't try to preserve per-tab state, and rebuild is cheap (each
    // button is a thin InteractiveWidget + text).
    destroyAllButtons();
    _tabButtons.reserve(_labels.size());
    for (size_t i = 0; i < _labels.size(); ++i) {
        Button* b = new Button();
        b->setText(_labels[i]);
        // Capture-by-value of `this` and the index — selectedIndex may
        // shift between construction time and click time (tabs added /
        // removed in between), so we resolve via the closure's `i` only
        // as a hint and clamp inside.
        const size_t capturedIndex = i;
        b->setOnClicked([this, capturedIndex]() {
            // Defensive clamp in case the label vector shrank between
            // creation and click.
            if (capturedIndex >= _labels.size()) return;
            setSelectedIndex(static_cast<int>(capturedIndex));
        });
        addChildExternal(b);
        _tabButtons.push_back(b);
    }
}

void TabStrip::destroyAllButtons() {
    // R6 / R-7 pattern: iterate a snapshot, detach + free, then clear.
    // Buttons are CompoundWidget children with no further siblings;
    // removeChildExternal is enough because the host never reparents
    // them out of the strip.
    for (Button* b : _tabButtons) {
        if (b == nullptr) continue;
        if (b->getParent() == this) {
            removeChild(b);
        }
        delete b;
    }
    _tabButtons.clear();
}

Widget* createTabStripWidget() {
    return new TabStrip();
}

} // namespace ayt::ui
