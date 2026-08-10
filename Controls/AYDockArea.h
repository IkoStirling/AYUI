#pragma once

#include "AYCompoundFocusableWidget.h"
#include "AYBox.h"
#include "AYDockOverlay.h"
#include "AYUIManager.h"
#include "AYDockCard.h"
#include "AYDockTrace.h"
#include "AYDockTabGroup.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::ui {

// DockArea - the editor-shell root that hosts the dock tree plus a
// DockOverlay for floating cards.
//
// The dock tree IS the AYUI widget tree: children are
// [root split node, DockOverlay]. The root is a VBox{HBox} split node
// with SplitterHandles between panels; panels are DockTabGroup leaves
// (the 5 legacy slots are pinned leaves whose leafId = slot name and
// which are never pruned). Nested splits created by edge drops are
// future phases. Drop hit-testing / guides follow live leaf geometry
// after the template is laid out so splitter-resized panels stay
// adaptive placement targets.
//
// The 5-slot legacy API (addCard(Slot)/getCardCount(Slot)/adoptCard/
// moveInSlot/...) is preserved verbatim and maps onto the pinned
// leaves, so hosts and tests keep working unchanged.
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
    // `slot`'s pinned leaf. Inline because the lambda body needs it.
    bool isCardInSlot(const DockCard* card, Slot slot) const {
        if (card == nullptr) return false;
        const int s = (int)slot;
        if (s < 0 || s >= (int)Slot::Count) return false;
        const DockTabGroup* leaf = _rootLeaves[s];
        return leaf != nullptr && leaf->containsCard(card);
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
    // NOTE: does NOT displace prior occupants — callers that need
    // single-occupant bands (redock / dockCard) must use adoptCard.
    void addCard(Slot slot, std::unique_ptr<DockCard> card);

    // Insert an external / floating card into `slot`, taking ownership.
    // If the slot is already occupied, prior cards are floated onto the
    // overlay (same policy as dockCard) so VBox does not stack them and
    // cover the band. Used by Gallery/Editor child-window redock.
    bool adoptCard(Slot slot, DockCard* card);

    // Remove a card by id. Returns true if a card was removed.
    bool removeCard(const std::string& cardId);

    // Close a card whether docked or floating on the overlay (destroys
    // the widget tree). Used by DockCard's header close affordance.
    bool closeCard(const std::string& cardId);

    // Find a card anywhere (slots or floating overlay) by id. Returns
    // nullptr if not found.
    DockCard* findCard(const std::string& cardId) const;

    // Cards currently docked in a slot = tabs of that slot's pinned
    // leaf, in insertion order. 0 until the tree exists.
    size_t getCardCount(Slot slot) const {
        if ((int)slot < 0 || (int)slot >= (int)Slot::Count) return 0;
        const DockTabGroup* leaf = _rootLeaves[(int)slot];
        return leaf != nullptr ? leaf->getTabCount() : 0;
    }
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

    // D5-redock: host-side bridge for a drag originating in a promoted
    // child window. The child's G12 drag session lives in the child
    // UIManager (never touches this tree), so isDockCardDrag(primary)
    // is false and the slot highlight must come from the host feeding
    // the cursor position (primary-client / world space) here every
    // frame. paintDropGuide then paints the slot under it. Clear with
    // clearExternalDropPos() when the drag leaves (or the child dies).
    void setExternalDropPos(const math::FVector2& pos) {
        _externalDropActive = true;
        _externalDropPos = pos;
    }
    void clearExternalDropPos() { _externalDropActive = false; }
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
    // ---- dock tree ----
    // children = [rootNode, overlay]. The root node is a VBox/HBox
    // split tree; leaves are DockTabGroups. Pinned leaves hold the 5
    // legacy slots (leafId = slot name, never pruned); split-created
    // leaves use g_N ids and prune when empty (future phases).
    //
    // Raw pointers (not unique_ptr) on purpose: every node is also
    // attached as a child of DockArea via addChild, so destroyWidgetTree
    // frees them when DockArea dies. Holding them via unique_ptr would
    // double-delete (once by destroyWidgetTree, once by ~unique_ptr).
    Widget* _rootNode = nullptr;
    DockTabGroup* _rootLeaves[(int)Slot::Count] = {};
    bool _templateBuilt = false;
    // First sync skips the pristine check (the tree may have been
    // built at size 0 — the first real-size performLayout must always
    // push the weight-derived geometry).
    bool _templateSynced = false;

    float _slotWeight[(int)Slot::Count] = {0.20f, 0.25f, 0.15f, 0.20f, 0.55f};
    float _slotMinSize[(int)Slot::Count] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // id -> card (across all slots + overlay). Ownership stays in the
    // dock tree / overlay.
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

    // D5-redock — external (child-window) drag bridge state, see
    // setExternalDropPos / clearExternalDropPos above.
    bool                 _externalDropActive = false;
    math::FVector2       _externalDropPos = math::FVector2(0.0f, 0.0f);

    // D3 — last-known cursor world position from the drag session.
    // UIManager doesn't expose lastMousePos publicly; we cache it in
    // onDragEnter for use by onDrop (overlay drop uses this position).
    math::FVector2 _dragEnterPos = math::FVector2(-1.0f, -1.0f);

    // ---- tree structure ops ----
    // Lazily build the template tree (root VBox{HBox} + pinned leaves
    // per enabled slot). Never runs twice; slot sizes start at 0 (fill)
    // and are pushed by the first syncTemplateIfPristine pass.
    void ensureRootTree();
    DockTabGroup* makePinnedLeaf(Slot slot);
    DockTabGroup* leafForSlot(Slot slot);
    DockTabGroup* findLeafOfCard(DockCard* card);
    void addTabToLeaf(DockTabGroup* leaf, DockCard* card);
    void removeTabFromLeaf(DockTabGroup* leaf, DockCard* card);

    // Push weight-derived slot sizes into the root template, but only
    // while the template is still "pristine": structure untouched AND
    // every panel slot size still equals the weight-derived value.
    // Splitter drags flip that and pixels become sticky — the fill
    // slot (mid / Center) absorbs any resize.
    void syncTemplateIfPristine(const math::FVector2& size);
    void applyTemplateGeometry(const math::FVector2& size);
    bool templateSizesMatchDerived(const math::FVector2& size) const;

    // Adaptive in-region drop: merge as a tab into the target leaf
    // without swapping / floating the prior occupant. Used when the
    // cursor is in the leaf's center zone.
    bool tabIntoSlot(const std::string& cardId, Slot target);
    bool dockCardAsTab(const std::string& cardId, Slot target);
};

} // namespace ayt::ui