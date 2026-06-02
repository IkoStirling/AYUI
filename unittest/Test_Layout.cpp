#include "AYTest.h"
#include "AYMathUtils.h"
#include "AYBox.h"
#include "AYMockRenderer.h"
#include "AYWindow.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Layout)

TEST_CASE(test_vbox_layout) {
    VBox* vbox = new VBox();
    vbox->setSize(FVector2(200.0f, 100.0f));
    vbox->setSpacing(4.0f);

    Widget* child1 = new Widget();
    Widget* child2 = new Widget();
    Widget* child3 = new Widget();

    vbox->addWidget(child1, 20.0f);
    vbox->addWidget(child2, 20.0f);
    vbox->addWidget(child3, 0.0f); // Fill remaining

    vbox->performLayout();

    FRectangle c1Bounds = child1->getWorldBounds();
    CHECK_FLOAT_EQ(c1Bounds.minY, 4.0f, 1e-5f);
    CHECK_FLOAT_EQ(c1Bounds.maxY - c1Bounds.minY, 20.0f, 1e-5f);

    FRectangle c2Bounds = child2->getWorldBounds();
    CHECK_FLOAT_EQ(c2Bounds.minY, 28.0f, 1e-5f);

    FRectangle c3Bounds = child3->getWorldBounds();
    CHECK_FLOAT_EQ(c3Bounds.minY, 52.0f, 1e-5f);

    delete vbox;
}

TEST_CASE(test_hbox_layout) {
    HBox* hbox = new HBox();
    hbox->setSize(FVector2(200.0f, 50.0f));
    hbox->setSpacing(4.0f);

    Widget* child1 = new Widget();
    Widget* child2 = new Widget();
    Widget* child3 = new Widget();

    hbox->addWidget(child1, 50.0f);
    hbox->addWidget(child2, 50.0f);
    hbox->addWidget(child3, 0.0f); // Fill remaining

    hbox->performLayout();

    FRectangle c1Bounds = child1->getWorldBounds();
    CHECK_FLOAT_EQ(c1Bounds.minX, 4.0f, 1e-5f);
    CHECK_FLOAT_EQ(c1Bounds.maxX - c1Bounds.minX, 50.0f, 1e-5f);

    FRectangle c2Bounds = child2->getWorldBounds();
    CHECK_FLOAT_EQ(c2Bounds.minX, 58.0f, 1e-5f);

    FRectangle c3Bounds = child3->getWorldBounds();
    CHECK_FLOAT_EQ(c3Bounds.minX, 112.0f, 1e-5f);

    delete hbox;
}

TEST_CASE(test_window_hit_test) {
    Window* window = new Window();
    window->setPosition(FVector2(100.0f, 100.0f));
    window->setSize(FVector2(400.0f, 300.0f));
    window->setTitleBarHeight(28.0f);

    Widget* hit = window->hitTest(FVector2(300.0f, 110.0f));
    CHECK(hit == window);

    hit = window->hitTest(FVector2(300.0f, 200.0f));
    CHECK(hit == window);

    hit = window->hitTest(FVector2(50.0f, 50.0f));
    CHECK(hit == nullptr);

    delete window;
}

TEST_CASE(test_mock_renderer) {
    MockRenderer renderer;
    renderer.clear();

    Widget* root = new Widget();
    root->setPosition(FVector2(0.0f, 0.0f));
    root->setSize(FVector2(100.0f, 100.0f));

    Widget* child = new Widget();
    child->setPosition(FVector2(10.0f, 10.0f));
    child->setSize(FVector2(50.0f, 50.0f));
    root->addChild(child);

    root->setRenderBackend(&renderer);
    root->render(renderer);

    auto calls = renderer.getDrawCalls();
    // Plain Widget has empty onRender, so no draw calls generated
    CHECK(calls.size() == 0);

    delete root;
}

TEST_SUITE_END

