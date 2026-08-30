#include "AYTest.h"
#include "AYUI/Button.h"
#include "AYUI/CheckBox.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuItem.h"
#include "AYUI/Style.h"
#include "AYUI/UIManager.h"
#include "AYUI/MockRenderer.h"

#include <fstream>
#include <iterator>

using namespace ayt::ui;
using namespace ayt::math;

// UI animation lane (cut 1): InteractiveWidget state colors transition
// through the 90ms default tween instead of swapping instantly. DrawCall
// colors are asserted on the fallback (unstyled) paths; the styled path
// is state-blind and must NOT transition.
TEST_SUITE(AYUI_ColorAnimation)

// Fallback fill (0.28,0.28,0.30) → hover (0.36,0.38,0.42), 90ms EaseOut.
// First frame after hover keeps the old color (tween started, not yet
// advanced); mid-flight the channels sit strictly between; completion
// lands on the target.
TEST_CASE(button_hover_color_transitions) {
    Button button;
    button.setSize(FVector2(100.0f, 32.0f));

    // Prime: first render snaps to the initial (normal) color.
    MockRenderer r0;
    button.render(r0);
    CHECK(r0.getDrawCalls().size() >= 1u);
    CHECK_FLOAT_EQ(r0.getDrawCalls()[0].color.x, 0.28f, 1e-5f);
    CHECK_FLOAT_EQ(r0.getDrawCalls()[0].color.y, 0.28f, 1e-5f);
    CHECK_FLOAT_EQ(r0.getDrawCalls()[0].color.z, 0.30f, 1e-5f);

    button.onMouseMove(UIMouseEvent(FVector2(50.0f, 16.0f), 0));

    // Tween started but zero ticks elapsed → still the old color.
    MockRenderer r1;
    button.render(r1);
    CHECK_FLOAT_EQ(r1.getDrawCalls()[0].color.x, 0.28f, 1e-5f);

    // Half duration (45ms → ease 0.75): strictly between old and new.
    button.tick(0.045f);
    MockRenderer r2;
    button.render(r2);
    const float cx = r2.getDrawCalls()[0].color.x;
    CHECK(cx > 0.28f && cx < 0.36f);

    // Full duration → target.
    button.tick(0.045f);
    MockRenderer r3;
    button.render(r3);
    CHECK_FLOAT_EQ(r3.getDrawCalls()[0].color.x, 0.36f, 1e-5f);
    CHECK_FLOAT_EQ(r3.getDrawCalls()[0].color.y, 0.38f, 1e-5f);
    CHECK_FLOAT_EQ(r3.getDrawCalls()[0].color.z, 0.42f, 1e-5f);
}

// setColorTweenMs(0) restores the instant swap.
TEST_CASE(button_color_tween_disabled_snaps) {
    Button button;
    button.setSize(FVector2(100.0f, 32.0f));
    button.setColorTweenMs(0.0f);

    MockRenderer r0;
    button.render(r0);
    CHECK_FLOAT_EQ(r0.getDrawCalls()[0].color.x, 0.28f, 1e-5f);

    button.onMouseMove(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    MockRenderer r1;
    button.render(r1);
    CHECK_FLOAT_EQ(r1.getDrawCalls()[0].color.x, 0.36f, 1e-5f);  // instant
}

// Pressing mid-flight retargets the tween from the current interpolated
// value toward the pressed color — no jump, no snap to either endpoint.
TEST_CASE(button_state_change_midflight_retargets) {
    Button button;
    button.setSize(FVector2(100.0f, 32.0f));
    button.setText(L"OK");

    MockRenderer r0;
    button.render(r0);                            // prime → normal

    button.onMouseMove(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    MockRenderer r1;
    button.render(r1);                            // start hover tween
    CHECK_FLOAT_EQ(r1.getDrawCalls()[0].color.x, 0.28f, 1e-5f);

    button.tick(0.045f);                          // ≈0.34 along hover

    // Press: target flips to (0.18,0.45,0.78). The draw right after
    // returns the current interpolated color — retarget, not snap.
    UIMouseEvent down(FVector2(50.0f, 16.0f), 0);
    CHECK(button.onMouseButtonDown(down));
    MockRenderer r2;
    button.render(r2);
    CHECK_FLOAT_EQ(r2.getDrawCalls()[0].color.x, 0.34f, 1e-4f);

    // Full duration from retarget → pressed color.
    button.tick(0.09f);
    MockRenderer r3;
    button.render(r3);
    CHECK_FLOAT_EQ(r3.getDrawCalls()[0].color.x, 0.18f, 1e-5f);
    CHECK_FLOAT_EQ(r3.getDrawCalls()[0].color.y, 0.45f, 1e-5f);
    CHECK_FLOAT_EQ(r3.getDrawCalls()[0].color.z, 0.78f, 1e-5f);
}

// CheckBox box fill (0.22,0.22,0.24) → hover (0.28,0.28,0.30), 90ms.
TEST_CASE(checkbox_box_bg_transitions_on_hover) {
    CheckBox cb;
    cb.setSize(FVector2(80.0f, 24.0f));

    MockRenderer r0;
    cb.render(r0);
    CHECK(r0.getDrawCalls().size() >= 1u);
    CHECK_FLOAT_EQ(r0.getDrawCalls()[0].color.x, 0.22f, 1e-5f);

    cb.onMouseMove(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    MockRenderer r1;
    cb.render(r1);                                // started, not advanced
    CHECK_FLOAT_EQ(r1.getDrawCalls()[0].color.x, 0.22f, 1e-5f);

    cb.tick(0.09f);
    MockRenderer r2;
    cb.render(r2);                                // completed → hover
    CHECK_FLOAT_EQ(r2.getDrawCalls()[0].color.x, 0.28f, 1e-5f);
    CHECK_FLOAT_EQ(r2.getDrawCalls()[0].color.y, 0.28f, 1e-5f);
    CHECK_FLOAT_EQ(r2.getDrawCalls()[0].color.z, 0.30f, 1e-5f);
}

// MenuItem highlight bar: transparent → (0.18,0.45,0.78,0.55) on hover,
// back to transparent on leave — both through the tween, and the bar
// must not be drawn at all once fully faded.
TEST_CASE(menuitem_hover_bar_fades) {
    MenuItem item;
    item.setSize(FVector2(200.0f, 24.0f));
    item.setText(L"Open");

    // Helper: does this render contain the highlight bar draw?
    auto hasBar = [](const MockRenderer& r) {
        for (const auto& dc : r.getDrawCalls()) {
            if (dc.type == MockRenderer::DrawCall::Rect &&
                std::abs(dc.color.x - 0.18f) < 1e-4f &&
                std::abs(dc.color.y - 0.45f) < 1e-4f &&
                std::abs(dc.color.z - 0.78f) < 1e-4f) {
                return true;
            }
        }
        return false;
    };
    auto barAlpha = [](const MockRenderer& r) {
        for (const auto& dc : r.getDrawCalls()) {
            if (dc.type == MockRenderer::DrawCall::Rect &&
                std::abs(dc.color.x - 0.18f) < 1e-4f &&
                std::abs(dc.color.y - 0.45f) < 1e-4f) {
                return dc.color.w;
            }
        }
        return -1.0f;
    };

    // Prime: first render (not hovered) must draw no bar.
    MockRenderer r0;
    item.render(r0);
    CHECK_FALSE(hasBar(r0));

    // Hover: bar fades in from transparent.
    item.onMouseMove(UIMouseEvent(FVector2(100.0f, 12.0f), 0));
    MockRenderer r1;
    item.render(r1);
    CHECK(hasBar(r1));
    CHECK_FLOAT_EQ(barAlpha(r1), 0.0f, 1e-5f);    // started, not advanced

    item.tick(0.045f);
    MockRenderer r2;
    item.render(r2);
    const float mid = barAlpha(r2);
    CHECK(mid > 0.0f && mid < 0.55f);             // mid-flight

    item.tick(0.045f);
    MockRenderer r3;
    item.render(r3);
    CHECK_FLOAT_EQ(barAlpha(r3), 0.55f, 1e-4f);   // completed

    // Leave: bar fades back out...
    item.onMouseLeave();
    MockRenderer r4;
    item.render(r4);
    CHECK(hasBar(r4));                            // still mid-fade-out

    // ...and is gone once the tween completes.
    item.tick(0.09f);
    MockRenderer r5;
    item.render(r5);
    CHECK_FALSE(hasBar(r5));
}

// Shell/Gallery popup path: Menu overrides tick while mounted on the overlay.
// The override must keep descending into MenuItem children or their hover bar
// starts at alpha 0 and never becomes visible even though clicks still work.
TEST_CASE(menu_overlay_tick_advances_item_hover_bar) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Widget host;
    host.setSize(FVector2(200.0f, 24.0f));
    Menu* menu = new Menu();
    MenuItem* item = menu->addItem(L"Open");
    menu->open(&host, FVector2(20.0f, 24.0f));
    menu->performLayout();

    auto barAlpha = [](const MockRenderer& renderer) {
        for (const auto& call : renderer.getDrawCalls()) {
            if (call.type == MockRenderer::DrawCall::Rect
                && std::abs(call.color.x - 0.18f) < 1e-4f
                && std::abs(call.color.y - 0.45f) < 1e-4f
                && std::abs(call.color.z - 0.78f) < 1e-4f) {
                return call.color.w;
            }
        }
        return -1.0f;
    };

    MockRenderer prime;
    item->render(prime);

    const FRectangle bounds = item->getWorldBounds();
    CHECK(ui.onMouseMove((bounds.minX + bounds.maxX) * 0.5f,
                         (bounds.minY + bounds.maxY) * 0.5f));
    CHECK(item->isMouseOver());

    MockRenderer start;
    item->render(start);
    CHECK_FLOAT_EQ(barAlpha(start), 0.0f, 1e-5f);

    ui.update(0.045f);
    MockRenderer mid;
    item->render(mid);
    const float alpha = barAlpha(mid);
    CHECK(alpha > 0.0f && alpha < 0.55f);

    menu->close();
    ui.shutdown();
}

// Gallery-shaped scenario: a JSON-loaded button inside a UIManager root.
// The hover color transition must survive the full event → tick → render
// chain (onMouseMove via manager dispatch, update() ticking the root
// cascade, render reading the interpolated fill).
TEST_CASE(button_hover_transition_via_uimanager_scenario) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    const char* json = R"({
        "type": "VBox",
        "id": "root",
        "size": { "w": 800, "h": 600 },
        "children": [
            { "type": "Button", "id": "anim_btn", "text": "Hover me", "size": { "w": 200, "h": 28 } }
        ]
    })";
    CHECK(ui.loadFromString(json));
    ui.layout();

    Button* btn = dynamic_cast<Button*>(ui.findById("anim_btn"));
    CHECK_NOT_NULL(btn);

    // Prime: first render paints the current (normal) state color. The
    // anti-ghost rule means a widget that has never rendered snaps to its
    // present-state color — so this must happen BEFORE the hover, like a
    // real on-screen button that has been painted at rest.
    MockRenderer r0;
    btn->render(r0);
    CHECK_FLOAT_EQ(r0.getDrawCalls()[0].color.x, 0.28f, 1e-5f);

    // Hover through the manager (same funnel as Gallery). The button is
    // laid out at (4,4)-(204,32) (VBox margins) — the pick must land
    // INSIDE it, else the VBox root consumes the hit and the button
    // never sees the event.
    ui.onMouseMove(100.0f, 18.0f);
    CHECK(btn->getState() == ButtonState::Hovered);

    // Tween started but not advanced → still the old color.
    MockRenderer r1;
    btn->render(r1);
    CHECK_FLOAT_EQ(r1.getDrawCalls()[0].color.x, 0.28f, 1e-5f);

    // Half the 90ms duration via manager update → mid-flight color.
    ui.update(0.045f);
    MockRenderer r2;
    btn->render(r2);
    const float cx = r2.getDrawCalls()[0].color.x;
    CHECK(cx > 0.28f && cx < 0.36f);

    // Full duration → hover target.
    ui.update(0.045f);
    MockRenderer r3;
    btn->render(r3);
    CHECK_FLOAT_EQ(r3.getDrawCalls()[0].color.x, 0.36f, 1e-5f);

    ui.shutdown();
}

// Styled buttons are state-blind (constant backgroundColor target), so
// the transition is a no-op there — hover must not shift the fill.
TEST_CASE(styled_button_hover_no_transition) {
    StyleManager::get().setStyleSheet(nullptr);

    StyleSheet sheet;
    WidgetStyle st = StyleBuilder::makeButton();
    sheet.setStyle("t_noanim_btn", st);
    StyleManager::get().setStyleSheet(&sheet);

    Button button;
    button.setSize(FVector2(100.0f, 32.0f));
    button.setStyleId("t_noanim_btn");

    MockRenderer r0;
    button.render(r0);
    CHECK(r0.getDrawCalls().size() >= 1u);
    const FVector4 c0 = r0.getDrawCalls()[0].color;

    button.onMouseMove(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    MockRenderer r1;
    button.render(r1);
    const FVector4 c1 = r1.getDrawCalls()[0].color;
    CHECK_FLOAT_EQ(c1.x, c0.x, 1e-6f);
    CHECK_FLOAT_EQ(c1.y, c0.y, 1e-6f);
    CHECK_FLOAT_EQ(c1.z, c0.z, 1e-6f);
    CHECK_FLOAT_EQ(c1.w, c0.w, 1e-6f);

    StyleManager::get().setStyleSheet(nullptr);
}

// Gallery-shaped full-tree scenario: load the REAL gallery.ui.json,
// switch to page 9, hover anim_btn3 through the manager, and drive the
// whole render (ui.render → tree cascade) — the fill draw at the
// button's world bounds must tween 0.28 → 0.36. This reproduces the
// Gallery report "hover 无反应" where the widget state WAS hovered
// (InputTrace hover=1 state=1) but the screen never changed.
TEST_CASE(gallery_page9_button_hover_via_full_tree) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(1280.0f, 600.0f);

    std::ifstream f(AYUI_SOURCE_DIR "/demo/assets/gallery.ui.json");
    const std::string json((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    CHECK(!json.empty());
    CHECK(ui.loadFromString(json));

    // Mirror showPage: only page_animation visible.
    static const char* kPages[] = {
        "page_basics", "page_images", "page_input", "page_collections",
        "page_overlay", "page_layout", "page_capabilities",
        "page_backend", "page_animation", "page_productization",
    };
    for (const char* id : kPages) {
        if (Widget* w = ui.findById(id)) {
            w->setVisible(std::strcmp(id, "page_animation") == 0);
        }
    }
    ui.layout();

    Button* btn = dynamic_cast<Button*>(ui.findById("anim_btn3"));
    CHECK_NOT_NULL(btn);

    // MockRenderer mirrors the production frame-local command buffer and
    // clears prior draw calls in beginFrame(). Search from the back to read
    // the top-most matching fill in the current frame.
    auto fillColorAt = [](const MockRenderer& r, const FRectangle& b) {
        const auto& dcs = r.getDrawCalls();
        for (auto it = dcs.rbegin(); it != dcs.rend(); ++it) {
            if (it->type == MockRenderer::DrawCall::Rect &&
                std::abs(it->bounds.minX - b.minX) < 0.5f &&
                std::abs(it->bounds.minY - b.minY) < 0.5f &&
                std::abs(it->bounds.maxX - b.maxX) < 0.5f &&
                std::abs(it->bounds.maxY - b.maxY) < 0.5f) {
                return it->color;
            }
        }
        return FVector4(-1.0f, -1.0f, -1.0f, -1.0f);
    };

    // Prime: full-tree render at rest → normal fill.
    ui.render();
    const FRectangle b = btn->getWorldBounds();
    const FVector4 c0 = fillColorAt(backend, b);
    CHECK(c0.x > 0.0f);
    CHECK_FLOAT_EQ(c0.x, 0.28f, 1e-4f);

    // Hover via manager at the button's top-left inside corner.
    ui.onMouseMove(b.minX + 8.0f, b.minY + 6.0f);
    CHECK(btn->getState() == ButtonState::Hovered);

    // Tween just started → old color.
    ui.render();
    CHECK_FLOAT_EQ(fillColorAt(backend, b).x, 0.28f, 1e-4f);

    // Half duration → strictly between.
    ui.update(0.045f);
    ui.render();
    const float mid = fillColorAt(backend, b).x;
    CHECK(mid > 0.28f && mid < 0.36f);

    // Full duration → hover target.
    ui.update(0.045f);
    ui.render();
    CHECK_FLOAT_EQ(fillColorAt(backend, b).x, 0.36f, 1e-4f);

    ui.shutdown();
}

TEST_SUITE_END
