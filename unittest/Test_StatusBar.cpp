#include "AYTest.h"
#include "AYUI/StatusBar.h"
#include "AYUI/TextLabel.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_StatusBar)

TEST_CASE(statusbar_initial_state) {
    StatusBar sb;
    CHECK(sb.getPanelCount() == 0u);
}

TEST_CASE(statusbar_add_panels) {
    StatusBar sb;
    auto* p1 = sb.addPanel(L"Ready");
    auto* p2 = sb.addPanel(L"Ln 1, Col 1");
    auto* p3 = sb.addPanel(L"UTF-8");

    CHECK(sb.getPanelCount() == 3u);
    CHECK(sb.getPanel(0) == p1);
    CHECK(sb.getPanel(1) == p2);
    CHECK(sb.getPanel(2) == p3);
    CHECK(p1->getText() == L"Ready");
}

TEST_CASE(statusbar_update_panel_text) {
    StatusBar sb;
    sb.addPanel(L"Line 1");
    sb.setPanelText(0, L"Line 42");
    CHECK(sb.getPanel(0)->getText() == L"Line 42");
}

TEST_CASE(statusbar_render) {
    StatusBar sb;
    sb.setSize(FVector2(400.0f, 22.0f));
    sb.addPanel(L"Ready");
    sb.setPosition(FVector2(0.0f, 0.0f));
    sb.performLayout();

    MockRenderer renderer;
    sb.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    CHECK(rectCount >= 1);
}

TEST_CASE(statusbar_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("StatusBar"));

    Widget* widget = factory.create("StatusBar");
    CHECK_NOT_NULL(widget);
    StatusBar* original = dynamic_cast<StatusBar*>(widget);
    CHECK_NOT_NULL(original);
    original->setId("status_main");
    original->addPanel(L"Ready");
    original->addPanel(L"Line 1");

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"StatusBar\"") != std::string::npos);
    CHECK(json.find("Ready") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    StatusBar* restoredSb = dynamic_cast<StatusBar*>(restored);
    CHECK_NOT_NULL(restoredSb);
    CHECK(restoredSb->getId() == "status_main");
    CHECK(restoredSb->getPanelCount() == 2u);
    CHECK(restoredSb->getPanel(0)->getText() == L"Ready");
    CHECK(restoredSb->getPanel(1)->getText() == L"Line 1");

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

TEST_SUITE_END

