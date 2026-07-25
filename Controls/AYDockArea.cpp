#include "AYDockArea.h"
#include "AYDockCard.h"
#include "AYDockOverlay.h"
#include "AYBox.h"
#include "IAYRenderBackend.h"

namespace ayt::ui {

namespace {

// Returns the slot's "weight" used by the layout pass when the host
// hasn't called setSlotWeight. Stored default values are in the header;
// this helper exists only to centralise the 0.0 = use-default rule.
float effectiveWeight(const DockArea& area, DockArea::Slot slot) {
    const float w = area.getSlotWeight(slot);
    if (w > 0.0f) {
        return w;
    }
    // Fall back to the header-defined defaults. Keep these in sync with
    // the DockArea header initialiser list.
    switch (slot) {
        case DockArea::Slot::Left:   return 0.20f;
        case DockArea::Slot::Right:  return 0.25f;
        case DockArea::Slot::Top:    return 0.15f;
        case DockArea::Slot::Bottom: return 0.20f;
        case DockArea::Slot::Center: return 0.55f;
        default: return 0.0f;
    }
}

} // namespace

DockArea::~DockArea() {
    // Clear bookkeeping aliases first — do not delete through them.
    // Actual heap teardown walks getChildren() below.
    _cardIndex.clear();
    for (int i = 0; i < (int)Slot::Count; ++i) {
        _slotCards[i].clear();
        _slotContainers[i] = nullptr;
    }
    _overlay = nullptr;

    // Widget::~Widget does NOT free children (UI-OWN-1). DockArea owns its
    // overlay / slot containers / cards via addChild, so we must free them
    // here. Snapshot before destroy — each destroyWidgetTree detaches.
    const std::vector<Widget*> kids = getChildren();
    for (Widget* child : kids) {
        if (child == nullptr) {
            continue;
        }
        if (child->isExternallyOwned()) {
            child->detachFromParent();
            continue;
        }
        destroyWidgetTree(child);
    }
}

bool DockArea::parseSlot(const std::string& name, Slot& outSlot) {
    if (name == "Left")   { outSlot = Slot::Left;   return true; }
    if (name == "Right")  { outSlot = Slot::Right;  return true; }
    if (name == "Top")    { outSlot = Slot::Top;    return true; }
    if (name == "Bottom") { outSlot = Slot::Bottom; return true; }
    if (name == "Center") { outSlot = Slot::Center; return true; }
    return false;
}

void DockArea::addCard(Slot slot, std::unique_ptr<DockCard> card) {
    if (!card) {
        return;
    }
    if ((int)slot < 0 || (int)slot >= (int)Slot::Count) {
        return;
    }

    // If a card with the same id already exists, remove it first so the
    // "last write wins" rule is consistent with the Loader.
    const std::string id = card->getId();
    if (!id.empty()) {
        removeCard(id);
    }

    // Push the card into the slot's container. All 5 slots — including
    // Center — route through their container so performLayout() can
    // position the container and let the existing layout pass handle
    // the inner card. (Center is a plain Widget, not a VBox; cards are
    // direct children of that Widget.)
    DockCard* raw = card.release();
    Widget* container = _slotContainers[(int)slot];
    if (slot == Slot::Center) {
        // Center hosts a single big panel (viewport). Detach from
        // DockArea's children (if it was attached earlier by mistake)
        // and re-parent under the Center container.
        if (container) {
            container->addChild(raw);
        } else {
            addChild(raw);
        }
    } else if (container) {
        if (auto* vbox = dynamic_cast<VBox*>(container)) {
            vbox->addWidget(raw);
        } else if (auto* hbox = dynamic_cast<HBox*>(container)) {
            hbox->addWidget(raw);
        }
    }
    _slotCards[(int)slot].push_back(raw);
    if (!id.empty()) {
        _cardIndex[id] = raw;
    }
}

bool DockArea::removeCard(const std::string& cardId) {
    auto it = _cardIndex.find(cardId);
    if (it == _cardIndex.end()) {
        return false;
    }
    DockCard* card = it->second;

    // D1 invariant: removeCard always frees the card. destroyWidgetTree
    // detaches from whatever parent (DockArea / VBox / HBox) then deletes.
    destroyWidgetTree(card);

    _cardIndex.erase(cardId);
    for (int i = 0; i < (int)Slot::Count; ++i) {
        auto& vec = _slotCards[i];
        for (size_t k = 0; k < vec.size(); ++k) {
            if (vec[k] == card) {
                vec.erase(vec.begin() + k);
                break;
            }
        }
    }
    return true;
}

DockCard* DockArea::findCard(const std::string& cardId) const {
    auto it = _cardIndex.find(cardId);
    if (it != _cardIndex.end()) {
        return it->second;
    }
    // Fallback: overlay cards (not indexed because the id-keyed map
    // already includes them via addCard path - we add overlay cards
    // through getOverlay()->addFloatingCard, which doesn't push into
    // _cardIndex today).
    if (_overlay) {
        const size_t n = _overlay->getFloatingCardCount();
        for (size_t i = 0; i < n; ++i) {
            DockCard* c = _overlay->getFloatingCard(i);
            if (c && c->getId() == cardId) {
                return c;
            }
        }
    }
    return nullptr;
}

DockCard* DockArea::getCard(Slot slot, size_t index) const {
    if ((int)slot < 0 || (int)slot >= (int)Slot::Count) {
        return nullptr;
    }
    const auto& vec = _slotCards[(int)slot];
    if (index >= vec.size()) {
        return nullptr;
    }
    return vec[index];
}

void DockArea::onRender(IRenderBackend& renderer) {
    AYUNREFERENCED_PARAM(renderer);
}

void DockArea::performLayout() {
    // First descend into children so the overlay + slot containers
    // get a layout pass.
    compoundDescendLayout(this);

    // Then split the DockArea's bounds into 5 regions according to the
    // slot weights. Order: Top strip (full width), Bottom strip (full
    // width), then Left + Center + Right columns inside the middle row.
    const math::FVector2 sz = getSize();
    const float w = sz.x;
    const float h = sz.y;

    const float wtTop    = effectiveWeight(*this, Slot::Top);
    const float wtBottom = effectiveWeight(*this, Slot::Bottom);
    const float wtLeft   = effectiveWeight(*this, Slot::Left);
    const float wtRight  = effectiveWeight(*this, Slot::Right);
    const float wtCenter = effectiveWeight(*this, Slot::Center);

    const float sumVert = wtTop + wtBottom + 1.0f; // middle row takes the rest
    const float topH    = h * (wtTop    / sumVert);
    const float botH    = h * (wtBottom / sumVert);
    const float midY    = topH;
    const float midH    = h - topH - botH;

    if (Widget* top = _slotContainers[(int)Slot::Top]) {
        top->setPosition(math::FVector2(0.0f, 0.0f));
        top->setSize(math::FVector2(w, topH));
    }
    if (Widget* bot = _slotContainers[(int)Slot::Bottom]) {
        bot->setPosition(math::FVector2(0.0f, midY + midH));
        bot->setSize(math::FVector2(w, botH));
    }

    const float sumHoriz = wtLeft + wtCenter + wtRight;
    const float leftW  = (sumHoriz > 0.0f) ? w * (wtLeft   / sumHoriz) : 0.0f;
    const float rightW = (sumHoriz > 0.0f) ? w * (wtRight  / sumHoriz) : 0.0f;
    const float centerW = w - leftW - rightW;

    if (Widget* left = _slotContainers[(int)Slot::Left]) {
        left->setPosition(math::FVector2(0.0f, midY));
        left->setSize(math::FVector2(leftW, midH));
    }
    // Center holds its own direct children; size them to fill the
    // middle region minus Left/Right columns.
    if (Widget* center = _slotContainers[(int)Slot::Center]) {
        center->setPosition(math::FVector2(leftW, midY));
        center->setSize(math::FVector2(centerW, midH));
        // Center's children must fill the Center container; without
        // this the center card stays at (0,0) relative to center
        // (which IS correct for local), but its world bounds may not
        // equal the slot bounds. Re-pin via inner layout pass.
        center->performLayout();
    }
    if (Widget* right = _slotContainers[(int)Slot::Right]) {
        right->setPosition(math::FVector2(leftW + centerW, midY));
        right->setSize(math::FVector2(rightW, midH));
    }

    // Overlay covers everything so floating cards can be positioned
    // anywhere within the DockArea bounds.
    if (_overlay) {
        _overlay->setPosition(math::FVector2(0.0f, 0.0f));
        _overlay->setSize(sz);
    }
}

void DockArea::onChildAdded(Widget* child) {
    AYUNREFERENCED_PARAM(child);
}

void DockArea::onChildRemoved(Widget* child) {
    AYUNREFERENCED_PARAM(child);
}

void DockArea::tearDownSlots() {
    // Drop cards first so they don't double-free during container teardown.
    for (int i = 0; i < (int)Slot::Count; ++i) {
        for (auto* c : _slotCards[i]) {
            if (c && c->getParent() == this) {
                // Already parented directly (Center) or indirectly
                // (through a VBox/HBox). Either way the container /
                // CompoundWidget destructor will free it.
            }
        }
        _slotCards[i].clear();
    }
    // _slotContainers holds raw pointers that are also attached as
    // children of DockArea. We MUST NOT delete them here — destroyWidgetTree
    // (or the direct delete in the test's `delete root` path) owns their
    // lifetime. tearDownSlots only clears the bookkeeping; the actual
    // slot container destruction is the parent's responsibility.
    _slotContainers[(int)Slot::Left]   = nullptr;
    _slotContainers[(int)Slot::Right]  = nullptr;
    _slotContainers[(int)Slot::Top]    = nullptr;
    _slotContainers[(int)Slot::Bottom] = nullptr;
    _slotContainers[(int)Slot::Center] = nullptr;
}

} // namespace ayt::ui