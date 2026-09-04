#pragma once

#include "AYMath/MathTypes.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace ayt::ui {

class StyleSheet;

// G11 — Theme. A named collection of:
//   * color tokens  — named RGBA values ("color.bg.surface" → (0.1, 0.1, 0.1, 1))
//   * float tokens  — named scalars     ("space.sm"        → 4.0)
//   * sheet fragments — StyleSheet fragments, merged when applied.
//
// Token references in WidgetStyle fields use the `$tokenName` syntax.
// `resolveColor()` expands them against the active theme's token map
// (or against a caller-supplied override map for per-widget overrides).
//
// JSON shape:
//   {
//     "tokens": {
//       "color": { "bg": { "surface": [0.10, 0.10, 0.10, 1] }, ... },
//       "space": { "sm": 4.0 }
//     },
//     "sheets": { "button": { "styles": { "my_btn": { ... } } } }
//   }
//
// The JSON token layout mirrors CSS custom-property namespaces (nested
// objects) for readability, but at storage time we flatten to dot-
// separated keys ("color.bg.surface") so resolveColor() can look them
// up in O(1).
class Theme {
public:
    Theme() = default;
    ~Theme() = default;

    // Build from JSON. Replaces any previous content. Returns true on
    // parse success (any malformed token is skipped, NOT fatal — the
    // editor must keep going on partial JSON).
    bool loadFromJson(const std::string& json);

    // Named inheritance. The parent is resolved lazily through
    // ThemeManager, so registry copies/replacements cannot leave dangling
    // pointers. Child tokens and sheet fragments override parent values.
    void setParentThemeName(const std::string& name) { _parentThemeName = name; }
    const std::string& getParentThemeName() const { return _parentThemeName; }

    // Color tokens.
    void setColorToken(const std::string& key, const math::FVector4& value);
    math::FVector4 getColorToken(const std::string& key) const;
    bool hasColorToken(const std::string& key) const;
    size_t colorTokenCount() const { return _colorTokens.size(); }

    // Float tokens.
    void setFloatToken(const std::string& key, float value);
    float getFloatToken(const std::string& key) const;
    bool hasFloatToken(const std::string& key) const;
    size_t floatTokenCount() const { return _floatTokens.size(); }

    // Sheet fragments — keyed by fragment name. Multiple fragments can
    // be added; on apply, the composed StyleSheet is the parent-first union
    // (child wins on style-id clash). hasSheetFragment follows inheritance.
    void addSheetFragment(const std::string& fragmentName, const StyleSheet& fragment);
    bool hasSheetFragment(const std::string& fragmentName) const;
    size_t sheetFragmentCount() const { return _sheetFragments.size(); }

    // Build a composed StyleSheet from all fragments + the default
    // StyleBuilder presets. Caller owns the returned pointer.
    StyleSheet* buildComposedSheet() const;

    // Resolve a string that's either a "$token" reference or a literal
    // "[r,g,b,a]" array. The default literal syntax matches what
    // StyleSheet::loadFromString expects for backgroundColor / etc.
    //
    // `overrides` is an optional per-call map (used by Widget::resolveStyle
    // when the widget has setStyleTokenOverride entries). Widget-level
    // overrides win over the theme.
    math::FVector4 resolveColor(
        const std::string& tokenOrLiteral,
        const std::unordered_map<std::string, math::FVector4>* overrides = nullptr) const;

private:
    std::unordered_map<std::string, math::FVector4> _colorTokens;
    std::unordered_map<std::string, float>          _floatTokens;
    std::unordered_map<std::string, StyleSheet>     _sheetFragments;
    std::string _parentThemeName;
};

// G11 — ThemeManager. Singleton registry of named themes + the "active"
// theme pointer. Hosts wire setActiveTheme() to swap skins; the manager
// applies the composed sheet to the global StyleManager and fires an
// onThemeChanged callback so widgets can re-resolve their styles.
class ThemeManager {
public:
    using ThemeChangedCallback = std::function<void(const std::string&)>;

    static ThemeManager& get();

    // Register a theme under `name`. Idempotent — re-registering with
    // the same name replaces the prior entry.
    void registerTheme(const std::string& name, const Theme& t);

    // Switch the active theme. If `name` is unknown, no-op + no crash.
    // Fires onThemeChanged() AFTER the StyleManager's sheet is swapped
    // so listeners that consult the manager see consistent state.
    void setActiveTheme(const std::string& name);

    // G11 default — register a built-in "dark" theme if no theme has
    // been registered yet. Hosts that want light-only can call
    // setActiveTheme("light") AFTER registerTheme.
    void ensureDefaultThemes();

    const Theme* getActiveTheme() const;
    const Theme* getTheme(const std::string& name) const;
    std::string getActiveThemeName() const { return _activeName; }

    // Listener. Multiple listeners supported.
    void addOnThemeChanged(ThemeChangedCallback cb);
    void clearOnThemeChangedListeners();

    // Pushes the active theme's composed sheet into the global
    // StyleManager. Called automatically by setActiveTheme().
    void applyActiveToStyleManager();

    // H-3 follow-up: returns true when an active theme has been
    // explicitly selected via setActiveTheme(). Used by
    // resolveAccentColor() to decide whether the token value or the
    // historical literal (0.18, 0.45, 0.78) is the source of truth.
    // Without this flag, the first ThemeManager::ensureDefaultThemes()
    // call (from any module's static init) would silently flip every
    // accent-filled widget from the legacy blue to the "dark" theme's
    // brighter blue — a breaking visual change nobody asked for.
    bool hasExplicitActiveTheme() const { return _explicitActiveSet; }

    // Reset the manager to "no active theme" — test-only helper.
    // Production code should not need this; ThemeManager is a
    // process-wide singleton and tests that poke setActiveTheme()
    // should restore the previous state at teardown so other suites
    // don't observe a stale theme.
    void clearActiveThemeForTest();

private:
    ThemeManager() = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    std::unordered_map<std::string, Theme> _themes;
    std::string _activeName;
    // H-3 follow-up: latched true by setActiveTheme() and reset by
    // clearActiveThemeForTest(). Read by hasExplicitActiveTheme()
    // (and through it, by resolveAccentColor) to decide whether the
    // historical literal default is still in effect.
    bool _explicitActiveSet = false;
    std::vector<ThemeChangedCallback> _listeners;
};

// G11 — Token expansion helper used by resolveStyle(). Returns the
// expanded color or the literal (if the input doesn't start with '$').
// ThemeManager-aware: looks up tokens in the currently active theme.
math::FVector4 expandColorToken(
    const std::string& tokenOrLiteral,
    const std::unordered_map<std::string, math::FVector4>* overrides = nullptr);

} // namespace ayt::ui
