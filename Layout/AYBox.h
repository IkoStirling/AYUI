#pragma once

#include "AYWidget.h"
#include "AYBoxSlotLimits.h"

namespace ayt::ui {

class SplitterHandle;

class BoxBase : public CompoundWidget {
public:
    BoxBase();
    virtual ~BoxBase();

    void setSpacing(float spacing) { _spacing = spacing; }
    float getSpacing() const { return _spacing; }

    void setPadding(float left, float top, float right, float bottom);
    const math::FVector4& getPadding() const { return _padding; }

    enum class Gravity { TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight };
    void setGravity(Gravity gravity) { _gravity = gravity; }
    Gravity getGravity() const { return _gravity; }

    void layoutChildren() override;

protected:
    float _spacing;
    math::FVector4 _padding;
    Gravity _gravity = Gravity::TopLeft;
};

class VBox : public BoxBase {
public:
    VBox();
    virtual ~VBox();

    void addWidget(Widget* widget, float height = 0.0f);
    void insertWidget(int index, Widget* widget, float height = 0.0f);

    void layoutChildren() override;
    void performLayout() override;

private:
    struct Slot {
        Widget* widget;
        float height;
    };
    std::vector<Slot> _slots;
};

class HBox : public BoxBase {
public:
    static constexpr float kMinPanelWidth = 120.0f;

    HBox();
    virtual ~HBox();

    void addWidget(Widget* widget, float width = 0.0f, const BoxSlotLimits& limits = {});
    void insertWidget(int index, Widget* widget, float width = 0.0f, const BoxSlotLimits& limits = {});

    void setSlotLimits(int slotIndex, const BoxSlotLimits& limits);

    float slotWidth(int slotIndex) const;

    void applySplitterDrag(int leftPanelSlot, int rightPanelSlot, float mouseWorldX,
                           float dragStartMouseX, float dragStartPrimaryWidth, bool adjustLeft);

    void rebindSplitters();

    // Exposed for tests / debugging so callers can verify the cached
    // splitter flag without paying a dynamic_cast. Hot paths use the
    // cached flag directly via Slot::isSplitter.
    bool isSplitterSlot(int slotIndex) const;

    Widget* hitTest(const math::FVector2& worldPos) override;
    void render(IRenderBackend& renderer) override;

    void layoutChildren() override;
    void performLayout() override;

private:
    struct Slot {
        Widget* widget = nullptr;
        float width = 0.0f;
        BoxSlotLimits limits;
        // Cached at insertion time so hot paths (render / hitTest / layout)
        // don't pay a dynamic_cast per slot per frame. See HBox::addWidget.
        bool isSplitter = false;
    };

    int panelSlotBefore(int slotIndex) const;
    int panelSlotAfter(int slotIndex) const;
    void bindSplitter(SplitterHandle* splitter, int splitterSlotIndex);

    float contentWidth() const;
    float resolveMinSlotWidth(const Slot& slot) const;
    float resolveMaxSlotWidth(const Slot& slot) const;
    float minSlotWidth(int slotIndex) const;
    float maxSlotWidth(int slotIndex) const;
    float clampSlotWidth(int slotIndex, float width) const;

    std::vector<Slot> _slots;
};

} // namespace ayt::ui
