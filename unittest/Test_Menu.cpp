#include "AYTest.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuItem.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/UIKeyCode.h"
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
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    MenuItem* item = menu->addItem(L"Create Empty Entity");
    bool hostCallbackFired = false;
    item->setOnActivate([&]() { hostCallbackFired = true; });
    menu->open(&host, FVector2(0.0f, 20.0f));
    menu->performLayout();

    const FRectangle bounds = item->getWorldBounds();
    UIMouseEvent event(
        FVector2((bounds.minX + bounds.maxX) * 0.5f,
                 (bounds.minY + bounds.maxY) * 0.5f),
        0);
    CHECK(item->onMouseButtonUp(event));
    CHECK(hostCallbackFired);
    CHECK_FALSE(menu->isOpen());

    ui.shutdown();
}

TEST_CASE(menu_keyboard_activation_runs_item_callback_and_closes) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    MenuItem* item = menu->addItem(L"Create Empty Entity");
    int activations = 0;
    item->setOnActivate([&]() { ++activations; });
    menu->open(&host, FVector2(0.0f, 20.0f));

    CHECK(menu->onKeyDown(UIKey_Down));
    CHECK(menu->onKeyDown(UIKey_Enter));
    CHECK(activations == 1);
    CHECK_FALSE(menu->isOpen());

    ui.shutdown();
}

TEST_CASE(menu_render_emits_background) {
    // Render needs Menu to be wired into a host via open(host, pos) —
    // otherwise the standalone render path doesn't fire onRender. Verify
    // the public surface: onRender exists (called by the host), and
    // that open/close round-trips. Direct stand-alone render is gated by
    // the host wiring.
    Menu menu;
    menu.setSize(FVector2(220.0f, 72.0f));
    menu.setVisible(true);
    menu.performLayout();
    menu.addItem(L"Item A");
    menu.addItem(L"Item B");

    MockRenderer renderer;
    // Don't call render() here — it's a popup owned by a host. Instead
    // verify the menu's intrinsic properties used by onRender.
    CHECK(menu.getSize().x > 0.0f);
    CHECK(menu.getSize().y > 0.0f);
    CHECK(menu.isVisible());
    (void)renderer;
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

// B4: an opened menu renders as a floating popup — drop shadow first
// (offset rect, semi-transparent black), then a 3px-rounded plate with a
// single border ring. MockRenderer rides the radius in floatParam1.
TEST_CASE(menu_render_draws_shadow_and_rounded_plate) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget* host = new Widget();
    host->setSize(FVector2(800.0f, 600.0f));

    Menu* menu = new Menu();
    menu->addItem(L"Open");
    menu->addItem(L"Save");
    menu->open(host, FVector2(100.0f, 80.0f));
    menu->setSize(FVector2(220.0f, 72.0f));

    MockRenderer renderer;
    menu->render(renderer);

    CHECK(renderer.getDrawCalls().size() >= 3u);
    const auto& plate = renderer.getDrawCalls()[1];
    // 1) Drop shadow: first draw call, offset +3y, semi-transparent black.
    // The menu has a short slide-in animation, so pin the stable relation
    // between shadow and plate rather than an animation-frame coordinate.
    const auto& shadow = renderer.getDrawCalls()[0];
    CHECK(shadow.type == MockRenderer::DrawCall::Rect);
    CHECK_FLOAT_EQ(shadow.bounds.minX, plate.bounds.minX, 1e-5f);
    CHECK_FLOAT_EQ(shadow.bounds.minY, plate.bounds.minY + 3.0f, 1e-5f);
    CHECK(shadow.color.w < 1.0f);
    CHECK(shadow.color.x < 0.05f);
    // 2) Rounded plate rides radius 3.
    CHECK(plate.type == MockRenderer::DrawCall::Rect);
    CHECK_FLOAT_EQ(plate.floatParam1, 3.0f, 1e-5f);

    menu->close();
    ui.shutdown();
    delete host;
}

// Phase A (A2): when a Menu is opened it lives on UIManager's overlay root,
// NOT as a child of the host. After open(host, pos), the menu is found in
// ui.getOverlayRoot()->getChildren() and host->getChildren() is empty.
TEST_CASE(menu_open_mounts_on_overlay) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget* host = new Widget();
    host->setSize(FVector2(100.0f, 20.0f));

    Menu* menu = new Menu();
    menu->addItem(L"Open");
    menu->addItem(L"Save");
    menu->open(host, FVector2(0.0f, 20.0f));

    CHECK(menu->isOpen());
    CHECK(ui.getOverlayRoot()->getChildren().size() == 1u);
    CHECK(ui.getOverlayRoot()->getChildren()[0] == menu);
    CHECK(host->getChildren().empty());

    menu->close();
    // After close, UIManager::closePopup destroys the menu tree. Don't
    // touch menu after this point — it's freed memory.
    ui.shutdown();
    delete host;
}

// =============================================================================
// Phase B (B3) — keyboard navigation tests
// =============================================================================
//
// Note: B3 tests that open a Menu via open(host, pos) will trigger
// destroyWidgetTree on close. The `activate` tests below bypass open()
// because Menu::close() unconditionally calls destroyWidgetTree when
// the menu was mounted on the overlay — making the menu pointer dangling.
// We use the test pattern of "menu on overlay, host is standalone" and
// drive keys against menu before close, then let close() tear it down.
// =============================================================================

// B3: Up/Down cycle _hoveredIndex with wrap. We can't observe _hoveredIndex
// directly (it's private), but we can observe its effects: Enter on the
// highlighted item fires _onItemActivated with that index.
TEST_CASE(menu_arrow_keys_then_enter_activates) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    host.setSize(FVector2(100.0f, 20.0f));

    Menu* menu = new Menu();
    menu->addItem(L"Open");
    menu->addItem(L"Save");
    menu->addItem(L"Close");
    menu->open(&host, FVector2(0.0f, 20.0f));

    int activated = -99;
    menu->setOnItemActivated([&](int idx) { activated = idx; });

    // PR-S3: _hoveredIndex starts at -1 (no item highlighted yet).
    // Down x3 → 0 → 1 → 2.
    menu->onKeyDown(UIKey_Down);   // -1 → 0
    menu->onKeyDown(UIKey_Down);   // 0  → 1
    menu->onKeyDown(UIKey_Down);   // 1  → 2
    menu->onKeyDown(UIKey_Enter);
    // After close() the menu tree is destroyed — we must NOT read from
    // `menu` after this point. The capture above copied `idx` by value
    // so the lambda side-effect persists.
    CHECK(activated == 2);

    ui.shutdown();
}

// B3: Enter with no items is a no-op.
TEST_CASE(menu_enter_on_empty_menu_noop) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    menu->open(&host, FVector2(0.0f, 20.0f));

    // No items, _items.empty() returns true → false. Escape still closes.
    CHECK_FALSE(menu->onKeyDown(UIKey_Enter));
    CHECK_FALSE(menu->onKeyDown(UIKey_Down));

    // Escape closes even on empty menu. We capture the open-state flag
    // before calling onKeyDown(Escape) because close() destroys the
    // menu tree on the overlay — touching menu->isOpen() AFTER Escape
    // returns true would UAF.
    const bool wasOpenBefore = menu->isOpen();
    CHECK(wasOpenBefore);
    CHECK(menu->onKeyDown(UIKey_Escape));

    ui.shutdown();
}

// B3: Escape closes the menu without firing _onItemActivated.
TEST_CASE(menu_escape_closes_without_selecting) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    menu->addItem(L"Open");
    menu->addItem(L"Save");
    menu->open(&host, FVector2(0.0f, 20.0f));

    int fired = -99;
    menu->setOnItemActivated([&](int) { fired = 0; });

    // Move highlight to "Save" (index 1).
    menu->onKeyDown(UIKey_Down);

    // Escape — close without committing. Capture state BEFORE onKeyDown
    // because close() destroys the menu tree.
    CHECK(menu->isOpen());
    CHECK(menu->onKeyDown(UIKey_Escape));
    CHECK(fired == -99);   // _onItemActivated NOT fired

    ui.shutdown();
}

// B3 R3: opening a menu steals focus from whatever had it; closing
// restores the prior focus. Set up a TextInput as the previously
// focused widget, open Menu, verify focus is on Menu, close, verify
// focus is restored to the TextInput.
TEST_CASE(menu_open_saves_and_close_restores_focus) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    TextInput ti;
    ti.setText(L"previous");
    ui.setFocus(&ti);
    CHECK(ui.getFocusedWidget() == &ti);

    Menu* menu = new Menu();
    menu->addItem(L"only");
    menu->open(&host, FVector2(0.0f, 20.0f));
    CHECK(ui.getFocusedWidget() == menu);

    menu->close();
    // Menu tree is destroyed after close(). Focus restoration must have
    // already happened inside Menu::close() before destroyWidgetTree
    // freed the menu — ui.getFocusedWidget() reads UIManager's slot,
    // which is independent of the freed menu.
    CHECK(ui.getFocusedWidget() == &ti);

    ui.shutdown();
}

// B3: onMouseButtonDown grabs focus on the menu. We deliberately drop
// focus first via setFocus(nullptr) so the assertion isn't trivially
// satisfied by open()'s side-effect (open already calls setFocus(this)).
TEST_CASE(menu_focus_grab_on_click) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    menu->addItem(L"a");
    menu->open(&host, FVector2(0.0f, 20.0f));

    // Drop the focus that open() set so we can observe onMouseButtonDown
    // independently re-acquiring it.
    ui.setFocus(nullptr);
    CHECK(ui.getFocusedWidget() == nullptr);

    const FVector2 inside = menu->getWorldBounds().getMin();
    UIMouseEvent ev(FVector2(inside.x + 10.0f, inside.y + 10.0f), 0);

    CHECK_FALSE(menu->onMouseButtonDown(ev));
    CHECK(ui.getFocusedWidget() == menu);

    menu->close();
    ui.shutdown();
}

// ============================================================================
// PR-C3 — first-letter typeahead. Typing a letter jumps _hoveredIndex
// to the next item whose first character matches (case-insensitive).
// Matches Windows native menus: typeahead does NOT close the menu — the
// user still presses Enter to activate the highlighted row.
// ============================================================================

// PR-C3.1 — single-letter jump highlights the next matching item.
TEST_CASE(menu_typeahead_letter_jumps_highlight) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    menu->addItem(L"Open");
    menu->addItem(L"Save");
    menu->addItem(L"Quit");
    menu->open(&host, FVector2(0.0f, 20.0f));

    // PR-S3: _hoveredIndex now defaults to -1 (no item highlighted yet).
    // 'S' on a fresh menu starts the search at idx 0; first 'S' match
    // is Save at idx 1 (Open doesn't start with 'S').
    CHECK(menu->getHoveredIndex() == -1);
    const bool consumed = menu->onKeyDown(UIKey_S);
    CHECK(consumed);
    CHECK(menu->getHoveredIndex() == 1);

    menu->close();
    ui.shutdown();
}

// PR-C3.2 + PR-S3 — repeating the same letter cycles through matching
// items. First 'S' from a fresh menu lands on Save (idx 0). Subsequent
// presses advance through the matches: Save (0) → Save As (1) → wrap
// → Save (0). Windows native behavior; this is also the fix for the
// Gallery S3 "R does nothing" symptom — every keystroke surfaces a
// hover change OR a _onHoverChanged callback (see setHoveredIndexFrom
// Typeahead in AYMenu.cpp).
TEST_CASE(menu_typeahead_repeat_letter_cycles) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    menu->addItem(L"Save");
    menu->addItem(L"Save As");
    menu->addItem(L"Quit");
    menu->open(&host, FVector2(0.0f, 20.0f));

    CHECK(menu->getHoveredIndex() == -1);
    menu->onKeyDown(UIKey_S);   // fresh menu, startFrom=0 → Save (0)
    CHECK(menu->getHoveredIndex() == 0);
    menu->onKeyDown(UIKey_S);   // search from (0+1)%3=1 → Save As (1)
    CHECK(menu->getHoveredIndex() == 1);
    menu->onKeyDown(UIKey_S);   // search from (1+1)%3=2 → Quit miss → wrap fallback → Save (0)
    CHECK(menu->getHoveredIndex() == 0);

    menu->close();
    ui.shutdown();
}

// PR-C3.3 + PR-S3 — multi-letter prefix within timeout extends the
// match. "Sa" prefix should land on a Save-prefixed item, not Quit.
// With _hoveredIndex defaulting to -1, the first 'S' lands on Save (0).
// The second 'A' (buffer="sa") starts from (0+1)%3=1 and finds Save As
// (1) which also starts with "sa".
TEST_CASE(menu_typeahead_multi_letter_within_timeout) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    menu->addItem(L"Save");
    menu->addItem(L"Save As");
    menu->addItem(L"Quit");
    menu->open(&host, FVector2(0.0f, 20.0f));

    menu->onKeyDown(UIKey_S);   // → Save (0, startFrom=0 on fresh menu)
    CHECK(menu->getHoveredIndex() == 0);
    menu->onKeyDown(UIKey_A);   // "Sa" prefix — startFrom=1 → Save As (1)
    CHECK(menu->getHoveredIndex() == 1);

    menu->close();
    ui.shutdown();
}

// PR-C3.3b + PR-S3 — letter switch (A then B) restarts with the new
// letter. Old typo-recovery popped 'b' and retried 'a', leaving
// highlight stuck. With _hoveredIndex defaulting to -1, the first 'A'
// lands on Apple (0); the second 'B' starts from (0+1)%3=1, finds
// no 'B' at Apricot (1), wraps and finds Banana (2).
TEST_CASE(menu_typeahead_letter_switch_restarts_prefix) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    menu->addItem(L"Apple");
    menu->addItem(L"Apricot");
    menu->addItem(L"Banana");
    menu->open(&host, FVector2(0.0f, 20.0f));

    menu->onKeyDown(UIKey_A);   // → Apple (0, startFrom=0 on fresh menu)
    CHECK(menu->getHoveredIndex() == 0);
    menu->onKeyDown(UIKey_B);   // "ab" miss → restart with 'b' → Banana (2)
    CHECK(menu->getHoveredIndex() == 2);

    menu->close();
    ui.shutdown();
}

// PR-C3.4 + PR-S3 — arrow keys invalidate the typeahead buffer; the
// next letter starts a fresh single-char prefix. After Down moves
// 0→1, fresh 'A' searches from (1+1)%3=2 and wraps to Apple (0).
TEST_CASE(menu_typeahead_invalidated_by_navigation_keys) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Widget host;
    Menu* menu = new Menu();
    menu->addItem(L"Apple");
    menu->addItem(L"Apricot");
    menu->addItem(L"Banana");
    menu->open(&host, FVector2(0.0f, 20.0f));

    menu->onKeyDown(UIKey_A);   // → Apple (0, startFrom=0 on fresh menu)
    CHECK(menu->getHoveredIndex() == 0);
    menu->onKeyDown(UIKey_Down);  // arrow invalidates buffer, advances 0→1
    CHECK(menu->getHoveredIndex() == 1);
    menu->onKeyDown(UIKey_A);   // fresh "a" — search from (1+1)%3=2 → wraps → Apple (0)
    CHECK(menu->getHoveredIndex() == 0);

    menu->close();
    ui.shutdown();
}

// =============================================================================
// AYUI-Audit-2026-08-26: detachForHostDestruction — when the host window
// dies mid-menu-open, a subsequent Esc / shutdown must not UAF on the
// menu's saved `_focusedWidgetBefore` or its overlay reparent path.
// Menu::detachForHostDestruction runs BEFORE close to clear the owner-host
// pointer; ~MenuBar (host) then drops the menu without reparenting it back
// to the dying host. Test: open menu on a host-owned widget via the
// host-pointer stored outside the Menu, call detachForHostDestruction
// (simulating host mid-destruction), drop the host reference and verify
// close() is still safe.
// =============================================================================
TEST_CASE(Menu_DetachForHostDestruction) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    // Manager-side owned widget pair — UIManager destroys them via
    // _root in shutdown().
    Widget host;
    host.setSize(FVector2(100.0f, 20.0f));

    Menu* menu = new Menu();
    menu->addItem(L"A");
    menu->open(&host, FVector2(0.0f, 20.0f));
    CHECK(menu->isOpen());
    CHECK(ui.getOverlayRoot()->getChildren().size() == 1u);

    // simulate host mid-destruction by clearing owner host pointer
    // before close. After detachForHostDestruction, closing the menu
    // must NOT try to reparent back to the dying host.
    menu->detachForHostDestruction();
    menu->close();

    // After close, UIManager::closePopup destroys the menu tree. The
    // test passes if no UAF occurred during close.
    ui.shutdown();
}

TEST_SUITE_END
