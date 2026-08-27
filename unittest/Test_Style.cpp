#include "AYTest.h"
#include "AYUI/Style.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Button.h"
#include <cstring>
#include <string>

using namespace ayt::ui;

TEST_SUITE(AYUI_Style)

// Default-constructed StyleSheet pre-registers the four StyleBuilder
// defaults (button_default / textlabel_default / window_default /
// panel_default). Any id lookup that misses a custom registration falls
// back to makeDefault.
TEST_CASE(stylesheet_default_presets) {
    StyleSheet sheet;
    const WidgetStyle* btn = sheet.getStyle("button_default");
    CHECK(btn != nullptr);
    // Button preset border radius is 4.0 (see StyleBuilder::makeButton).
    CHECK(btn->border.cornerRadius == 4.0f);

    const WidgetStyle* missing = sheet.getStyle("does_not_exist");
    CHECK(missing != nullptr);
    // Fallback is makeDefault; cornerRadius 0.
    CHECK(missing->border.cornerRadius == 0.0f);
}

TEST_CASE(stylesheet_load_from_string_basic) {
    StyleSheet sheet;

    const char* json = R"({
        "styles": {
            "button_primary": {
                "backgroundColor": [0.25, 0.45, 0.85, 1.0],
                "border": { "width": 2, "cornerRadius": 8, "color": [0.4, 0.4, 0.4, 1.0] }
            },
            "panel_dark": {
                "backgroundColor": [0.1, 0.1, 0.12, 0.95],
                "border": { "width": 1, "cornerRadius": 6 }
            }
        }
    })";

    CHECK(sheet.loadFromString(json, std::strlen(json)));

    const WidgetStyle* primary = sheet.getStyle("button_primary");
    CHECK(primary != nullptr);
    CHECK_FLOAT_EQ(primary->backgroundColor.x, 0.25f, 1e-5f);
    CHECK_FLOAT_EQ(primary->backgroundColor.y, 0.45f, 1e-5f);
    CHECK_FLOAT_EQ(primary->backgroundColor.z, 0.85f, 1e-5f);
    CHECK_FLOAT_EQ(primary->backgroundColor.w, 1.0f, 1e-5f);
    CHECK(primary->border.width == 2.0f);
    CHECK(primary->border.cornerRadius == 8.0f);

    const WidgetStyle* dark = sheet.getStyle("panel_dark");
    CHECK(dark != nullptr);
    CHECK_FLOAT_EQ(dark->backgroundColor.x, 0.1f, 1e-5f);
    CHECK(dark->border.cornerRadius == 6.0f);
    // color not provided in JSON — must fall back to makeDefault's color
    // (0.5, 0.5, 0.5, 1.0).
    CHECK_FLOAT_EQ(dark->border.color.x, 0.5f, 1e-5f);
}

TEST_CASE(stylesheet_load_from_string_malformed) {
    StyleSheet sheet;
    const char* bad = R"({ not valid json )";
    CHECK(sheet.loadFromString(bad, std::strlen(bad)) == false);

    // Wrong top-level shape (no "styles" key).
    const char* noStyles = R"({ "foo": 42 })";
    CHECK(sheet.loadFromString(noStyles, std::strlen(noStyles)) == false);
}

TEST_CASE(stylesheet_load_from_string_null_or_empty) {
    StyleSheet sheet;
    CHECK(sheet.loadFromString(nullptr, 0) == false);
    CHECK(sheet.loadFromString("", 0) == false);
}

TEST_CASE(stylesheet_set_style_overrides) {
    StyleSheet sheet;
    WidgetStyle custom;
    custom.backgroundColor = {0.9f, 0.1f, 0.1f, 1.0f};
    custom.border.cornerRadius = 12.0f;
    sheet.setStyle("button_primary", custom);

    const WidgetStyle* s = sheet.getStyle("button_primary");
    CHECK(s != nullptr);
    CHECK_FLOAT_EQ(s->backgroundColor.x, 0.9f, 1e-5f);
    CHECK(s->border.cornerRadius == 12.0f);
}

TEST_CASE(stylemanager_singleton_set_sheet) {
    // Reset to a known state (singleton may have been touched by prior tests).
    StyleManager::get().setStyleSheet(nullptr);

    StyleSheet sheet;
    sheet.setStyle("from_manager",
                   [] { WidgetStyle s = StyleBuilder::makeDefault();
                        s.backgroundColor = {0.7f, 0.2f, 0.2f, 1.0f};
                        return s; }());

    StyleManager::get().setStyleSheet(&sheet);
    const WidgetStyle* s = StyleManager::get().getStyle("from_manager");
    CHECK(s != nullptr);
    CHECK_FLOAT_EQ(s->backgroundColor.x, 0.7f, 1e-5f);

    // Detach so subsequent tests start clean.
    StyleManager::get().setStyleSheet(nullptr);
}

TEST_CASE(button_uses_style_when_registered) {
    // R-5 plumbing: when a Button has a styleId and the StyleManager has
    // a style with a non-default backgroundColor registered for that id,
    // onRender must use the style color rather than the hardcoded fallback.
    StyleManager::get().setStyleSheet(nullptr);

    StyleSheet sheet;
    WidgetStyle custom = StyleBuilder::makeDefault();
    custom.backgroundColor = {0.25f, 0.45f, 0.85f, 1.0f};  // distinct from makeDefault
    custom.border.cornerRadius = 6.0f;
    sheet.setStyle("button_primary", custom);
    StyleManager::get().setStyleSheet(&sheet);

    MockRenderer renderer;
    Button button;
    button.setSize({100.0f, 32.0f});
    button.setStyleId("button_primary");
    button.setText(L"OK");

    button.render(renderer);

    // The first draw call is the fill rect — must reflect the custom
    // backgroundColor we registered, NOT the hardcoded (0.28, 0.28, 0.30).
    CHECK(renderer.getDrawCalls().size() >= 1u);
    const auto& fill = renderer.getDrawCalls().front();
    CHECK(fill.type == MockRenderer::DrawCall::Rect);
    CHECK_FLOAT_EQ(fill.color.x, 0.25f, 1e-5f);
    CHECK_FLOAT_EQ(fill.color.y, 0.45f, 1e-5f);
    CHECK_FLOAT_EQ(fill.color.z, 0.85f, 1e-5f);

    StyleManager::get().setStyleSheet(nullptr);
}

TEST_CASE(button_falls_back_when_no_style) {
    // No StyleSheet registered. The Button must render with its hardcoded
    // (R-1) fallback so the existing button_render_preserves_hover_fill
    // assertion (color 0.36f for Hovered) keeps working.
    StyleManager::get().setStyleSheet(nullptr);

    MockRenderer renderer;
    Button button;
    button.setSize({100.0f, 32.0f});

    UIMouseEvent hover({50.0f, 16.0f}, 0);
    button.onMouseMove(hover);
    button.render(renderer);

    CHECK(renderer.getDrawCalls().size() >= 1u);
    CHECK_FLOAT_EQ(renderer.getDrawCalls().front().color.x, 0.36f, 1e-5f);
    CHECK_FLOAT_EQ(renderer.getDrawCalls().front().color.y, 0.38f, 1e-5f);
    CHECK_FLOAT_EQ(renderer.getDrawCalls().front().color.z, 0.42f, 1e-5f);
}

TEST_CASE(button_falls_back_when_style_id_unknown) {
    // styleId is set but StyleManager has no matching style (and no default
    // stylesheet registered). Must still fall back to hardcoded.
    StyleManager::get().setStyleSheet(nullptr);

    MockRenderer renderer;
    Button button;
    button.setSize({100.0f, 32.0f});
    button.setStyleId("does_not_exist_anywhere");

    UIMouseEvent hover({50.0f, 16.0f}, 0);
    button.onMouseMove(hover);
    button.render(renderer);

    CHECK_FLOAT_EQ(renderer.getDrawCalls().front().color.x, 0.36f, 1e-5f);
}

// resolveStyle(): empty styleId → hasStyle=false (no work done).
TEST_CASE(resolveStyle_empty_id_returns_no_style) {
    StyleManager::get().setStyleSheet(nullptr);
    const ResolvedStyle r = resolveStyle("");
    CHECK(r.hasStyle == false);
}

// resolveStyle(): no StyleSheet wired → manager.getStyle returns nullptr
// → hasStyle=false even when styleId is set.
TEST_CASE(resolveStyle_no_stylesheet_returns_no_style) {
    StyleManager::get().setStyleSheet(nullptr);
    const ResolvedStyle r = resolveStyle("button_primary");
    CHECK(r.hasStyle == false);
}

// resolveStyle(): makeDefault sentinel. StyleBuilder pre-registers
// button_default with backgroundColor = (0.3, 0.3, 0.3, 1) which IS
// distinct from makeDefault → should resolve. (Sanity for the bgIsDefault
// check: ensure the compare itself works.)
TEST_CASE(resolveStyle_button_default_resolves) {
    StyleManager::get().setStyleSheet(nullptr);

    StyleSheet sheet;  // constructor pre-registers button_default
    StyleManager::get().setStyleSheet(&sheet);

    const ResolvedStyle r = resolveStyle("button_default");
    CHECK(r.hasStyle == true);
    CHECK_FLOAT_EQ(r.backgroundColor.x, 0.3f, 1e-5f);
    CHECK_FLOAT_EQ(r.backgroundColor.y, 0.3f, 1e-5f);
    CHECK_FLOAT_EQ(r.backgroundColor.z, 0.3f, 1e-5f);
    CHECK_FLOAT_EQ(r.backgroundColor.w, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(r.borderWidth, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(r.cornerRadius, 4.0f, 1e-5f);

    StyleManager::get().setStyleSheet(nullptr);
}

// resolveStyle(): bgIsDefault sentinel. A style whose backgroundColor
// exactly matches makeDefault must NOT be returned as a real style —
// otherwise every makeDefault-derived preset would silently override
// hardcoded colors. We register such a style explicitly here.
TEST_CASE(resolveStyle_bg_is_default_returns_no_style) {
    StyleManager::get().setStyleSheet(nullptr);

    StyleSheet sheet;
    WidgetStyle sentinel = StyleBuilder::makeDefault();  // bg = (0.2, 0.2, 0.2, 1)
    sheet.setStyle("sentinel_style", sentinel);
    StyleManager::get().setStyleSheet(&sheet);

    const ResolvedStyle r = resolveStyle("sentinel_style");
    CHECK(r.hasStyle == false);

    StyleManager::get().setStyleSheet(nullptr);
}

// resolveStyle(): panel_default now has a non-default backgroundColor
// (post C-2 fix). Pre-fix it was (0.2, 0.2, 0.2, 1) which collided with
// makeDefault and silently dropped. This test pins the new value so a
// regression to the makeDefault collision is caught.
TEST_CASE(resolveStyle_panel_default_resolves) {
    StyleManager::get().setStyleSheet(nullptr);

    StyleSheet sheet;  // constructor pre-registers panel_default
    StyleManager::get().setStyleSheet(&sheet);

    const ResolvedStyle r = resolveStyle("panel_default");
    CHECK(r.hasStyle == true);
    // makePanel sets bg to (0.18, 0.18, 0.20, 1) — distinct from makeDefault's
    // (0.2, 0.2, 0.2, 1) so the bgIsDefault sentinel does NOT trigger.
    CHECK_FLOAT_EQ(r.backgroundColor.x, 0.18f, 1e-5f);
    CHECK_FLOAT_EQ(r.backgroundColor.y, 0.18f, 1e-5f);
    CHECK_FLOAT_EQ(r.backgroundColor.z, 0.20f, 1e-5f);
    CHECK_FLOAT_EQ(r.borderWidth, 1.0f, 1e-5f);
    // makePanel border color is (0.3, 0.3, 0.3, 1).
    CHECK_FLOAT_EQ(r.borderColor.x, 0.3f, 1e-5f);

    StyleManager::get().setStyleSheet(nullptr);
}

// AYUI-Perf-2026-08-26: regression for the resolveStyle() memo. The
// public overload caches results by (styleId, themeVersion); a second
// call with the same args must return an equal (by value) ResolvedStyle
// without rebuilding. We also verify the cache invalidates when the
// theme version bumps (StyleSheet mutation or setStyleSheet call).
TEST_CASE(resolve_style_memo_returns_stable_result) {
    StyleManager::get().setStyleSheet(nullptr);
    StyleSheet sheet;
    WidgetStyle custom = StyleBuilder::makeButton();
    custom.backgroundColor = ayt::math::FVector4(0.42f, 0.13f, 0.66f, 1.0f);
    custom.border.cornerRadius = 7.0f;
    sheet.setStyle("memo_test", custom);
    StyleManager::get().setStyleSheet(&sheet);

    const uint64_t v1 = StyleManager::get().getResolveCacheVersion();
    const ResolvedStyle r1 = resolveStyle("memo_test");
    CHECK(r1.hasStyle == true);
    CHECK_FLOAT_EQ(r1.backgroundColor.x, 0.42f, 1e-5f);
    CHECK_FLOAT_EQ(r1.backgroundColor.z, 0.66f, 1e-5f);
    CHECK_FLOAT_EQ(r1.cornerRadius, 7.0f, 1e-5f);

    // Second call: cache hit. The result must be byte-identical.
    const ResolvedStyle r2 = resolveStyle("memo_test");
    CHECK(r2.hasStyle == r1.hasStyle);
    CHECK_FLOAT_EQ(r2.backgroundColor.x, r1.backgroundColor.x, 1e-7f);
    CHECK_FLOAT_EQ(r2.backgroundColor.y, r1.backgroundColor.y, 1e-7f);
    CHECK_FLOAT_EQ(r2.backgroundColor.z, r1.backgroundColor.z, 1e-7f);
    CHECK_FLOAT_EQ(r2.borderWidth, r1.borderWidth, 1e-7f);
    CHECK_FLOAT_EQ(r2.cornerRadius, r1.cornerRadius, 1e-7f);
    CHECK_FLOAT_EQ(r2.borderColor.x, r1.borderColor.x, 1e-7f);

    // Cache version unchanged across cache-hit reads.
    CHECK(StyleManager::get().getResolveCacheVersion() == v1);

    // Mutating the StyleSheet invalidates the cache version, forcing a
    // rebuild on the next resolveStyle() call.
    WidgetStyle alt = custom;
    alt.backgroundColor = ayt::math::FVector4(0.99f, 0.01f, 0.02f, 1.0f);
    sheet.setStyle("memo_test", alt);
    CHECK(StyleManager::get().getResolveCacheVersion() != v1);

    const ResolvedStyle r3 = resolveStyle("memo_test");
    CHECK_FLOAT_EQ(r3.backgroundColor.x, 0.99f, 1e-5f);

    // setStyleSheet(nullptr) must also bump the version so cached
    // results from the prior sheet don't survive a swap to a fresh one.
    StyleManager::get().setStyleSheet(nullptr);
    CHECK(StyleManager::get().getResolveCacheVersion() != v1);

    StyleManager::get().setStyleSheet(nullptr);
}

TEST_SUITE_END