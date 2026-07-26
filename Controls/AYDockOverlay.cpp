#include "AYDockOverlay.h"
#include "AYDockCard.h"
#include "AYWidget.h"
#include "IAYRenderBackend.h"

namespace ayt::ui {

DockOverlay::DockOverlay() {
    // Overlay covers the whole DockArea; host is expected to size it
    // to match. We default to a sensible editor resolution.
    setSize(math::FVector2(1280.0f, 720.0f));
    // Floating cards should be drawn above the slots; raise our draw
    // order via the Widget z-order hint. CompoundFocusableWidget exposes
    // no direct setter; children added later already draw on top of
    // earlier siblings so insertion order is enough.

    // D3 — drop-target wiring for the overlay. Dropping a card
    // ON a floating card is a no-op (the card stays where it is);
    // the design choice was made in plan §D3 ("drop on floating
    // card = no-op", can be polished to stack-on-top later).
    // Drops on the overlay's empty area are routed by G12 to the
    // parent DockArea instead (overlay's hitTest pass-through means
    // the parent-chain walk stops at DockArea, which IS the drop
    // target for the empty-area case).
    setAcceptDrops(true);
    setOnDrop([](const DragPayload& /*payload*/) {
        // Intentionally empty — see ctor comment above.
    });
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

Widget* DockOverlay::hitTest(const math::FVector2& worldPos) {
    // K-INV-D3-4 pass-through override. See AYDockOverlay.h for the
    // rationale (F3 freecam isPointOnChrome compatibility + drop
    // semantics for the empty overlay area).
    if (!_visible) return nullptr;
    const math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) return nullptr;
    // Descend into floating cards in reverse insertion order (topmost
    // first). The default compoundDescendHitTest walks the children
    // list the same way; we re-implement inline so we can return
    // nullptr instead of `this` when no child hits.
    const std::vector<Widget*>& kids = getChildren();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        Widget* w = *it;
        if (w && w->isVisible() && w->hitTest(worldPos)) {
            return w;
        }
    }
    // Pass-through — caller (UIManager::pickTopmostWidget's parent-
    // chain walk) will see nullptr and continue up to DockArea itself,
    // which has isAcceptDrops()==true. Net effect: drop on overlay
    // empty area routes to DockArea::onDrop, not DockOverlay::onDrop.
    return nullptr;
}

} // namespace ayt::ui