#include "AYTest.h"
#include "AYUI/ToolBarSeparator.h"
#include "AYUI/Separator.h"
#include "AYUI/ToolBar.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_ToolBarSeparator)

TEST_CASE(toolbarseparator_is_a_separator) {
    // G7 — ToolBarSeparator is a Separator subclass. dynamic_cast
    // both ways must succeed (is-a relationship).
    ToolBarSeparator tbs;
    Separator* base = &tbs;
    CHECK_NOT_NULL(base);
    ToolBarSeparator* derived = dynamic_cast<ToolBarSeparator*>(base);
    CHECK_NOT_NULL(derived);
    CHECK(derived == &tbs);
}

TEST_CASE(toolbarseparator_default_orientation_vertical) {
    // G7 — toolbars group buttons horizontally, so the separator
    // between groups is vertical by default.
    ToolBarSeparator tbs;
    CHECK(tbs.getOrientation() == Separator::Orientation::Vertical);
}

TEST_CASE(toolbarseparator_default_palette_is_toolbar_cool) {
    // G7 — the toolbar palette is a slightly cooler gray than the
    // base Separator's neutral (which is 0.5/0.5/0.55). The base
    // RGB is 0.42/0.42/0.48 so we assert each channel explicitly.
    ToolBarSeparator tbs;
    const auto& c = tbs.getColor();
    CHECK_FLOAT_EQ(c.x, 0.42f, 1e-5f);  // R
    CHECK_FLOAT_EQ(c.y, 0.42f, 1e-5f);  // G
    CHECK_FLOAT_EQ(c.z, 0.48f, 1e-5f);  // B
    CHECK_FLOAT_EQ(c.w, 0.9f,  1e-5f);  // A
}

TEST_CASE(toolbarseparator_default_thickness_is_one) {
    // G7 — thickness defaults to 1.0f so a single-pixel vertical
    // rule renders against the toolbar background.
    ToolBarSeparator tbs;
    CHECK_FLOAT_EQ(tbs.getThickness(), 1.0f, 1e-5f);
}

TEST_CASE(toolbarseparator_default_size_matches_doc) {
    // G7 — defaults: 1px wide, kDefaultBarHeight (24) tall.
    ToolBarSeparator tbs;
    const FVector2 s = tbs.getSize();
    CHECK_FLOAT_EQ(s.x, ToolBarSeparator::kDefaultBarWidth,  1e-5f);
    CHECK_FLOAT_EQ(s.y, ToolBarSeparator::kDefaultBarHeight, 1e-5f);
}

TEST_CASE(toolbarseparator_factory_creates_toolbarseparator) {
    // G7 — JSON-loaded layouts must be able to instantiate by name.
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("ToolBarSeparator"));
    Widget* w = factory.create("ToolBarSeparator");
    CHECK_NOT_NULL(w);
    ToolBarSeparator* tbs = dynamic_cast<ToolBarSeparator*>(w);
    CHECK_NOT_NULL(tbs);
    CHECK(tbs->getOrientation() == Separator::Orientation::Vertical);
    destroyWidgetTree(w);
}

TEST_CASE(toolbarseparator_serializer_round_trip) {
    // G7 — must serialize as "ToolBarSeparator" (its factory type),
    // NOT "Separator", so the JSON type tag round-trips losslessly.
    ToolBarSeparator tbs;
    tbs.setId("tbs1");
    tbs.setThickness(2.0f);
    tbs.setInset(4.0f);
    // Note: orientation is already Vertical by default for ToolBarSeparator.

    std::string json = WidgetSerializer::serialize(&tbs);
    CHECK(json.find("\"type\": \"ToolBarSeparator\"") != std::string::npos);
    CHECK(json.find("vertical") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    ToolBarSeparator* rt = dynamic_cast<ToolBarSeparator*>(restored);
    CHECK_NOT_NULL(rt);
    CHECK(rt->getId() == "tbs1");
    CHECK(rt->getOrientation() == Separator::Orientation::Vertical);
    CHECK_FLOAT_EQ(rt->getThickness(), 2.0f, 1e-5f);
    CHECK_FLOAT_EQ(rt->getInset(), 4.0f, 1e-5f);

    destroyWidgetTree(restored);
}

TEST_CASE(toolbarseparator_serializer_does_not_mislabel_as_separator) {
    // G7 — guard against regression where the Serializer's if/else-if
    // chain checks Separator* BEFORE ToolBarSeparator* (the latter is
    // a subclass of the former). If the order is wrong, a ToolBarSeparator
    // would serialize as "Separator" and lose its type identity on
    // round-trip — the test `factory_creates_toolbarseparator` would
    // fail downstream.
    ToolBarSeparator tbs;
    std::string json = WidgetSerializer::serialize(&tbs);
    // Must contain "ToolBarSeparator" — and importantly, the position
    // of the `"type"` field must point at it. We use a regex-free
    // heuristic: the substring `"type": "ToolBarSeparator"` MUST appear.
    CHECK(json.find("\"type\": \"ToolBarSeparator\"") != std::string::npos);
    // And the bare Separator type MUST NOT appear as the type field —
    // check that "Separator" alone isn't standing in for "type".
    CHECK(json.find("\"type\": \"Separator\"") == std::string::npos);
}

TEST_CASE(toolbar_toolbar_addSeparator_creates_toolbarseparator) {
    // G7 — ToolBar::addSeparator() must use the new ToolBarSeparator,
    // not a raw Separator. Verify by dynamic_cast.
    ToolBar tb;
    tb.addButton(L"Save");
    tb.addSeparator();
    tb.addButton(L"Open");
    CHECK(tb.getItemCount() == 3u);

    Widget* sep = tb.getItem(1);
    CHECK_NOT_NULL(sep);
    ToolBarSeparator* tbs = dynamic_cast<ToolBarSeparator*>(sep);
    CHECK_NOT_NULL(tbs);
    // And of course it's still a Separator.
    Separator* asSep = dynamic_cast<Separator*>(sep);
    CHECK_NOT_NULL(asSep);
}

TEST_CASE(toolbarseparator_render_emits_rect) {
    // G7 — rendering delegates to Separator::onRender which emits a
    // single rect draw call. Sanity-check that it draws at all.
    ToolBarSeparator tbs;
    tbs.setSize(FVector2(2.0f, 24.0f));
    tbs.setPosition(FVector2(0.0f, 0.0f));
    tbs.performLayout();

    MockRenderer renderer;
    tbs.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    CHECK(rectCount >= 1);
}

TEST_SUITE_END