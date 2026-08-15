#pragma once

#include "AYUI/Widget.h"
#include "AYUI/BoxSlotLimits.h"

namespace ayt::ui {

class SplitterHandle;

// BoxBase — shared container base for VBox / HBox.
//
// The splitter machinery lives HERE, not on HBox: the dock tree IS the
// widget tree itself (VBox/HBox are split nodes with
// [panel, SplitterHandle, panel, ...] children), so both orientations
// must support draggable splitter handles. Axis-neutral wording below:
// "size" = extent along the box's main axis (width for HBox, height for
// VBox); 0 = fill (remaining space split equally between fill slots).
class BoxBase : public CompoundWidget {
public:
    // Minimum size a panel can be dragged to via its splitter.
    // Historical HBox-only constant (kMinPanelWidth); hoisted here so
    // VBox splitters clamp identically.
    static constexpr float kMinPanelSize = 120.0f;

    BoxBase();
    virtual ~BoxBase();

    void setSpacing(float spacing) { _spacing = spacing; }
    float getSpacing() const { return _spacing; }

    void setPadding(float left, float top, float right, float bottom);
    const math::FVector4& getPadding() const { return _padding; }

    enum class Gravity { TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight };
    void setGravity(Gravity gravity) { _gravity = gravity; }
    Gravity getGravity() const { return _gravity; }

    // PR-B3 hotfix — natural content size = sum of visible children's
    // heights + spacing + padding for VBox; max child width + extra
    // padding for HBox. Used by ScrollView to compute the scrollable
    // extent when the content widget's own size is dictated by the
    // parent (content-fills-viewport case).
    math::FVector2 getPreferredContentSize() const override;

    // ---- slot management (shared by VBox and HBox) ----
    void addWidget(Widget* widget, float size = 0.0f, const BoxSlotLimits& limits = {});
    void insertWidget(int index, Widget* widget, float size = 0.0f, const BoxSlotLimits& limits = {});
    void removeWidget(Widget* widget);
    // Reorder an existing slot (and matching child). Used by layout editors.
    bool moveSlotToIndex(Widget* widget, size_t index);

    void setSlotLimits(int slotIndex, const BoxSlotLimits& limits);
    float slotSize(int slotIndex) const;
    void setSlotSize(int slotIndex, float size);

    // Index of `widget` in the layout slot array (_slots order = layout
    // order). getChildren() is NOT a valid index space for slot
    // arithmetic: addChild appends to the children vector while
    // insertWidget inserts into _slots, so the two orders diverge after
    // the first insert. Callers that mix slotSize/setSlotSize/
    // insertWidget with a children-derived index corrupt the wrong
    // slot. Returns -1 when the widget is not in this box.
    int slotIndexOf(Widget* widget) const;

    // Exposed for tests / debugging so callers can verify the cached
    // splitter flag without paying a dynamic_cast. Hot paths use the
    // cached flag directly via Slot::isSplitter.
    bool isSplitterSlot(int slotIndex) const;
    // Widget at `_slots[index]`, or nullptr if out of range.
    Widget* slotAt(int slotIndex) const;

    // ---- splitter machinery (axis-neutral) ----
    // `mouseAxisPos` is the cursor coordinate along this box's main
    // axis (SplitterHandle resolves x vs y through its orientation).
    void applySplitterDrag(int beforePanelSlot, int afterPanelSlot, float mouseAxisPos,
                           float dragStartMouseAxisPos, float dragStartPrimarySize, bool adjustBefore);
    void rebindSplitters();

    void layoutChildren() override;
    void performLayout() override;

    Widget* hitTest(const math::FVector2& worldPos) override;
    void render(IRenderBackend& renderer) override;

protected:
    struct Slot {
        Widget* widget = nullptr;
        float size = 0.0f;
        BoxSlotLimits limits;
        // Cached at insertion time so hot paths (render / hitTest / layout)
        // don't pay a dynamic_cast per slot per frame. See addWidget.
        bool isSplitter = false;
    };

    // Axis abstraction — the only orientation-specific piece. HBox
    // returns the inner width, VBox the inner height (content minus
    // padding on the main axis). Used by splitter clamping only;
    // layoutChildren stays virtual per orientation (VBox keeps its
    // natural-height cache, HBox keeps its splitter-fill repair pass).
    virtual float axisContentLength() const = 0;

    int panelSlotBefore(int slotIndex) const;
    int panelSlotAfter(int slotIndex) const;
    void bindSplitter(SplitterHandle* splitter, int splitterSlotIndex);

    float resolveMinSlotSize(const Slot& slot) const;
    float resolveMaxSlotSize(const Slot& slot) const;
    float minSlotSize(int slotIndex) const;
    float maxSlotSize(int slotIndex) const;
    float clampSlotSize(int slotIndex, float size) const;

    // True when the slot participates in main-axis layout (visible child
    // still parented to this box). Shared by layoutChildren and the
    // splitter max-size reserve so drag clamps match painted widths.
    bool slotOccupiesSpace(const Slot& slot) const;

    // If equal-share fill panels would land below their mins, shrink
    // fixed panels that are still above min until fills fit (or every
    // fixed panel is already at its floor).
    void stealFromFixedToSatisfyFillMins(float availableMain);

    // VBox caches pre-stretch natural height for ScrollView. Slot edits
    // (add/remove/resize) must drop that cache or a later page switch
    // walks a stale height and the scrollbar stays disabled.
    virtual void invalidateNaturalSizeCache() {}

    std::vector<Slot> _slots;
    float _spacing;
    math::FVector4 _padding;
    Gravity _gravity = Gravity::TopLeft;
};

class VBox : public BoxBase {
public:
    VBox();
    virtual ~VBox();

    // PR-B3 hotfix (Bug #4 follow-up) — exposed for the
    // walkNaturalHeight walker in AYBox.cpp so a nested VBox can
    // report its pre-fill-stretch natural height without us having
    // to re-derive it. Returns -1.0f if layout hasn't run yet
    // (the walker falls back to the recursive walk in that case).
    float getCachedNaturalHeight() const { return _naturalHeight; }

    // PR-B3 hotfix override — sum visible children's slot heights +
    // spacing + padding (vs BoxBase default which returns getSize()).
    // HBox stays on BoxBase default (no override in this commit).
    math::FVector2 getPreferredContentSize() const override;

    void layoutChildren() override;

protected:
    float axisContentLength() const override;
    void invalidateNaturalSizeCache() override { _naturalHeight = -1.0f; }

private:
    // PR-B3 hotfix (Bug #4 follow-up) — natural height = sum of
    // children's natural heights (without fill stretch). Cached each
    // layoutChildren pass so getPreferredContentSize can return it
    // without recomputing. Falls back to getSize().y if layout hasn't
    // run yet (freshly added children, before first tick).
    float _naturalHeight = -1.0f;
};

class HBox : public BoxBase {
public:
    HBox();
    virtual ~HBox();

    void layoutChildren() override;

protected:
    float axisContentLength() const override;
};

} // namespace ayt::ui
