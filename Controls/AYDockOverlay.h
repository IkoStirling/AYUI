#pragma once

#include "AYCompoundFocusableWidget.h"
#include <vector>

namespace ayt::ui {

class DockCard;

// D1: DockOverlay - the floating-cards layer that sits on top of a
// DockArea. Hit-test descends into floating cards first; when nothing is
// hit, the hit "passes through" (returns nullptr) so the underlying
// DockArea / freecam viewport logic (see AYEditorSession.cpp:299) keeps
// working unchanged.
//
// D1 ships the container + add/remove + hit-test pass-through. D3 wires
// the actual tear-off drag UX.
class DockOverlay : public CompoundFocusableWidget {
public:
    DockOverlay();
    ~DockOverlay() override;

    // Adds a floating card. Overlay takes ownership; cards are destroyed
    // when the overlay is destroyed or the card is removed.
    void addFloatingCard(DockCard* card);
    void removeFloatingCard(DockCard* card);

    // Number of floating cards currently in the overlay.
    size_t getFloatingCardCount() const { return _floatingCards.size(); }

    // Index-based accessor for tests / editor save. Order = insertion order.
    DockCard* getFloatingCard(size_t index) const;

    void onRender(IRenderBackend& renderer) override;

private:
    // Non-owning index of the floating cards stored in this overlay's
    // child tree. CompoundFocusableWidget owns them via ~Widget's child
    // destruction path; we only keep the index so add / remove / count
    // are O(1) and order is well-defined.
    std::vector<DockCard*> _floatingCards;
};

} // namespace ayt::ui