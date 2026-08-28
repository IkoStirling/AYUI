#include "AYTest.h"
#include "AYUI/Widget.h"
#include "AYUI/UIManager.h"
#include "AYUI/ComboBox.h"
#include "AYUI/Menu.h"
#include "AYUI/Modal.h"
#include "AYUI/Tooltip.h"
#include "AYUI/MockRenderer.h"

#include <cmath>

using namespace ayt::ui;
using namespace ayt::math;

// UI animation lane (cut 1): popup fade-in on open and fade-out on close.
// Fade-out runs through UIManager's pending-close queue — the popup stays
// mounted (and excluded from hit-testing) until the fade completes, then
// unmounts / destroys. Pattern mirrors menu_open_fades_in_via_ui_update
// (Test_OpacityAnimation.cpp).
TEST_SUITE(AYUI_PopupFade)

namespace {

// ComboBox helper: fresh UIManager + one opened ComboBox whose popup
// lives on the overlay. The ComboBox is intentionally leaked (same as
// the existing overlay tests) — shutdown destroys the overlay subtree.
struct ComboFixture {
    MockRenderer backend;
    UIManager ui;
    ComboBox* cb = nullptr;

    ComboFixture() {
        ui.initialize(&backend);
        ui.setClientSize(800.0f, 600.0f);
        cb = new ComboBox();
        cb->setItems({L"a", L"b", L"c"});
        cb->setSize(FVector2(160.0f, 28.0f));
        cb->setPosition(FVector2(20.0f, 20.0f));
    }
    void open() { cb->openPopup(); }
    Widget* popup() const { return ui.getOverlayRoot()->getChildren()[0]; }
    size_t overlayCount() const { return ui.getOverlayRoot()->getChildren().size(); }
};

} // namespace

TEST_CASE(combobox_popup_fades_in) {
    ComboFixture fx;
    fx.open();
    CHECK(fx.cb->isPopupOpen());
    CHECK(fx.overlayCount() == 1u);

    // First frame: popup starts at ~0 opacity — every draw is invisible.
    MockRenderer r0;
    fx.popup()->render(r0);
    CHECK(!r0.getDrawCalls().empty());
    int visibleDuringFadeInCount = 0;
    for (const auto& dc : r0.getDrawCalls()) {
        if (dc.color.w >= 0.05f) {
            ++visibleDuringFadeInCount;
        }
    }
    CHECK(visibleDuringFadeInCount == 0);

    // UIManager::update drives the overlay cascade; 140ms fade completes.
    fx.ui.update(0.14f);

    MockRenderer r1;
    fx.popup()->render(r1);
    CHECK(!r1.getDrawCalls().empty());
    int translucentAfterFadeInCount = 0;
    for (const auto& dc : r1.getDrawCalls()) {
        if (dc.color.w <= 0.95f) {
            ++translucentAfterFadeInCount;
        }
    }
    CHECK(translucentAfterFadeInCount == 0);

    fx.ui.shutdown();
}

TEST_CASE(combobox_soft_close_fades_then_unmounts) {
    ComboFixture fx;
    fx.open();
    fx.cb->closePopup();          // UX-driven close (destroy=false)

    // Logically closed, but still mounted to render the fade-out.
    CHECK_FALSE(fx.cb->isPopupOpen());
    CHECK(fx.overlayCount() == 1u);

    fx.ui.update(0.13f);          // 120ms fade done → unmount
    CHECK(fx.overlayCount() == 0u);

    fx.ui.shutdown();
}

TEST_CASE(combobox_click_outside_fades_then_destroys) {
    ComboFixture fx;
    fx.open();
    Widget* popup = fx.popup();
    CHECK(fx.overlayCount() == 1u);

    // Click far from the popup → click-outside dismiss (destroy=true).
    fx.ui.onMouseButtonDown(500.0f, 500.0f, 0);
    fx.ui.onMouseButtonUp(500.0f, 500.0f, 0);
    CHECK_FALSE(fx.cb->isPopupOpen());

    // Fading: still mounted, but excluded from hit-testing.
    CHECK(fx.overlayCount() == 1u);
    CHECK(fx.ui.pickTopmostWidget(FVector2(120.0f, 60.0f)) != popup);

    fx.ui.update(0.13f);
    CHECK(fx.overlayCount() == 0u);

    fx.ui.shutdown();
}

TEST_CASE(combobox_reopen_cancels_fade_out) {
    ComboFixture fx;
    fx.open();
    fx.cb->closePopup();          // fade-out starts
    fx.cb->openPopup();           // reopen cancels it, fade-in restarts

    CHECK(fx.cb->isPopupOpen());
    CHECK(fx.overlayCount() == 1u);

    fx.ui.update(0.25f);          // long past both fades
    CHECK(fx.cb->isPopupOpen());
    CHECK(fx.overlayCount() == 1u);
    CHECK_FLOAT_EQ(fx.popup()->getOpacity(), 1.0f, 1e-5f);   // ended opaque

    fx.ui.shutdown();
}

TEST_CASE(menu_close_fades_then_reparents) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Widget host;
    host.setSize(FVector2(800.0f, 600.0f));

    Menu* menu = new Menu();
    menu->addItem(L"Open");
    menu->open(&host, FVector2(100.0f, 80.0f));
    menu->setSize(FVector2(220.0f, 72.0f));

    menu->close();
    CHECK_FALSE(menu->isOpen());
    // Fading: still mounted on the overlay.
    CHECK(menu->getParent() == ui.getOverlayRoot());

    ui.update(0.13f);
    // Finalize: soft-unmount reparents under the owning host, hidden.
    CHECK(menu->getParent() == &host);
    CHECK_FALSE(menu->isVisible());

    ui.shutdown();
}

TEST_CASE(modal_close_fades_then_detaches) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Modal* m = new Modal();
    m->openModal();
    CHECK(m->isOpen());
    // Dimmer + modal both live on the overlay.
    CHECK(ui.getOverlayRoot()->getChildren().size() == 2u);

    m->closeModal();
    CHECK_FALSE(m->isOpen());
    // Fading: both still mounted (closeModal's removeChild is held off
    // while the close is pending).
    CHECK(ui.getOverlayRoot()->getChildren().size() == 2u);

    ui.update(0.13f);
    CHECK(ui.getOverlayRoot()->getChildren().empty());

    ui.shutdown();
}

TEST_CASE(tooltip_show_fades_in_hide_fades_out) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    CHECK_NOT_NULL(tip);
    tip->setText(L"Fade test");
    tip->setHoverDelay(0.1f);

    // Show/hide are private — drive them through the hover timer like a
    // host does (3-arg tick; viewport (0,0) pulls from the UIManager).
    tip->tick(0.15f, FVector2(75.0f, 60.0f), FVector2(0.0f, 0.0f));  // in bounds, past delay
    CHECK(tip->isShowing());

    // Fade-in first frame: everything nearly transparent.
    MockRenderer r0;
    tip->render(r0);
    CHECK(!r0.getDrawCalls().empty());
    int visibleDuringFadeInCount = 0;
    for (const auto& dc : r0.getDrawCalls()) {
        if (dc.color.w >= 0.05f) {
            ++visibleDuringFadeInCount;
        }
    }
    CHECK(visibleDuringFadeInCount == 0);

    ui.update(0.13f);             // 120ms fade completes
    MockRenderer r1;
    tip->render(r1);
    int translucentAfterFadeInCount = 0;
    for (const auto& dc : r1.getDrawCalls()) {
        if (dc.color.w <= 0.95f) {
            ++translucentAfterFadeInCount;
        }
    }
    CHECK(translucentAfterFadeInCount == 0);

    // Move out of the target → hide: visually faded out, but the widget
    // stays visible (rendering the fade) until the tween completes.
    tip->tick(0.2f, FVector2(500.0f, 400.0f), FVector2(0.0f, 0.0f));  // outside
    CHECK_FALSE(tip->isShowing());
    CHECK(tip->isVisible());

    ui.update(0.11f);             // 100ms fade-out done
    CHECK_FALSE(tip->isVisible());

    tip->detach();
    destroyWidgetTree(tip);
    ui.shutdown();
}

// =============================================================================
// UI-anim cut 2 — popup slide-in. All four overlay popups slide from 6-8px
// off the anchor while fading; the fade assertions above only look at
// color.w so they are unaffected by the added position tween.
// =============================================================================

TEST_CASE(combobox_popup_slides_in) {
    ComboFixture fx;
    fx.open();
    CHECK(fx.cb->isPopupOpen());

    // Mounted 8px above the anchored position, fading in.
    const float startY = fx.popup()->getPosition().y;
    CHECK(startY < 200.0f);                       // sane placement
    MockRenderer r0;
    fx.popup()->render(r0);
    CHECK(!r0.getDrawCalls().empty());
    CHECK(r0.getDrawCalls()[0].color.w < 0.05f);  // still fading

    // 160ms slide + 140ms fade both complete.
    fx.ui.update(0.17f);
    const float endY = fx.popup()->getPosition().y;
    CHECK_FLOAT_EQ(endY - startY, 8.0f, 1e-3f);
    CHECK_FLOAT_EQ(fx.popup()->getOpacity(), 1.0f, 1e-5f);
    CHECK_FALSE(fx.popup()->isPositionAnimating());

    fx.ui.shutdown();
}

TEST_CASE(menu_slides_in_with_spring) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Widget host;
    host.setSize(FVector2(800.0f, 600.0f));

    Menu* menu = new Menu();
    menu->addItem(L"Open");
    menu->open(&host, FVector2(100.0f, 80.0f));

    // Starts 6px above the anchor; Spring curve never overshoots past it.
    CHECK_FLOAT_EQ(menu->getPosition().y, 74.0f, 1e-3f);
    CHECK_FLOAT_EQ(menu->getPosition().x, 100.0f, 1e-3f);

    ui.update(0.17f);                 // 160ms slide done
    CHECK_FLOAT_EQ(menu->getPosition().y, 80.0f, 1e-3f);
    CHECK_FLOAT_EQ(menu->getOpacity(), 1.0f, 1e-5f);
    CHECK_FALSE(menu->isPositionAnimating());

    ui.shutdown();
}

TEST_CASE(modal_plate_slides_dimmer_stays) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 600.0f);

    Modal* m = new Modal();
    m->openModal();
    CHECK(m->isOpen());
    CHECK(ui.getOverlayRoot()->getChildren().size() == 2u);

    Widget* dimmer = ui.getOverlayRoot()->getChildren()[0];
    CHECK_FLOAT_EQ(dimmer->getPosition().x, 0.0f, 1e-3f);
    CHECK_FLOAT_EQ(dimmer->getPosition().y, 0.0f, 1e-3f);

    // Plate starts 8px above its resting position; dimmer untouched.
    const float plateStartY = m->getPosition().y;
    CHECK_FLOAT_EQ(plateStartY, -8.0f, 1e-3f);

    ui.update(0.17f);
    CHECK_FLOAT_EQ(m->getPosition().y, 0.0f, 1e-3f);
    CHECK_FLOAT_EQ(dimmer->getPosition().y, 0.0f, 1e-3f);
    CHECK_FLOAT_EQ(m->getOpacity(), 1.0f, 1e-5f);

    ui.shutdown();
}

TEST_SUITE_END
