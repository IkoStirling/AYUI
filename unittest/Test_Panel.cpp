#include "AYTest.h"
#include "AYUI/Panel.h"
#include "AYUI/Button.h"
#include "AYUI/TextLabel.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Style.h"

#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Panel)

// R-10 (C-1): Panel is registered in WidgetFactory under "Panel". A round-trip
// via WidgetSerializer must serialize as type "Panel" and deserialize back
// into a Panel instance (not a generic Widget).
TEST_CASE(panel_registered_in_factory) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK(factory.isRegistered("Panel"));

    Widget* widget = factory.create("Panel");
    CHECK(widget != nullptr);
    Panel* panel = dynamic_cast<Panel*>(widget);
    CHECK(panel != nullptr);

    destroyWidgetTree(widget);
}

// R-10: Panel is a CompoundWidget. CompoundWidget's hitTest descends into
// children (R-6 invariant). A Button nested inside a Panel must be hit-testable
// through the Panel.
TEST_CASE(panel_hittest_descends_to_children) {
    Panel* panel = new Panel();
    panel->setSize(FVector2(300.0f, 200.0f));
    panel->setPosition(FVector2(0.0f, 0.0f));

    Button* btn = new Button();
    btn->setId("inner_btn");
    btn->setSize(FVector2(80.0f, 30.0f));
    btn->setPosition(FVector2(20.0f, 20.0f));
    panel->addChild(btn);

    Widget* hit = panel->hitTest(FVector2(40.0f, 30.0f));
    CHECK(hit == btn);

    // Far outside the panel — null.
    hit = panel->hitTest(FVector2(999.0f, 999.0f));
    CHECK(hit == nullptr);

    destroyWidgetTree(panel);
}

// R-10: Panel's own onRender issues a drawRect for the background and
// (with border enabled) a drawBorderRect. IRenderBackend::drawBorderRect
// fans out into 4-5 separate drawRect calls (top/bottom/left/right edges
// + optional corners). Without a registered StyleSheet the onRender falls
// back to hardcoded neutral values, so the test does not need to wire
// StyleManager.
TEST_CASE(panel_render_emits_background_and_border) {
    Panel* panel = new Panel();
    panel->setSize(FVector2(120.0f, 80.0f));
    panel->setPosition(FVector2(10.0f, 20.0f));
    panel->setBorderEnabled(true);

    MockRenderer renderer;
    panel->render(renderer);

    // 1 background fill + 4 border edges (cornerRadius=0 skips the 4 corner
    // fill rects) = 5 rects total.
    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) {
            ++rectCount;
        }
    }
    CHECK(rectCount == 5);

    delete panel;
}

// R-10: setBorderEnabled(false) skips the border draw call entirely — only
// the background fill remains.
TEST_CASE(panel_border_disabled_skips_border) {
    Panel* panel = new Panel();
    panel->setSize(FVector2(100.0f, 100.0f));
    panel->setBorderEnabled(false);

    MockRenderer renderer;
    panel->render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) {
            ++rectCount;
        }
    }
    CHECK(rectCount == 1);

    delete panel;
}

// R-10: default padding is (4, 4, 4, 4); setPadding(overrides) is reflected
// back via getPadding().
TEST_CASE(panel_padding_default_and_setter) {
    Panel* panel = new Panel();
    FVector4 def = panel->getPadding();
    CHECK_FLOAT_EQ(def.x, 4.0f, 1e-5f);
    CHECK_FLOAT_EQ(def.y, 4.0f, 1e-5f);
    CHECK_FLOAT_EQ(def.z, 4.0f, 1e-5f);
    CHECK_FLOAT_EQ(def.w, 4.0f, 1e-5f);

    panel->setPadding(2.0f, 6.0f, 10.0f, 14.0f);
    FVector4 got = panel->getPadding();
    CHECK_FLOAT_EQ(got.x, 2.0f, 1e-5f);
    CHECK_FLOAT_EQ(got.y, 6.0f, 1e-5f);
    CHECK_FLOAT_EQ(got.z, 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(got.w, 14.0f, 1e-5f);

    // FVector4 overload.
    panel->setPadding(FVector4(1.0f, 2.0f, 3.0f, 4.0f));
    got = panel->getPadding();
    CHECK_FLOAT_EQ(got.x, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(got.y, 2.0f, 1e-5f);

    delete panel;
}

// R-10: WidgetSerializer round-trip preserves the "Panel" type. R-8 (B11) is
// the same fix path — a fresh Panel without explicit styleId still serializes
// as type "Panel", not as the style id.
TEST_CASE(panel_serialize_type_field) {
    Panel* panel = new Panel();
    panel->setId("settings_group");
    panel->setSize(FVector2(300.0f, 200.0f));
    panel->setPosition(FVector2(10.0f, 20.0f));
    panel->setStyleId("panel_default");
    panel->setBorderEnabled(false);

    std::string json = WidgetSerializer::serialize(panel);
    CHECK(json.find("\"type\": \"Panel\"") != std::string::npos);
    CHECK(json.find("\"borderEnabled\"") != std::string::npos);
    CHECK(json.find("false") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK(restored != nullptr);
    Panel* restoredPanel = dynamic_cast<Panel*>(restored);
    CHECK(restoredPanel != nullptr);
    CHECK(restoredPanel->getId() == "settings_group");
    CHECK(restoredPanel->isBorderEnabled() == false);

    destroyWidgetTree(panel);
    destroyWidgetTree(restored);
}

// R-10: a Panel with children round-trips through JSON — the "children"
// subtree is preserved.
TEST_CASE(panel_serialize_with_children) {
    Panel* panel = new Panel();
    panel->setId("group");
    panel->setSize(FVector2(200.0f, 100.0f));

    TextLabel* label = new TextLabel();
    label->setId("lbl");
    label->setText(L"Settings");
    panel->addChild(label);

    Button* btn = new Button();
    btn->setId("apply_btn");
    btn->setText(L"Apply");
    panel->addChild(btn);

    std::string json = WidgetSerializer::serialize(panel);
    CHECK(json.find("\"type\": \"Panel\"") != std::string::npos);
    CHECK(json.find("TextLabel") != std::string::npos);
    CHECK(json.find("Button") != std::string::npos);
    CHECK(json.find("\"id\": \"lbl\"") != std::string::npos);
    CHECK(json.find("\"id\": \"apply_btn\"") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK(restored != nullptr);
    CHECK(restored->getChildren().size() == 2);

    destroyWidgetTree(panel);
    destroyWidgetTree(restored);
}

// R-10: Panel's style id "panel_default" is pre-registered by StyleSheet's
// default constructor (StyleBuilder::makePanel). StyleManager::getStyle must
// resolve it without any explicit loadFromString call.
TEST_CASE(panel_default_style_registered) {
    StyleSheet sheet;
    const WidgetStyle* s = sheet.getStyle("panel_default");
    CHECK(s != nullptr);
    // makePanel() sets padding to (4, 4, 4, 4).
    CHECK_FLOAT_EQ(s->padding.x, 4.0f, 1e-5f);
    CHECK_FLOAT_EQ(s->padding.y, 4.0f, 1e-5f);
}

// C-2 (style-dedup follow-up): Panel with styleId="panel_default" and a
// wired StyleSheet must use the resolved style's colors — not the
// hardcoded fallback. Pre-fix the makePanel bg collided with makeDefault's
// bg, the bgIsDefault sentinel dropped it, and Panel silently fell back
// even with a stylesheet wired in. This pins the working post-fix path.
TEST_CASE(panel_uses_panel_default_when_wired) {
    StyleManager::get().setStyleSheet(nullptr);

    StyleSheet sheet;  // pre-registers panel_default with bg = (0.18, 0.18, 0.20, 1)
    StyleManager::get().setStyleSheet(&sheet);

    Panel panel;
    panel.setSize({120.0f, 80.0f});
    panel.setStyleId("panel_default");
    panel.setBorderEnabled(false);

    MockRenderer renderer;
    panel.render(renderer);

    // First draw call is the background fill — must reflect panel_default's
    // (0.18, 0.18, 0.20, 1), NOT the hardcoded (0.20, 0.20, 0.20, 1) fallback.
    CHECK(renderer.getDrawCalls().size() >= 1u);
    const auto& fill = renderer.getDrawCalls().front();
    CHECK(fill.type == MockRenderer::DrawCall::Rect);
    CHECK_FLOAT_EQ(fill.color.x, 0.18f, 1e-5f);
    CHECK_FLOAT_EQ(fill.color.y, 0.18f, 1e-5f);
    CHECK_FLOAT_EQ(fill.color.z, 0.20f, 1e-5f);

    StyleManager::get().setStyleSheet(nullptr);
}

TEST_SUITE_END