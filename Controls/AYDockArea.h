#pragma once

#include "AYCompoundFocusableWidget.h"
#include "AYBox.h"
#include "AYDockOverlay.h"
#include "AYUIManager.h"
#include "AYDockCard.h"
#include "AYDockTrace.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::ui {

// D1: DockArea - the editor-shell root that hosts named slots
// (Left/Right/Top/Bottom/Center) plus a DockOverlay for floating cards.
//
// Each slot owns a VBox (Left/Right stack vertically; Top/Bottom stack
// horizontally; Center holds the main view). Cards added to a slot are
// pushed into that slot's VBox in insertion order; the existing layout
// engine then sizes them with fill-style stretching.
//
// D1 ships the slot tree, addCard / removeCard / findCard, and overlay
// ownership. Tear-off UX is D3; persistence is D4.
class DockArea : public CompoundFocusableWidget {
public:
    enum class Slot {
        Left,
        Right,
        Top,
        Bottom,
        Center,
        Count
    };

    // Ctor lives in AYDockArea.cpp — MUST NOT be inline in this header.
    // Gallery constructs DockArea via WidgetFactory (AYWidgetFactory.cpp);
    // an inline ctor meant drop/Center fixes only applied when that TU
    // was rebuilt, which is why logs still showed centerType=Widget and
    // same-slot floatCard with no onDrop trace.
    DockArea();

public:

    // Frees overlay + slot containers + docked cards (heap children).
    // Stack DockArea must NOT be passed to destroyWidgetTree — that would
    // `delete` the stack object. Rely on this dtor (or heap+destroyWidgetTree).
    ~DockArea() override;

    // D3 internal — used by the ctor's onDrop lambda to enforce
    // K-INV-D3-1 (same-slot no-op). Returns true if `card` lives in
    // `slot`. Inline because the lambda body needs it.
    bool isCardInSlot(const DockCard* card, Slot slot) const {
        if (card == nullptr) return false;
        const int s = (int)slot;
        if (s < 0 || s >= (int)Slot::Count) return false;
        for (DockCard* c : _slotCards[s]) {
            if (c == card) return true;
        }
        return false;
    }

    // Slot weight (0..1) used by the layout pass for relative sizing.
    // Weights of 0 fall back to a hard-coded sensible default
    // (Left=0.20 / Right=0.25 / Top=0.15 / Bottom=0.20 / Center=0.55).
    void setSlotWeight(Slot slot, float weight) {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return;
        _slotWeight[(int)slot] = weight;
    }
    float getSlotWeight(Slot slot) const {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return 0.0f;
        return _slotWeight[(int)slot];
    }

    // Slot min size (logical pixels). Default 0 for all.
    void setSlotMinSize(Slot slot, float minSize) {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return;
        _slotMinSize[(int)slot] = minSize;
    }
    float getSlotMinSize(Slot slot) const { return _slotMinSize[(int)slot]; }

    // Add a card to a named slot. DockArea takes ownership of `card`.
    // Adding a card with the same id as an existing card replaces the old
    // card (mirroring the Loader's "last write wins" rule).
    void addCard(Slot slot, std::unique_ptr<DockCard> card);

    // Remove a card by id. Returns true if a card was removed.
    bool removeCard(const std::string& cardId);

    // Find a card anywhere (slots or floating overlay) by id. Returns
    // nullptr if not found.
    DockCard* findCard(const std::string& cardId) const;

    // Cards currently docked in a slot, in insertion order.
    size_t getCardCount(Slot slot) const { return _slotCards[(int)slot].size(); }
    DockCard* getCard(Slot slot, size_t index) const;

    // Convenience: turn a string id into a Slot enum. Returns false if
    // the string is not a valid slot name.
    static bool parseSlot(const std::string& name, Slot& outSlot);

    // The overlay that owns floating cards. Always non-null after ctor.
    DockOverlay* getOverlay() const { return _overlay; }

    void onRender(IRenderBackend& renderer) override;
    // Paint drop highlight AFTER children so opaque DockCards cannot
    // cover the pre-slot fill (Gallery "preslot matched but blocked").
    void render(IRenderBackend& renderer) override;
    // Translucent drop-zone for the slot under the cursor while a
    // DockCard drag is active (no-op when idle). Hosts may call after
    // tree render if DockArea::render was skipped by a container path.
    void paintDropGuide(IRenderBackend& renderer);
    void performLayout() override;

    // =================================================================
    // D3 — Tear-off UX. These are the public surface that the
    // DockArea's G12 drop callbacks call. Hosts don't normally call
    // them directly; the drag/drop system invokes floatCard /
    // dockCard as a side-effect of the drop event.
    // =================================================================
    // Tear a docked card off into a floating card on the overlay.
    // Returns false if no card with `cardId` lives in any slot.
    // K-INV-D3-7: `_cardIndex` is erased here because the card leaves
    // the slot namespace.
    bool floatCard(const std::string& cardId, const math::FVector2& pos);

    // Re-dock a floating card into a named slot. Returns false if no
    // floating card with `cardId` exists. K-INV-D3-1: same-slot
    // transition is a no-op (handled at the drop callback level).
    bool dockCard(const std::string& cardId, Slot target);

    // Code-review 2026-08-02 #8: same-tree move (slot→slot) without
    // routing through the overlay. Avoids the visible flicker the
    // dockCard/floatCard pair caused on docked→docked drops (the card
    // momentarily flashed at the cursor position before snapping into
    // the new slot). Returns false if the card is not in any slot.
    bool moveInSlot(const std::string& cardId, Slot target);

    // Returns the slot whose region contains `worldPos`, or Slot::Count
    // if none. Mirrors the region math in performLayout() so the
    // drop-zone highlight stays in lockstep with the layout pass.
    Slot hitTestSlot(const math::FVector2& worldPos) const;

    // PR-Dock-SlotHighlight: the world-space rect of a slot's drop
    // region, using the SAME weights as hitTestSlot (configured
    // weights, not layout weights) so the pre-slot highlight paints
    // exactly where a drop would land. Slot::Count → empty rect.
    math::FRectangle getSlotRect(Slot slot) const;

    // True if `worldPos` is inside the overlay's bounds but not on any
    // floating card. K-INV-D3-4 (DockOverlay pass-through) requires
    // this distinction: drop on the overlay EMPTY area = float, drop
    // on a floating card = no-op (handled by DockOverlay::onDrop).
    bool hitTestOverlay(const math::FVector2& worldPos) const;

protected:
    // Mirrors CompoundFocusableWidget hooks. Default empty.
    void onChildAdded(Widget* child);
    void onChildRemoved(Widget* child);

private:
    // The VBox / HBox for each slot. Center is a child Widget (no VBox
    // wrapper) because Center typically hosts a single large panel
    // (viewport) rather than a stack.
    //
    // Raw pointers (not unique_ptr) on purpose: every entry is also
    // attached as a child of DockArea via addChild, so destroyWidgetTree
    // frees them when DockArea dies. Holding them via unique_ptr would
    // double-delete (once by destroyWidgetTree, once by ~unique_ptr).
    Widget* _slotContainers[(int)Slot::Count] = {};   // {} zero-initialises the whole array (NOT just [0])
    float _slotWeight[(int)Slot::Count] = {0.20f, 0.25f, 0.15f, 0.20f, 0.55f};
    float _slotMinSize[(int)Slot::Count] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<DockCard*> _slotCards[(int)Slot::Count] = {};  // {} forces element-wise default-ctor

    // id -> card (across all slots + overlay). Ownership stays in
    // _slotCards / overlay.
    std::unordered_map<std::string, DockCard*> _cardIndex;

    // Floating overlay (always present, child of DockArea). Raw pointer
    // because lifetime is owned by the children tree; we don't want a
    // unique_ptr here (it would need DockOverlay's complete type at the
    // point DockArea's dtor is implicit-synthesised).
    DockOverlay* _overlay = nullptr;

    // D3 — render-time highlight state. Set in onDragEnter, cleared in
    // onDragLeave / onDrop. Stored as enum + bool (not Widget*) so we
    // can't hold a stale pointer to a torn-off card.
    Slot _hoveredSlot = Slot::Count;   // Slot::Count sentinel = no hover
    bool _hoveredOverlay = false;

    // D3 — last-known cursor world position from the drag session.
    // UIManager doesn't expose lastMousePos publicly; we cache it in
    // onDragEnter for use by onDrop (overlay drop uses this position).
    math::FVector2 _dragEnterPos = math::FVector2(-1.0f, -1.0f);

    // Code-review 2026-08-02 #20: tearDownSlots() was dead (never
    // called) AND inconsistent (didn't clear _cardIndex, leaving stale
    // pointers that would UAF via removeCard on the next rebuild).
    // Removed body + declaration; if a future "rebuild slots" path
    // needs it, write a new function with full bookkeeping (cards +
    // containers + _cardIndex + correct ownership semantics).
};

} // namespace ayt::ui