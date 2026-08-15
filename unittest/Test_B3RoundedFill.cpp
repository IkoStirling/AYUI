#include "AYTest.h"
#include "AYUI/ComboBox.h"
#include "AYUI/ListView.h"
#include "AYUI/TextInput.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/CheckBox.h"
#include "AYUI/ScrollView.h"
#include "AYUI/RadioButton.h"
#include "AYUI/Slider.h"
#include "AYUI/MockRenderer.h"

#include <cmath>

using namespace ayt::ui;
using namespace ayt::math;

// B3 sweep: every control that draws a rounded border (radius 2px, or
// half-size for the radio circle) must fill with the SAME radius — a
// square fill pokes out of the ring corners. MockRenderer rides the
// radius in floatParam1; for all controls below the background fill is
// the FIRST draw call (onRender draws bg before chrome/children).
TEST_SUITE(AYUI_B3_RoundedFill)

TEST_CASE(b3_fills_follow_border_radius) {
    {   // ComboBox
        ComboBox w;
        w.setSize(FVector2(120.0f, 28.0f));
        MockRenderer r;
        w.render(r);
        CHECK(r.getDrawCalls().size() >= 1u);
        CHECK_FLOAT_EQ(r.getDrawCalls()[0].floatParam1, 2.0f, 1e-5f);
    }
    {   // ListView
        ListView w;
        w.setSize(FVector2(120.0f, 80.0f));
        MockRenderer r;
        w.render(r);
        CHECK(r.getDrawCalls().size() >= 1u);
        CHECK_FLOAT_EQ(r.getDrawCalls()[0].floatParam1, 2.0f, 1e-5f);
    }
    {   // TextInput
        TextInput w;
        w.setSize(FVector2(120.0f, 26.0f));
        MockRenderer r;
        w.render(r);
        CHECK(r.getDrawCalls().size() >= 1u);
        CHECK_FLOAT_EQ(r.getDrawCalls()[0].floatParam1, 2.0f, 1e-5f);
    }
    {   // ScrollBar track
        ScrollBar w;
        w.setSize(FVector2(14.0f, 100.0f));
        MockRenderer r;
        w.render(r);
        CHECK(r.getDrawCalls().size() >= 1u);
        CHECK_FLOAT_EQ(r.getDrawCalls()[0].floatParam1, 2.0f, 1e-5f);
    }
    {   // ProgressBar unfilled base
        ProgressBar w;
        w.setSize(FVector2(150.0f, 14.0f));
        MockRenderer r;
        w.render(r);
        CHECK(r.getDrawCalls().size() >= 1u);
        CHECK_FLOAT_EQ(r.getDrawCalls()[0].floatParam1, 2.0f, 1e-5f);
    }
    {   // CheckBox box
        CheckBox w;
        w.setSize(FVector2(90.0f, 24.0f));
        MockRenderer r;
        w.render(r);
        CHECK(r.getDrawCalls().size() >= 1u);
        CHECK_FLOAT_EQ(r.getDrawCalls()[0].floatParam1, 2.0f, 1e-5f);
    }
    {   // ScrollView bg
        ScrollView w;
        w.setSize(FVector2(160.0f, 100.0f));
        MockRenderer r;
        w.render(r);
        CHECK(r.getDrawCalls().size() >= 1u);
        CHECK_FLOAT_EQ(r.getDrawCalls()[0].floatParam1, 2.0f, 1e-5f);
    }
    {   // RadioButton circle — radius is HALF the circle size (round fill)
        RadioButton w;
        w.setSize(FVector2(90.0f, 24.0f));
        MockRenderer r;
        w.render(r);
        CHECK(r.getDrawCalls().size() >= 1u);
        CHECK_FLOAT_EQ(r.getDrawCalls()[0].floatParam1, 8.0f, 1e-5f);
    }
}

TEST_CASE(b3_slider_handle_fill_is_rounded) {
    Slider w;
    w.setSize(FVector2(150.0f, 28.0f));
    MockRenderer r;
    w.render(r);

    // Locate the handle fill by its default colour (0.85, 0.88, 0.92).
    bool found = false;
    for (const auto& dc : r.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect &&
            std::abs(dc.color.x - 0.85f) < 0.01f &&
            std::abs(dc.color.y - 0.88f) < 0.01f &&
            std::abs(dc.color.z - 0.92f) < 0.01f) {
            CHECK_FLOAT_EQ(dc.floatParam1, 2.0f, 1e-5f);
            found = true;
        }
    }
    CHECK(found);
}

TEST_SUITE_END
