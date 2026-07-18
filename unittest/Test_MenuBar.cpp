#include "AYTest.h"
#include "AYMenuBar.h"
#include "AYMenu.h"
#include "AYMenuItem.h"
#include "AYMockRenderer.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_MenuBar)

TEST_CASE(menubar_initial_state) {
    MenuBar bar;
    CHECK(bar.getMenuCount() == 0u);
    CHECK(bar.getOpenMenuIndex() == -1);
}

TEST_CASE(menubar_add_menus) {
    MenuBar bar;
    Menu* file = bar.addMenu(L"File");
    Menu* edit = bar.addMenu(L"Edit");

    CHECK(bar.getMenuCount() == 2u);
    CHECK(bar.getMenu(0) == file);
    CHECK(bar.getMenu(1) == edit);
    CHECK(bar.getMenuTitle(0) == L"File");
    CHECK(bar.getMenuTitle(1) == L"Edit");
}

TEST_CASE(menubar_menu_can_hold_items) {
    MenuBar bar;
    Menu* file = bar.addMenu(L"File");
    file->addItem(L"New");
    file->addItem(L"Open");
    file->addItem(L"Save");
    CHECK(file->getItemCount() == 3u);
}

TEST_CASE(menubar_close_open) {
    MenuBar bar;
    bar.addMenu(L"File");
    bar.closeOpenMenu();
    CHECK(bar.getOpenMenuIndex() == -1);
}

TEST_CASE(menubar_render) {
    MenuBar bar;
    bar.setSize(FVector2(400.0f, 26.0f));
    bar.addMenu(L"File");
    bar.addMenu(L"Edit");
    bar.setPosition(FVector2(0.0f, 0.0f));
    bar.performLayout();

    MockRenderer renderer;
    bar.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    CHECK(rectCount >= 1);   // at least background + border
}

TEST_CASE(menubar_factory_registered) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("MenuBar"));
    Widget* widget = factory.create("MenuBar");
    CHECK_NOT_NULL(widget);
    MenuBar* mb = dynamic_cast<MenuBar*>(widget);
    CHECK_NOT_NULL(mb);
    destroyWidgetTree(widget);
}

TEST_SUITE_END

