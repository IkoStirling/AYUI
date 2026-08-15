#pragma once

#include "AYUI/Widget.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class DockCard;

// DockTabGroup — the leaf node of the dock tree. Hosts 1..N DockCards
// as tabs.
//
//   * Single card: the card is laid out full-bleed (its own title bar
//     is the only chrome — legacy 5-slot geometry, byte-identical;
//     test_center_card_fills_slot_height pins the exact bounds).
//   * ≥2 cards: a custom-painted tab strip (kTabStripHeight) appears at
//     the top. The active card is slid up by (stripH - cardHeaderHeight)
//     so the strip exactly covers the card's own title bar and the card
//     body starts below the strip. Inactive cards are setVisible(false).
//   * The strip's close x routes through setOnCloseTab (DockArea injects
//     DockArea::closeCard); pressing an inactive tab activates it;
//     pressing the ACTIVE tab starts a G12 tear-off drag of that card
//     (same payload convention as DockCard's own title bar).
//
// Cards are NOT freed here — removeTab detaches only (UI-OWN-1; the
// DockArea / children tree owns teardown).
//
// NOT registered in the WidgetFactory; created exclusively by DockArea.
// The ctor lives in AYDockTabGroup.cpp (same TU trap as DockArea).
class DockTabGroup : public CompoundWidget {
public:
    static constexpr float kTabStripHeight = 26.0f;

    DockTabGroup();
    ~DockTabGroup() override;

    // ---- tab management (detach-only; never frees the card) ----
    void addTab(DockCard* card);
    bool removeTab(DockCard* card);
    bool containsCard(const DockCard* card) const;
    size_t getTabCount() const { return _tabs.size(); }
    DockCard* getTab(size_t index) const;
    void activateTab(size_t index);
    void activateTabById(const std::string& id);
    DockCard* getActiveTab() const;
    std::string getActiveTabId() const;

    // ---- leaf identity ----
    void setLeafId(const std::string& id) { _leafId = id; }
    const std::string& getLeafId() const { return _leafId; }
    // Pinned leaves (the 5 legacy slots, id = slot name) are never
    // pruned even when empty. Split-created leaves (g_N) prune when
    // empty.
    void setPinned(bool pinned) { _pinned = pinned; }
    bool isPinned() const { return _pinned; }

    using CloseCallback = std::function<void(DockCard* card)>;
    void setOnCloseTab(CloseCallback cb) { _onCloseTab = std::move(cb); }

    void performLayout() override;
    // Cards render first; the tab strip paints AFTER so it covers the
    // active card's own title bar (slid up beneath it).
    void render(IRenderBackend& renderer) override;
    // Strip region claims hits before the active card's title bar.
    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    void onMouseLeave() override;
    UiCursorHint getCursorHint() const override;

private:
    math::FRectangle stripRectWorld() const;
    math::FRectangle tabRectWorld(size_t index) const;
    math::FRectangle closeRectWorld(size_t index) const;
    int tabIndexAt(const math::FVector2& worldPos) const;   // -1 = none
    void paintStrip(IRenderBackend& renderer);

    std::string _leafId;
    bool _pinned = false;
    std::vector<DockCard*> _tabs;
    size_t _activeIndex = 0;
    int _hoveredTab = -1;     // strip tab under the cursor, or -1
    int _hoveredClose = -1;   // close x under the cursor, or -1
    CloseCallback _onCloseTab;
};

} // namespace ayt::ui
