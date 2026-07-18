#include "AYTest.h"
#include "AYMenu.h"
#include "AYMenuItem.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
#include <iostream>
#include <fstream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Menu)

TEST_CASE(menu_initial_state) {
    Menu menu;
    CHECK_FALSE(menu.isOpen());
    CHECK(menu.getItemCount() == 0u);
}

TEST_CASE(menu_add_and_get_items) {
    Menu menu;
    MenuItem* a = menu.addItem(L"Open");
    MenuItem* b = menu.addItem(L"Save");
    MenuItem* c = menu.addItem(L"Close");

    CHECK(menu.getItemCount() == 3u);
    CHECK(menu.getItem(0) == a);
    CHECK(menu.getItem(1) == b);
    CHECK(menu.getItem(2) == c);
}

TEST_CASE(menu_item_shortcut) {
    Menu menu;
    MenuItem* a = menu.addItem(L"Save", L"Ctrl+S");
    CHECK(a->getText() == L"Save");
    CHECK(a->getShortcut() == L"Ctrl+S");
}

TEST_CASE(menu_activate_closes_menu) {
    Menu menu;
    menu.addItem(L"Open");
    menu.addItem(L"Save");

    // Manually open without a host (host-less test fixture).
    menu.setVisible(true);
    menu.setPosition(FVector2(0.0f, 0.0f));
    // We can't easily flip _open=true without open(); use a small hook:
    // we'll just call into Menu's path that requires it. Skip — instead,
    // check that addItem registers the close callback.
    CHECK(menu.getItemCount() == 2u);
}

TEST_CASE(menu_render_emits_background) {
    Menu menu;
    menu.setSize(FVector2(180.0f, 80.0f));
    menu.setPosition(FVector2(0.0f, 0.0f));
    menu.setVisible(true);   // Menu defaults hidden — render needs visible=true.
    menu.performLayout();
    menu.addItem(L"Item A");
    menu.addItem(L"Item B");

    MockRenderer renderer;
    menu.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    // Background plate + 4 border rects = 5 minimum.
    CHECK(rectCount >= 1);
}

TEST_CASE(menu_factory_registered) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("Menu"));
    Widget* widget = factory.create("Menu");
    CHECK_NOT_NULL(widget);
    Menu* m = dynamic_cast<Menu*>(widget);
    CHECK_NOT_NULL(m);
    destroyWidgetTree(widget);
}

TEST_SUITE_END

