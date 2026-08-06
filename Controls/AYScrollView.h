#pragma once

#include "AYWidget.h"
#include "AYScrollableWidget.h"
#include "AYScrollBar.h"
#include <functional>

namespace ayt::ui {

// C-4 ScrollView: a CompoundWidget that clips its content to its
// viewport and exposes H/V ScrollBars. Content is a single Widget
// whose bounds ScrollView offsets each frame by -_scrollOffset.
//
// Architecture:
//   ScrollView (CompoundWidget)
//     ├─ content: Widget*  (added via setContent, drawn first;
//     │                      click-through / hit-test delegated to
//     │                      host's content widget; for v1 we delegate
//     │                      to content first)
//     ├─ vScrollBar: ScrollBar* (auto-managed; optional)
//     └─ hScrollBar: ScrollBar* (auto-managed; optional)
//
// Mutex between content and bars:
//   - User drags a ScrollBar → ScrollBar fires setOnValueChanged →
//     ScrollView calls scrollBy() which clamps and updates
//     _scrollOffset. The next render offsets content by -offset.
//   - User scrolls via mouse-wheel or programmatic API →
//     ScrollView::scrollBy → updates _scrollOffset → re-syncs
//     ScrollBars via setScroll() → bar thumbs move.
//
// Tick cascade: scroll blink is N/A but tick cascade for caret blink
// of any TextInput-as-content is preserved (content is a Widget, we
// recurse into it via CompoundWidget::tick).
//
// For v1: content is treated as a single child. There is no
// virtualization.

class ScrollView : public CompoundWidget {
public:
    ScrollView();
    ~ScrollView() override;

    // Content widget — ScrollView becomes its layout owner. Content
    // position is reset on content-size change; content's own bounds
    // are not modified by ScrollView's performLayout (it just decides
    // where the content gets drawn).
    void setContent(Widget* content);
    Widget* getContent() const { return _content; }

    // Set the virtual content size explicitly. Required when the
    // content widget's own size does not represent its scrollable
    // extent (e.g. an inner TextLabel that's logically taller than
    // its widget bounds).
    void setContentSize(const math::FVector2& size);

    void setVerticalScrollBarEnabled(bool enabled) { _vbarEnabled = enabled; }
    void setHorizontalScrollBarEnabled(bool enabled) { _hbarEnabled = enabled; }

    // Apply a delta to _scrollOffset. Clamped. Returns true on real
    // change. Re-syncs bar values to the new offset.
    bool scrollBy(const math::FVector2& delta);

    // PR-B3 — explicit scroll offset setter. Mirrors ListView's
    // setScrollOffset for tests + hosts that want to seed a known
    // scroll position before running a wheel assertion. Clamped via
    // the same chokepoint scrollBy uses.
    void setScrollOffset(const math::FVector2& offset) {
        scrollBy(offset - _scrollState.getScrollOffset());
    }

    void setOnScroll(std::function<void(const math::FVector2&)> cb) {
        _onScroll = std::move(cb);
    }

    ScrollBar* getVerticalScrollBar()   const { return _vbar; }
    ScrollBar* getHorizontalScrollBar() const { return _hbar; }

    // PR-B3 — exposes the current scroll offset for tests + host code
    // that wants to mirror the bar's value. Mirrors ListView's
    // getScrollOffset() so wheel-routing assertions can read either
    // container's state the same way.
    const math::FVector2& getScrollOffset() const {
        return _scrollState.getScrollOffset();
    }

    // PR-B3 — wheel routing. Maps a vertical wheel deltaY into a
    // scrollBy(0, -deltaY) call (sign flipped: wheel deltaY positive
    // means "scroll content UP", which is a DECREASE in scrollOffset.y
    // because content moves with the cursor). Returns true if the
    // scroll actually moved (clamped + changed) so the UIManager
    // router stops bubbling the event to outer scroll containers.
    bool onMouseWheel(const UIMouseWheelEvent& e) override;

protected:
    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

private:
    void ensureBarsCreated();
    void syncBarsToOffset();
    math::FVector2 getViewportSize() const;

    Widget*  _content = nullptr;
    ScrollBar* _vbar = nullptr;
    ScrollBar* _hbar = nullptr;
    bool _vbarEnabled = true;
    bool _hbarEnabled = false;
    std::function<void(const math::FVector2&)> _onScroll;

    ScrollableWidget _scrollState;
};

Widget* createScrollViewWidget();

} // namespace ayt::ui
