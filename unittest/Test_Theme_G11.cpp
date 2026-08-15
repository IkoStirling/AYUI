// G11 — Theme / ThemeManager / token expansion / per-widget overrides.
// Each test wipes the StyleManager + ThemeManager to a known state so
// the suite is order-independent.

#include "AYTest.h"
#include "AYUI/Theme.h"
#include "AYUI/Style.h"
#include "AYUI/Widget.h"
#include "AYUI/Panel.h"
#include "AYUI/Button.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Reset both singletons to a known-empty state. Called at the top of
// every test so the suite is order-independent (singleton state from
// a prior case would otherwise leak through resolveStyle).
void resetG11State() {
    StyleManager::get().setStyleSheet(nullptr);
    // ThemeManager doesn't expose a full reset in v1 (intentional —
    // callers shouldn't blow away every registered theme from afar).
    // Tests that need to start clean register fresh themes under
    // fresh names; the canonical "dark" theme stays available.
    ThemeManager::get().clearOnThemeChangedListeners();
    // Make sure default themes are registered AND the dark theme is
    // the active one — earlier tests that called setActiveTheme with
    // a tokenless custom name leave _activeName pointing at a theme
    // that has no tokens, which would make $token lookups silently
    // resolve to (0,0,0,1).
    ThemeManager::get().ensureDefaultThemes();
    ThemeManager::get().setActiveTheme("dark");
}

} // anon

TEST_SUITE(AYUI_Theme_G11)

// ---------- Theme: token CRUD + JSON load ----------

TEST_CASE(theme_color_token_set_get_roundtrip) {
    resetG11State();
    Theme t;
    FVector4 red(1.0f, 0.0f, 0.0f, 1.0f);
    t.setColorToken("color.accent", red);
    CHECK(t.hasColorToken("color.accent") == true);
    CHECK(t.getColorToken("color.accent").x == 1.0f);
    CHECK(t.getColorToken("color.accent").y == 0.0f);
    CHECK(t.colorTokenCount() == 1u);
}

TEST_CASE(theme_get_missing_token_returns_zero) {
    resetG11State();
    Theme t;
    FVector4 v = t.getColorToken("not.there");
    CHECK(v.x == 0.0f);
    CHECK(v.y == 0.0f);
    CHECK(v.z == 0.0f);
    CHECK(v.w == 1.0f);   // sentinel alpha
    CHECK(t.hasColorToken("not.there") == false);
}

TEST_CASE(theme_float_token_set_get_roundtrip) {
    resetG11State();
    Theme t;
    t.setFloatToken("space.sm", 4.0f);
    CHECK(t.hasFloatToken("space.sm") == true);
    CHECK(t.getFloatToken("space.sm") == 4.0f);
    CHECK(t.floatTokenCount() == 1u);
}

TEST_CASE(theme_resolve_literal_color_passthrough) {
    resetG11State();
    Theme t;
    // No $token → resolveColor returns the sentinel (0,0,0,1) because
    // the string isn't a token reference and isn't an array literal.
    // Use the array form for the "literal passes through" check.
    FVector4 v = t.resolveColor("[0.5, 0.5, 0.5, 1]");
    CHECK_FLOAT_EQ(v.x, 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(v.y, 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(v.z, 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(v.w, 1.0f, 1e-5f);
}

TEST_CASE(theme_resolve_dollar_token_expands) {
    resetG11State();
    Theme t;
    t.setColorToken("color.bg", FVector4(0.1f, 0.2f, 0.3f, 1.0f));
    FVector4 v = t.resolveColor("$color.bg");
    CHECK_FLOAT_EQ(v.x, 0.1f, 1e-5f);
    CHECK_FLOAT_EQ(v.y, 0.2f, 1e-5f);
    CHECK_FLOAT_EQ(v.z, 0.3f, 1e-5f);
}

TEST_CASE(theme_resolve_widget_override_wins_over_theme) {
    resetG11State();
    Theme t;
    t.setColorToken("k", FVector4(0.0f, 0.0f, 1.0f, 1.0f));   // blue
    std::unordered_map<std::string, FVector4> overrides;
    overrides["k"] = FVector4(1.0f, 0.0f, 0.0f, 1.0f);        // red
    FVector4 v = t.resolveColor("$k", &overrides);
    CHECK_FLOAT_EQ(v.x, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(v.y, 0.0f, 1e-5f);
}

TEST_CASE(theme_load_from_json_parses_nested_tokens) {
    resetG11State();
    Theme t;
    std::string j = R"({
        "tokens": {
            "color": { "bg": { "surface": [0.10, 0.20, 0.30, 1] } },
            "space": { "sm": 4.0 }
        }
    })";
    CHECK(t.loadFromJson(j) == true);
    CHECK(t.hasColorToken("color.bg.surface") == true);
    FVector4 v = t.getColorToken("color.bg.surface");
    CHECK_FLOAT_EQ(v.x, 0.10f, 1e-5f);
    CHECK_FLOAT_EQ(v.y, 0.20f, 1e-5f);
    CHECK(t.hasFloatToken("space.sm") == true);
    CHECK_FLOAT_EQ(t.getFloatToken("space.sm"), 4.0f, 1e-5f);
}

// ---------- ThemeManager: register / activate / listeners ----------

TEST_CASE(thememanager_register_and_set_active) {
    resetG11State();
    ThemeManager& mgr = ThemeManager::get();
    Theme dark;
    dark.setColorToken("k", FVector4(0.0f, 0.0f, 0.0f, 1.0f));
    Theme light;
    light.setColorToken("k", FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    mgr.registerTheme("g11_dark_1", dark);
    mgr.registerTheme("g11_light_1", light);
    mgr.setActiveTheme("g11_light_1");
    CHECK(mgr.getActiveThemeName() == "g11_light_1");
    const Theme* active = mgr.getActiveTheme();
    CHECK(active != nullptr);
    CHECK(active->getColorToken("k").x == 1.0f);
}

TEST_CASE(thememanager_set_active_unknown_is_noop) {
    resetG11State();
    ThemeManager& mgr = ThemeManager::get();
    Theme t;
    mgr.registerTheme("g11_known", t);
    mgr.setActiveTheme("g11_known");
    mgr.setActiveTheme("g11_nonexistent");
    // Active theme should still be "g11_known" — no crash, no swap.
    CHECK(mgr.getActiveThemeName() == "g11_known");
}

TEST_CASE(thememanager_active_theme_callback_fires_on_swap) {
    resetG11State();
    ThemeManager& mgr = ThemeManager::get();
    mgr.registerTheme("g11_cb_a", Theme{});
    mgr.registerTheme("g11_cb_b", Theme{});
    std::vector<std::string> recorded;
    mgr.addOnThemeChanged([&](const std::string& name) {
        recorded.push_back(name);
    });
    mgr.setActiveTheme("g11_cb_b");
    CHECK(recorded.size() == 1u);
    CHECK(recorded[0] == "g11_cb_b");
    mgr.setActiveTheme("g11_cb_a");
    CHECK(recorded.size() == 2u);
    CHECK(recorded[1] == "g11_cb_a");
}

// ---------- StyleSheet: token expansion in JSON ----------

TEST_CASE(stylesheet_parse_property_dollar_prefix_expands) {
    resetG11State();
    // ensureDefaultThemes() ran when StyleManager ctor fired earlier;
    // the dark theme has color.bg.surface = (0.10, 0.10, 0.12, 1).
    const char* sheetJson = R"({
        "styles": {
            "g11_token_bg": { "backgroundColor": "$color.bg.surface" }
        }
    })";
    StyleSheet sheet;
    CHECK(sheet.loadFromString(sheetJson, std::strlen(sheetJson)) == true);
    const WidgetStyle* s = sheet.getStyle("g11_token_bg");
    CHECK(s != nullptr);
    // backgroundColor should be the active theme's color.bg.surface.
    CHECK_FLOAT_EQ(s->backgroundColor.x, 0.10f, 1e-3f);
    // Token ref is recorded for later override application.
    CHECK(s->bgToken == "color.bg.surface");
}

// ---------- Widget: token override + JSON round-trip ----------

TEST_CASE(widget_token_override_set_clear_roundtrip) {
    resetG11State();
    Button btn;
    FVector4 red(1.0f, 0.0f, 0.0f, 1.0f);
    btn.setStyleTokenOverride("color.accent", red);
    CHECK(btn.hasStyleTokenOverride("color.accent") == true);
    CHECK(btn.hasStyleTokenOverride("color.does.not.exist") == false);
    CHECK(btn.getStyleTokenOverrides().size() == 1u);
    btn.clearStyleTokenOverrides();
    CHECK(btn.hasStyleTokenOverride("color.accent") == false);
    CHECK(btn.getStyleTokenOverrides().empty());
}

TEST_CASE(widget_token_override_accepts_dollar_prefix) {
    resetG11State();
    Button btn;
    FVector4 red(1.0f, 0.0f, 0.0f, 1.0f);
    // setStyleTokenOverride accepts both "$foo" and bare "foo" input.
    btn.setStyleTokenOverride("$color.accent", red);
    // hasStyleTokenOverride normalizes input — both forms find it.
    CHECK(btn.hasStyleTokenOverride("color.accent") == true);
    CHECK(btn.hasStyleTokenOverride("$color.accent") == true);
}

TEST_CASE(widget_style_overrides_round_trip_json) {
    resetG11State();
    json j;
    j["type"] = "Button";
    j["style"] = "primary";
    j["styleOverrides"] = {
        { "color.bg", { 0.5f, 0.5f, 0.5f, 1.0f } }
    };
    Widget* w = WidgetSerializer::deserialize(j.dump());
    CHECK(w != nullptr);
    if (w) {
        CHECK(w->getStyleId() == "primary");
        CHECK(w->hasStyleTokenOverride("color.bg") == true);
        FVector4 c = w->getStyleTokenOverrides().at("color.bg");
        CHECK_FLOAT_EQ(c.x, 0.5f, 1e-5f);

        // Round-trip back to JSON.
        std::string j2str = WidgetSerializer::serializeWidget(w);
        json j2 = json::parse(j2str);
        CHECK(j2.contains("styleOverrides") == true);
        CHECK(j2["styleOverrides"].is_object() == true);
        CHECK(j2["styleOverrides"].contains("color.bg") == true);
        CHECK_FLOAT_EQ(j2["styleOverrides"]["color.bg"][0].get<float>(),
                       0.5f, 1e-5f);
        destroyWidgetTree(w);
    }
}

TEST_CASE(widget_resolve_style_applies_token_overrides) {
    resetG11State();
    // Sheet whose bg is $color.bg.surface (dark theme default).
    const char* sheetJson = R"({
        "styles": {
            "g11_over": { "backgroundColor": "$color.bg.surface" }
        }
    })";
    StyleSheet* sheet = new StyleSheet();
    sheet->loadFromString(sheetJson, std::strlen(sheetJson));
    StyleManager::get().setStyleSheet(sheet);

    Button btn;
    btn.setStyleId("g11_over");
    // Default: dark theme color.bg.surface = (0.10, 0.10, 0.12, 1).
    {
        ResolvedStyle r = resolveStyle("g11_over", &btn);
        CHECK(r.hasStyle == true);
        CHECK_FLOAT_EQ(r.backgroundColor.x, 0.10f, 1e-3f);
    }
    // Now override the token — bg should follow.
    btn.setStyleTokenOverride("color.bg.surface",
                              FVector4(0.7f, 0.0f, 0.0f, 1.0f));
    {
        ResolvedStyle r = resolveStyle("g11_over", &btn);
        CHECK(r.hasStyle == true);
        CHECK_FLOAT_EQ(r.backgroundColor.x, 0.7f, 1e-3f);
        CHECK_FLOAT_EQ(r.backgroundColor.y, 0.0f, 1e-3f);
    }
    StyleManager::get().setStyleSheet(nullptr);
    delete sheet;
}

TEST_CASE(thememanager_apply_to_style_manager_swaps_sheet) {
    resetG11State();
    ThemeManager& mgr = ThemeManager::get();
    Theme custom;
    custom.setColorToken("custom.k", FVector4(0.42f, 0.42f, 0.42f, 1.0f));
    mgr.registerTheme("g11_apply", custom);
    mgr.setActiveTheme("g11_apply");
    // After setActiveTheme the StyleManager has the composed sheet.
    // Loading a stylesheet JSON that uses $custom.k should resolve.
    const char* sheetJson = R"({
        "styles": { "g11_apply_t": { "backgroundColor": "$custom.k" } }
    })";
    StyleManager::get().getStyleSheet()->loadFromString(
        sheetJson, std::strlen(sheetJson));
    const WidgetStyle* s = StyleManager::get().getStyle("g11_apply_t");
    CHECK(s != nullptr);
    if (s) {
        CHECK_FLOAT_EQ(s->backgroundColor.x, 0.42f, 1e-3f);
    }
    // Cleanup — leave StyleManager at nullptr so subsequent tests
    // don't see this composed sheet.
    StyleManager::get().setStyleSheet(nullptr);
}

TEST_SUITE_END