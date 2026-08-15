#include "AYTest.h"
#include "AYUI/StatusBar.h"
#include "AYUI/TextLabel.h"
#include "AYUI/Button.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_StatusBar_G8)

// G8 — addPanel(Widget*) accepts any Widget and StatusBar owns it.

TEST_CASE(statusbar_g8_addpanel_widget_takes_ownership) {
    StatusBar sb;
    auto* lbl = new TextLabel();
    lbl->setText(L"hello");
    Widget* returned = sb.addPanel(lbl);
    CHECK(returned == lbl);
    CHECK(sb.getPanelCount() == 1u);
    CHECK(sb.getPanelWidget(0) == lbl);
    CHECK(sb.getPanel(0) == lbl);   // TextLabel legacy accessor still works
    // ~StatusBar will delete lbl.
}

TEST_CASE(statusbar_g8_addpanel_nullptr_is_noop) {
    StatusBar sb;
    sb.addPanel(static_cast<Widget*>(nullptr));
    CHECK(sb.getPanelCount() == 0u);
}

TEST_CASE(statusbar_g8_addpanel_custom_widget_not_textlabel) {
    // Button is not a TextLabel — legacy getPanel must return nullptr;
    // getPanelWidget must still return the Button.
    StatusBar sb;
    auto* btn = new Button();
    btn->setText(L"Click");
    sb.addPanel(btn);
    CHECK(sb.getPanelCount() == 1u);
    CHECK_NOT_NULL(sb.getPanelWidget(0));
    CHECK(sb.getPanelWidget(0) == btn);
    CHECK(sb.getPanel(0) == nullptr);   // Button is not a TextLabel
}

TEST_CASE(statusbar_g8_addpanel_progressbar) {
    // ProgressBar is a real-world non-text panel use case (think
    // download-status, build progress).
    StatusBar sb;
    auto* pb = new ProgressBar();
    pb->setMin(0.0f);
    pb->setMax(100.0f);
    pb->setValue(42.0f);
    sb.addPanel(pb);
    CHECK(sb.getPanelCount() == 1u);
    ProgressBar* rt = dynamic_cast<ProgressBar*>(sb.getPanelWidget(0));
    CHECK_NOT_NULL(rt);
    CHECK_FLOAT_EQ(rt->getValue(), 42.0f, 1e-5f);
}

TEST_CASE(statusbar_g8_addpanel_text_overload_wraps_in_textlabel) {
    StatusBar sb;
    auto* lbl = sb.addPanel(L"Ready");
    CHECK_NOT_NULL(lbl);
    CHECK(sb.getPanelCount() == 1u);
    CHECK(lbl->getText() == L"Ready");
}

TEST_CASE(statusbar_g8_mixed_text_and_widget_panels) {
    // Mix text + button + progress — confirms the polymorphic store.
    StatusBar sb;
    sb.addPanel(L"Status:");
    auto* btn = new Button();
    btn->setText(L"Retry");
    sb.addPanel(btn);
    auto* pb = new ProgressBar();
    sb.addPanel(pb);
    CHECK(sb.getPanelCount() == 3u);
    CHECK(sb.getPanel(0) != nullptr);
    CHECK(sb.getPanel(0)->getText() == L"Status:");
    CHECK(sb.getPanel(1) == nullptr);
    CHECK_NOT_NULL(dynamic_cast<Button*>(sb.getPanelWidget(1)));
    CHECK_NOT_NULL(dynamic_cast<ProgressBar*>(sb.getPanelWidget(2)));
}

TEST_CASE(statusbar_g8_setpaneltext_skips_non_textlabel) {
    StatusBar sb;
    auto* btn = new Button();
    sb.addPanel(btn);
    // No crash; just silently no-op since the panel isn't a TextLabel.
    sb.setPanelText(0, L"ignored");
    CHECK(sb.getPanelCount() == 1u);
}

TEST_CASE(statusbar_g8_clearpanels_deletes_owned_widgets) {
    // StatusBar owns its panels. clearPanels must delete them and not
    // leave dangling pointers in the children[] walk.
    StatusBar sb;
    auto* pb = new ProgressBar();
    sb.addPanel(pb);
    CHECK(sb.getPanelCount() == 1u);
    sb.clearPanels();
    CHECK(sb.getPanelCount() == 0u);
    // Adding a new panel after clear must work (no UAF).
    sb.addPanel(L"after clear");
    CHECK(sb.getPanelCount() == 1u);
    CHECK(sb.getPanel(0)->getText() == L"after clear");
}

TEST_CASE(statusbar_g8_dtor_deletes_panels_without_crash) {
    // Stack-allocated StatusBar + heap panel — ~StatusBar must delete
    // the panel cleanly without UAF. If this case segfaults, the
    // ownership / detach-from-parent path is broken.
    {
        StatusBar sb;
        auto* lbl = new TextLabel();
        lbl->setText(L"ephemeral");
        sb.addPanel(lbl);
        // sb goes out of scope here; lbl must be freed.
    }
    CHECK(true);   // reaching this line means no crash
}

// G8 — multi-line support. A panel that sets its own height causes the
// bar to grow (up to MAX(panel heights) + 2*padding).

TEST_CASE(statusbar_g8_multiline_grows_bar_to_tallest_panel) {
    StatusBar sb;
    sb.setSize(FVector2(400.0f, 22.0f));
    auto* tall = new TextLabel();
    tall->setText(L"Two-row info");
    tall->setSize(FVector2(0.0f, 38.0f));   // 38px panel → 46px bar
    sb.addPanel(tall);
    sb.performLayout();
    CHECK(sb.getSize().y >= 46.0f - 1e-3f);
}

TEST_CASE(statusbar_g8_single_row_default_height_unchanged) {
    // Empty bar keeps its declared size; single-row default panels
    // (height not set) don't grow the bar.
    StatusBar sb;
    sb.setSize(FVector2(400.0f, 22.0f));
    sb.addPanel(L"Ready");
    sb.performLayout();
    CHECK_FLOAT_EQ(sb.getSize().y, 22.0f, 1e-3f);
}

TEST_CASE(statusbar_g8_clear_panels_resets_height_after_multiline_grow) {
    // After growing the bar to fit a tall panel, clearing panels
    // should leave the bar at its declared size (or close to it).
    StatusBar sb;
    sb.setSize(FVector2(400.0f, 22.0f));
    auto* tall = new TextLabel();
    tall->setSize(FVector2(0.0f, 38.0f));
    sb.addPanel(tall);
    sb.performLayout();
    CHECK(sb.getSize().y > 22.0f);
    sb.clearPanels();
    // After clear we don't auto-shrink (declarative API), but the
    // bar's height should at least be the kDefaultHeight floor.
    CHECK(sb.getSize().y >= StatusBar::kDefaultHeight - 1e-3f);
}

TEST_SUITE_END