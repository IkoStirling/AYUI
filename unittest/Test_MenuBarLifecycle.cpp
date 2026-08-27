#include "AYTest.h"
#include "AYUI/MenuBar.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuItem.h"
#include "AYUI/Button.h"
#include "AYUI/UIManager.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Widget.h"
#include <cstdio>

// =============================================================================
// AYUI-Audit-2026-08-26 Bug2: MenuBar::~MenuBar had a use-after-free.
//
// Background: addMenu() registers each Menu via addChildExternal(), so
// every Menu* lives in this MenuBar's `_children` list. ~MenuBar runs a
// `delete e.menu` loop, but then ~Widget walks `_children` writing
// `child->_parent = nullptr` through the now-freed pointers — classic
// UAF. The fix in Controls/AYMenuBar.cpp removes the deleted menus
// from `_children` BEFORE the delete loop.
//
// These tests build a MenuBar with several Menus, attach them to a
// host Window (the production path), destroy the host, and assert no
// crash. Under /fsanitize=address the UAF in the base-dtor child walk
// would surface as a heap-use-after-free; without ASan we at minimum
// catch the visible SEGV when the freed memory has been reused.
// =============================================================================

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_MenuBarLifecycle)

// MenuBar with 3 menus, attached as a child of a stack-scoped Window.
// Pre-fix this UAF'd inside ~MenuBar's `delete e.menu` loop on the
// next ~Widget child-walk.
TEST_CASE(menubar_with_three_menus_destroy_no_crash) {
    MenuBar bar;
    bar.setSize(FVector2(300.0f, 26.0f));

    Menu* fileMenu   = bar.addMenu(L"File");
    Menu* editMenu   = bar.addMenu(L"Edit");
    Menu* viewMenu   = bar.addMenu(L"View");
    CHECK_NOT_NULL(fileMenu);
    CHECK_NOT_NULL(editMenu);
    CHECK_NOT_NULL(viewMenu);
    CHECK(bar.getMenuCount() == 3u);

    fileMenu->addItem(L"New");
    fileMenu->addItem(L"Open");
    editMenu->addItem(L"Cut");
    editMenu->addItem(L"Copy");
    viewMenu->addItem(L"Zoom In");

    // bar is stack-scoped; its dtor fires at end of scope and must
    // not UAF when clearing the externally-owned Menu* entries from
    // its `_children` list.
}

// MenuBar attached under a UIManager-driven Window host. Mirrors the
// editor shell layout where the MenuBar lives inside a Window and
// both go down together. Pre-fix: ~MenuBar ran the delete loop, then
// ~Widget walked _children writing into freed pointers.
TEST_CASE(menubar_under_window_destroy_no_crash) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    Window* host = new Window();
    host->setSize(FVector2(400.0f, 300.0f));

    MenuBar* bar = new MenuBar();
    bar->setSize(FVector2(380.0f, 26.0f));
    host->addChild(bar);

    Menu* file = bar->addMenu(L"File");
    Menu* edit = bar->addMenu(L"Edit");
    Menu* help = bar->addMenu(L"Help");
    CHECK_NOT_NULL(file);
    CHECK_NOT_NULL(edit);
    CHECK_NOT_NULL(help);

    file->addItem(L"Open");
    file->addItem(L"Save");
    edit->addItem(L"Cut");
    help->addItem(L"About");

    // Tear the whole tree down. Pre-fix the host's destroyWidgetTree
    // chain triggered ~MenuBar's UAF on the way to ~Widget's child walk.
    destroyWidgetTree(host);

    ui.shutdown();
}

// Repeated build/teardown cycle to surface any "delete twice" pathology
// from a non-idempotent dtor chain. Catches edge cases where one Menu's
// external pointer slot is reused across iterations.
TEST_CASE(menubar_repeated_build_destroy_no_crash) {
    for (int i = 0; i < 4; ++i) {
        MenuBar bar;
        bar.setSize(FVector2(200.0f, 24.0f));
        Menu* m1 = bar.addMenu(L"File");
        Menu* m2 = bar.addMenu(L"Edit");
        m1->addItem(L"Open");
        m2->addItem(L"Cut");
        // ~MenuBar at end of scope must reclaim each Menu* cleanly.
    }
}

TEST_SUITE_END
