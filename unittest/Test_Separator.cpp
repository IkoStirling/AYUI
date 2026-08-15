#include "AYTest.h"
#include "AYUI/Separator.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Separator)

TEST_CASE(separator_initial_state) {
    Separator sep;
    CHECK(sep.getOrientation() == Separator::Orientation::Horizontal);
    CHECK_FLOAT_EQ(sep.getThickness(), 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(sep.getInset(), 0.0f, 1e-5f);
}

TEST_CASE(separator_set_orientation_thickness_inset) {
    Separator sep;
    sep.setOrientation(Separator::Orientation::Vertical);
    CHECK(sep.getOrientation() == Separator::Orientation::Vertical);

    sep.setThickness(3.0f);
    CHECK_FLOAT_EQ(sep.getThickness(), 3.0f, 1e-5f);

    sep.setThickness(0.0f);   // clamps to >= 1
    CHECK_FLOAT_EQ(sep.getThickness(), 1.0f, 1e-5f);

    sep.setInset(8.0f);
    CHECK_FLOAT_EQ(sep.getInset(), 8.0f, 1e-5f);

    sep.setInset(-2.0f);   // clamps to >= 0
    CHECK_FLOAT_EQ(sep.getInset(), 0.0f, 1e-5f);
}

TEST_CASE(separator_render_emits_rect) {
    Separator sep;
    sep.setSize(FVector2(100.0f, 4.0f));
    sep.setPosition(FVector2(0.0f, 0.0f));
    sep.performLayout();

    MockRenderer renderer;
    sep.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    CHECK(rectCount >= 1);
}

TEST_CASE(separator_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("Separator"));

    Widget* widget = factory.create("Separator");
    CHECK_NOT_NULL(widget);
    Separator* original = dynamic_cast<Separator*>(widget);
    CHECK_NOT_NULL(original);
    original->setId("sep1");
    original->setOrientation(Separator::Orientation::Vertical);
    original->setThickness(2.0f);
    original->setInset(4.0f);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"Separator\"") != std::string::npos);
    CHECK(json.find("vertical") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    Separator* restoredSep = dynamic_cast<Separator*>(restored);
    CHECK_NOT_NULL(restoredSep);
    CHECK(restoredSep->getId() == "sep1");
    CHECK(restoredSep->getOrientation() == Separator::Orientation::Vertical);
    CHECK_FLOAT_EQ(restoredSep->getThickness(), 2.0f, 1e-5f);
    CHECK_FLOAT_EQ(restoredSep->getInset(), 4.0f, 1e-5f);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

TEST_SUITE_END

