#pragma once

#include "AYWidget.h"

namespace ayt::ui {

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

private:
    struct Slot {
        Widget* widget;
        float height;
    };
    std::vector<Slot> _slots;
};

class HBox : public BoxBase {
public:
    HBox();
    virtual ~HBox();

    void addWidget(Widget* widget, float width = 0.0f);
    void insertWidget(int index, Widget* widget, float width = 0.0f);

    void layoutChildren() override;

private:
    struct Slot {
        Widget* widget;
        float width;
    };
    std::vector<Slot> _slots;
};

} // namespace ayt::ui