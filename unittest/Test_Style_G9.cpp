#include "AYTest.h"
#include "AYStyle.h"
#include "AYSlider.h"
#include "AYCheckBox.h"
#include "AYRadioButton.h"
#include "AYPanel.h"
#include "AYSeparator.h"
#include "AYToolBarSeparator.h"
#include "AYMockRenderer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Find the first drawRect whose color matches the predicate. The
// MockRenderer records one entry per draw, so this locates a specific
// rect by color (more robust than by index, since multi-widget scenes
// emit many rects).
const MockRenderer::DrawCall* findRectByColor(
    const MockRenderer& r,
    const FVector4& target,
    float eps = 1e-3f)
{
    for (const auto& dc : r.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) continue;
        const FVector4& c = dc.color;
        if (std::abs(c.x - target.x) < eps &&
            std::abs(c.y - target.y) < eps &&
            std::abs(c.z - target.z) < eps &&
            std::abs(c.w - target.w) < eps) {
            return &dc;
        }
    }
    return nullptr;
}

// Wire a stylesheet with one custom style under "g9_theme".
// Returns a heap StyleSheet* so callers can `delete` it.
StyleSheet* installG9Theme() {
    StyleSheet* sheet = new StyleSheet();
    WidgetStyle s = StyleBuilder::makePanel();
    // Distinctive red — must NOT match the v1 fallback colors so the
    // assertion can prove the style was honored.
    s.backgroundColor = FVector4(0.65f, 0.10f, 0.10f, 1.0f);
    // borderColor (top-level) is unused by resolveStyle(); the resolved
    // style reads from BorderStyle::color. Set both for consistency so
    // code that reads either field gets the same answer.
    s.borderColor    = FVector4(0.10f, 0.85f, 0.10f, 1.0f);
    s.border.color   = FVector4(0.10f, 0.85f, 0.10f, 1.0f);
    s.border.width   = 2.0f;
    sheet->setStyle("g9_theme", s);
    StyleManager::get().setStyleSheet(sheet);
    return sheet;
}

void uninstallTheme(StyleSheet* sheet) {
    StyleManager::get().setStyleSheet(nullptr);
    delete sheet;
}

} // anon

TEST_SUITE(AYUI_Style_G9)

// ---------- Slider ----------

TEST_CASE(style_g9_slider_track_uses_style_background) {
    StyleSheet* sheet = installG9Theme();
    Slider s;
    s.setSize(FVector2(120.0f, 16.0f));
    s.setStyleId("g9_theme");
    s.setPosition(FVector2(0.0f, 0.0f));
    s.performLayout();

    MockRenderer r;
    s.render(r);

    // Track background is the FIRST rect drawn for a horizontal slider;
    // style.backgroundColor = (0.65, 0.10, 0.10, 1.0). Find it.
    const FVector4 expectedBg(0.65f, 0.10f, 0.10f, 1.0f);
    const auto* dc = findRectByColor(r, expectedBg);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

TEST_CASE(style_g9_slider_handle_brightened_from_style_bg) {
    // Slider handle (with no interaction state) should be a brightened
    // variant of the style background — mix toward white by 0.55.
    // bg = (0.65, 0.10, 0.10); handle = bg + (1-bg) * 0.55.
    StyleSheet* sheet = installG9Theme();
    Slider s;
    s.setSize(FVector2(120.0f, 16.0f));
    s.setStyleId("g9_theme");
    s.setPosition(FVector2(0.0f, 0.0f));
    s.performLayout();

    MockRenderer r;
    s.render(r);

    const float rComp = 0.65f + (1.0f - 0.65f) * 0.55f;
    const float gComp = 0.10f + (1.0f - 0.10f) * 0.55f;
    const float bComp = 0.10f + (1.0f - 0.10f) * 0.55f;
    const FVector4 expectedHandle(rComp, gComp, bComp, 1.0f);
    const auto* dc = findRectByColor(r, expectedHandle);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

TEST_CASE(style_g9_slider_no_style_uses_fallback_colors) {
    // Without a styleId, the v1 hardcoded greys win.
    Slider s;
    s.setSize(FVector2(120.0f, 16.0f));
    // No setStyleId — fallback palette.
    s.setPosition(FVector2(0.0f, 0.0f));
    s.performLayout();

    MockRenderer r;
    s.render(r);

    // Track fallback bg = (0.18, 0.18, 0.20, 1.0).
    const FVector4 trackBg(0.18f, 0.18f, 0.20f, 1.0f);
    const auto* dc = findRectByColor(r, trackBg);
    CHECK_NOT_NULL(dc);
}

// ---------- CheckBox ----------

TEST_CASE(style_g9_checkbox_box_uses_style_background) {
    StyleSheet* sheet = installG9Theme();
    CheckBox cb;
    cb.setSize(FVector2(120.0f, 24.0f));
    cb.setStyleId("g9_theme");
    cb.setChecked(true);
    cb.setPosition(FVector2(0.0f, 0.0f));
    cb.performLayout();

    MockRenderer r;
    cb.render(r);

    const FVector4 expectedBg(0.65f, 0.10f, 0.10f, 1.0f);
    const auto* dc = findRectByColor(r, expectedBg);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

TEST_CASE(style_g9_checkbox_accent_uses_brightened_style) {
    // Checked CheckBox accent: bg * 1.6 (clamped to 1.0).
    // bg = (0.65, 0.10, 0.10); accent = (1.0, 0.16, 0.16, 1.0).
    StyleSheet* sheet = installG9Theme();
    CheckBox cb;
    cb.setSize(FVector2(120.0f, 24.0f));
    cb.setStyleId("g9_theme");
    cb.setChecked(true);
    cb.setPosition(FVector2(0.0f, 0.0f));
    cb.performLayout();

    MockRenderer r;
    cb.render(r);

    const FVector4 expectedAccent(1.0f, 0.16f, 0.16f, 1.0f);
    const auto* dc = findRectByColor(r, expectedAccent);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

// ---------- RadioButton ----------

TEST_CASE(style_g9_radiobutton_outer_uses_style_background) {
    StyleSheet* sheet = installG9Theme();
    RadioButton rb;
    rb.setSize(FVector2(120.0f, 24.0f));
    rb.setStyleId("g9_theme");
    rb.setPosition(FVector2(0.0f, 0.0f));
    rb.performLayout();

    MockRenderer r;
    rb.render(r);

    const FVector4 expectedBg(0.65f, 0.10f, 0.10f, 1.0f);
    const auto* dc = findRectByColor(r, expectedBg);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

TEST_CASE(style_g9_radiobutton_selected_dot_brightened) {
    // Checked RadioButton dot: bg * 1.6.
    StyleSheet* sheet = installG9Theme();
    RadioButton rb;
    rb.setSize(FVector2(120.0f, 24.0f));
    rb.setStyleId("g9_theme");
    rb.setChecked(true);
    rb.setPosition(FVector2(0.0f, 0.0f));
    rb.performLayout();

    MockRenderer r;
    rb.render(r);

    const FVector4 expectedDot(1.0f, 0.16f, 0.16f, 1.0f);
    const auto* dc = findRectByColor(r, expectedDot);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

// ---------- Panel ----------

TEST_CASE(style_g9_panel_uses_style_background) {
    StyleSheet* sheet = installG9Theme();
    Panel p;
    p.setSize(FVector2(200.0f, 100.0f));
    p.setStyleId("g9_theme");
    p.setPosition(FVector2(0.0f, 0.0f));
    p.performLayout();

    MockRenderer r;
    p.render(r);

    const FVector4 expectedBg(0.65f, 0.10f, 0.10f, 1.0f);
    const auto* dc = findRectByColor(r, expectedBg);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

TEST_CASE(style_g9_panel_uses_style_border_color) {
    StyleSheet* sheet = installG9Theme();
    Panel p;
    p.setSize(FVector2(200.0f, 100.0f));
    p.setStyleId("g9_theme");
    p.setBorderEnabled(true);
    p.setPosition(FVector2(0.0f, 0.0f));
    p.performLayout();

    MockRenderer r;
    p.render(r);

    // Border color = (0.10, 0.85, 0.10, 1.0). drawBorderRect emits a
    // drawRect entry per side; findRectByColor finds the first one.
    const FVector4 expectedBorder(0.10f, 0.85f, 0.10f, 1.0f);
    const auto* dc = findRectByColor(r, expectedBorder);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

// ---------- Separator ----------

TEST_CASE(style_g9_separator_uses_style_border_color) {
    StyleSheet* sheet = installG9Theme();
    Separator sep;
    sep.setSize(FVector2(100.0f, 1.0f));
    sep.setStyleId("g9_theme");
    sep.setPosition(FVector2(0.0f, 0.0f));
    sep.performLayout();

    MockRenderer r;
    sep.render(r);

    const FVector4 expectedLine(0.10f, 0.85f, 0.10f, 1.0f);
    const auto* dc = findRectByColor(r, expectedLine);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

TEST_CASE(style_g9_separator_no_style_uses_internal_color) {
    // Without a styleId, Separator's stored _color wins (v1 behavior).
    Separator sep;
    sep.setSize(FVector2(100.0f, 1.0f));
    // setColor stores the override; no setStyleId → style.hasStyle=false.
    sep.setColor(FVector4(0.42f, 0.42f, 0.48f, 0.9f));
    sep.setPosition(FVector2(0.0f, 0.0f));
    sep.performLayout();

    MockRenderer r;
    sep.render(r);

    const FVector4 expected(0.42f, 0.42f, 0.48f, 0.9f);
    const auto* dc = findRectByColor(r, expected);
    CHECK_NOT_NULL(dc);
}

TEST_CASE(style_g9_toolbarseparator_style_overrides_constructor_palette) {
    // ToolBarSeparator's constructor sets a hardcoded palette
    // (0.42, 0.42, 0.48, 0.9). When a style is wired AND styleId is
    // set, the style.borderColor wins — proves the G9 收口 is uniform
    // for both Separator and ToolBarSeparator.
    StyleSheet* sheet = installG9Theme();
    ToolBarSeparator tbs;
    tbs.setSize(FVector2(1.0f, 24.0f));
    tbs.setStyleId("g9_theme");
    tbs.setPosition(FVector2(0.0f, 0.0f));
    tbs.performLayout();

    MockRenderer r;
    tbs.render(r);

    const FVector4 expected(0.10f, 0.85f, 0.10f, 1.0f);
    const auto* dc = findRectByColor(r, expected);
    CHECK_NOT_NULL(dc);
    uninstallTheme(sheet);
}

TEST_SUITE_END