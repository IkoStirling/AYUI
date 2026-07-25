#include "AYDockOverlay.h"
#include "AYDockCard.h"
#include "AYWidget.h"

namespace ayt::ui {

DockOverlay::DockOverlay() {
    // Overlay covers the whole DockArea; host is expected to size it
    // to match. We default to a sensible editor resolution.
    setSize(math::FVector2(1280.0f, 720.0f));
    // Floating cards should be drawn above the slots; raise our draw
    // order via the Widget z-order hint. CompoundFocusableWidget exposes
    // no direct setter; children added later already draw on top of
    // earlier siblings so insertion order is enough.
}

DockOverlay::~DockOverlay() {
    // Children are torn down by ~CompoundFocusableWidget. Cards stored
    // in _floatingCards are non-owning indices; we clear the vector
    // before the destructor to avoid a stale-pointer view.
    _floatingCards.clear();
}

void DockOverlay::addFloatingCard(DockCard* card) {
    if (!card) {
        return;
    }
    _floatingCards.push_back(card);
    // addChild transfers ownership; when DockOverlay is destroyed the
    // card is destroyed too. This mirrors how VBox stores its slots.
    addChild(card);
}

void DockOverlay::removeFloatingCard(DockCard* card) {
    if (!card) {
        return;
    }
    for (auto it = _floatingCards.begin(); it != _floatingCards.end(); ++it) {
        if (*it == card) {
            _floatingCards.erase(it);
            break;
        }
    }
    // removeChild + destroy mirrors CompoundWidget ownership rules.
    // The caller may want to keep the card alive (e.g. to re-dock it),
    // so we detach without delete and let the caller decide.
    if (card->getParent() == this) {
        removeChild(card);
    }
}

DockCard* DockOverlay::getFloatingCard(size_t index) const {
    if (index >= _floatingCards.size()) {
        return nullptr;
    }
    return _floatingCards[index];
}

void DockOverlay::onRender(IRenderBackend& renderer) {
    // No background - overlay is transparent so the underlying DockArea
    // (with its slot backgrounds) is visible. Floating cards draw their
    // own bodies; their headers + borders are sufficient visual cue.
    AYUNREFERENCED_PARAM(renderer);
}

} // namespace ayt::ui