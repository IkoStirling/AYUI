#include "AYTest.h"
#include "AYUI/MenuBar.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuItem.h"
#include "AYUI/Button.h"
#include "AYUI/UIManager.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
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

TEST_CASE(menubar_respects_parent_assigned_height) {
    MenuBar bar;
    bar.setSize(FVector2(240.0f, 22.0f));
    bar.addMenu(L"File");
    bar.addMenu(L"Edit");
    bar.performLayout();

    CHECK(bar.getSize().y == 22.0f);
    for (Widget* child : bar.getChildren()) {
        if (auto* anchor = dynamic_cast<Button*>(child)) {
            CHECK(anchor->getSize().y == 22.0f);
            CHECK(anchor->getWorldBounds().maxY
                  <= bar.getWorldBounds().maxY);
        }
    }
}

// PR-S1c (Gallery Menu): click-outside closes a menu via UIManager →
// Menu::close() without updating MenuBar::_openIdx. The next anchor
// click must still reopen it (previously the stale _openIdx hit the
// toggle-close branch and swallowed the click).
TEST_CASE(menubar_anchor_reopens_after_external_close) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    MenuBar bar;
    bar.setSize(FVector2(200.0f, 26.0f));
    bar.addMenu(L"File");
    bar.performLayout();
    Menu* file = bar.getMenu(0);
    file->addItem(L"New");

    // Locate the anchor Button — MenuBar addChildExternal's the menu
    // first, then addChild's the anchor button; walk children for the
    // first Button.
    Button* anchor = nullptr;
    for (Widget* w : bar.getChildren()) {
        anchor = dynamic_cast<Button*>(w);
        if (anchor != nullptr) break;
    }
    CHECK_NOT_NULL(anchor);
    if (anchor == nullptr) {
        ui.shutdown();
        return;
    }
    const FRectangle a = anchor->getWorldBounds();
    const FVector2 pos((a.minX + a.maxX) * 0.5f, (a.minY + a.maxY) * 0.5f);

    // 1) Click anchor → menu opens (anchor wiring path).
    anchor->onMouseButtonDown(UIMouseEvent(pos, 0));
    anchor->onMouseButtonUp(UIMouseEvent(pos, 0));
    CHECK(file->isOpen());
    CHECK(bar.getOpenMenuIndex() == 0);

    // 2) External close (what click-outside does). Menu::close() never
    //    tells MenuBar → _openIdx stays stale.
    file->close();
    CHECK_FALSE(file->isOpen());
    CHECK(bar.getOpenMenuIndex() == 0);   // stale, pre-fix state

    // 3) Click anchor again → menu must reopen.
    anchor->onMouseButtonDown(UIMouseEvent(pos, 0));
    anchor->onMouseButtonUp(UIMouseEvent(pos, 0));
    CHECK(file->isOpen());

    // 4) Clicking the SAME anchor while open still toggles closed.
    anchor->onMouseButtonDown(UIMouseEvent(pos, 0));
    anchor->onMouseButtonUp(UIMouseEvent(pos, 0));
    CHECK_FALSE(file->isOpen());

    ui.shutdown();
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
