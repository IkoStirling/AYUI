#include "AYUI/Theme.h"
#include "AYUI/Style.h"
#include <nlohmann/json.hpp>
#include <charconv>
#include <cmath>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace ayt::ui {

namespace {

using json = nlohmann::json;

// Recursive flatten: { "color": { "bg": { "surface": [r,g,b,a] } } } →
// "_colorTokens["color.bg.surface"] = (r,g,b,a)". Returns count of
// entries written so the caller can decide "did anything parse?".
size_t flattenColorJson(
    const json& j,
    const std::string& prefix,
    std::unordered_map<std::string, math::FVector4>& out)
{
    size_t count = 0;
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string key = prefix.empty()
                ? it.key()
                : (prefix + "." + it.key());
            count += flattenColorJson(it.value(), key, out);
        }
    } else if (j.is_array() && j.size() == 4 &&
               j[0].is_number() && j[1].is_number() &&
               j[2].is_number() && j[3].is_number()) {
        out[prefix] = math::FVector4(
            j[0].get<float>(), j[1].get<float>(),
            j[2].get<float>(), j[3].get<float>());
        count = 1;
    } else if (j.is_number()) {
        // Single-float token — promote to a grayscale color for
        // convenience so a host can say `"alpha": 0.5` if they want.
        // Uncommon path; float tokens have their own section.
    }
    return count;
}

size_t flattenFloatJson(
    const json& j,
    const std::string& prefix,
    std::unordered_map<std::string, float>& out)
{
    size_t count = 0;
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string key = prefix.empty()
                ? it.key()
                : (prefix + "." + it.key());
            count += flattenFloatJson(it.value(), key, out);
        }
    } else if (j.is_number()) {
        out[prefix] = j.get<float>();
        count = 1;
    }
    return count;
}

} // anon

// --- Theme ---

bool Theme::loadFromJson(const std::string& jsonStr) {
    if (jsonStr.empty()) return false;
    try {
        json j = json::parse(jsonStr);
        if (!j.is_object()) return false;

        // Wipe prior content so a re-load is a clean swap.
        _colorTokens.clear();
        _floatTokens.clear();
        _sheetFragments.clear();
        _parentThemeName = j.value("extends", std::string());

        if (j.contains("tokens") && j["tokens"].is_object()) {
            const auto& toks = j["tokens"];
            if (toks.contains("color") && toks["color"].is_object()) {
                flattenColorJson(toks["color"], std::string("color"),
                                 _colorTokens);
            }
            if (toks.contains("space") && toks["space"].is_object()) {
                flattenFloatJson(toks["space"], std::string("space"),
                                 _floatTokens);
            }
            // Also accept top-level numeric tokens like
            // `"radius.md": 4.0` for hosts that prefer flat layouts.
            for (auto it = toks.begin(); it != toks.end(); ++it) {
                const auto& k = it.key();
                if (k == "color" || k == "space") continue;
                if (it.value().is_number()) {
                    _floatTokens[k] = it.value().get<float>();
                }
            }
        }

        if (j.contains("sheets") && j["sheets"].is_object()) {
            const auto& sheets = j["sheets"];
            for (auto it = sheets.begin(); it != sheets.end(); ++it) {
                StyleSheet fragment;
                // Each fragment is a small StyleSheet JSON
                // { "styles": { "name": {...} } }
                std::string fragJson = it.value().dump();
                fragment.loadFromString(fragJson.data(), fragJson.size());
                _sheetFragments[it.key()] = std::move(fragment);
            }
            // Code-review Sweep3-#6: sheet fragments are eagerly
            // parsed at loadFromJson time, which means $token refs in
            // fragment styles are expanded against the ACTIVE theme at
            // load time. After a setActiveTheme() swap, the fragment's
            // already-parsed WidgetStyle.backgroundColor / border.color
            // / textColor are STALE — they hold the old theme's
            // resolved values.
            //
            // The fix in resolveStyle() (Sweep3-#3) sidesteps this for
            // token-captured slots: the resolver re-expands against the
            // active theme on every call, so widgets always see live
            // values. The literal slots in a fragment, however, are
            // frozen. Hosts that need fully-dynamic sheets should keep
            // token refs in their fragment JSON, not literal arrays, so
            // the resolver path applies.
            //
            // Future work: store raw fragment JSON and re-parse inside
            // buildComposedSheet against the currently active theme.
            // Currently out of scope; the Sweep3-#3 fix mitigates the
            // user-visible impact.
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void Theme::setColorToken(const std::string& key, const math::FVector4& v) {
    if (key.empty()) return;
    const bool changed = _colorTokens.find(key) == _colorTokens.end()
        || _colorTokens[key] != v;
    _colorTokens[key] = v;
    // Audit M-R-3: a token mutation can change the resolved output of
    // every cached (styleId, state) entry that captured this token as a
    // $foo reference. Bump the StyleManager's resolveCacheVersion so the
    // next resolveStyle() call rebuilds entries instead of serving
    // stale theme materialised colours.
    if (changed) {
        StyleManager::get().invalidateResolveCache();
    }
}

math::FVector4 Theme::getColorToken(const std::string& key) const {
    std::unordered_set<const Theme*> visited;
    const Theme* current = this;
    while (current != nullptr && visited.insert(current).second) {
        auto it = current->_colorTokens.find(key);
        if (it != current->_colorTokens.end()) return it->second;
        current = current->_parentThemeName.empty()
            ? nullptr : ThemeManager::get().getTheme(current->_parentThemeName);
    }
    return math::FVector4(0.0f, 0.0f, 0.0f, 1.0f);
}

bool Theme::hasColorToken(const std::string& key) const {
    std::unordered_set<const Theme*> visited;
    const Theme* current = this;
    while (current != nullptr && visited.insert(current).second) {
        if (current->_colorTokens.find(key) != current->_colorTokens.end()) return true;
        current = current->_parentThemeName.empty()
            ? nullptr : ThemeManager::get().getTheme(current->_parentThemeName);
    }
    return false;
}

void Theme::setFloatToken(const std::string& key, float v) {
    if (key.empty()) return;
    const bool changed = _floatTokens.find(key) == _floatTokens.end()
        || _floatTokens[key] != v;
    _floatTokens[key] = v;
    // Audit M-R-3: float tokens feed resolveFloat(), which is invoked
    // from widget-side resolvers (spacing, radii). Theme mutations do
    // not flow through Theme::loadFromJson (which already bumps via
    // StyleManager::setStyleSheet) so the cache version would otherwise
    // stay pinned to the pre-mutation snapshot.
    if (changed) {
        StyleManager::get().invalidateResolveCache();
    }
}

float Theme::getFloatToken(const std::string& key) const {
    std::unordered_set<const Theme*> visited;
    const Theme* current = this;
    while (current != nullptr && visited.insert(current).second) {
        auto it = current->_floatTokens.find(key);
        if (it != current->_floatTokens.end()) return it->second;
        current = current->_parentThemeName.empty()
            ? nullptr : ThemeManager::get().getTheme(current->_parentThemeName);
    }
    return 0.0f;
}

bool Theme::hasFloatToken(const std::string& key) const {
    std::unordered_set<const Theme*> visited;
    const Theme* current = this;
    while (current != nullptr && visited.insert(current).second) {
        if (current->_floatTokens.find(key) != current->_floatTokens.end()) return true;
        current = current->_parentThemeName.empty()
            ? nullptr : ThemeManager::get().getTheme(current->_parentThemeName);
    }
    return false;
}

void Theme::addSheetFragment(const std::string& fragmentName,
                             const StyleSheet& fragment) {
    if (fragmentName.empty()) return;
    _sheetFragments[fragmentName] = fragment;
}

bool Theme::hasSheetFragment(const std::string& fragmentName) const {
    std::unordered_set<const Theme*> visited;
    const Theme* current = this;
    while (current != nullptr && visited.insert(current).second) {
        if (current->_sheetFragments.find(fragmentName)
            != current->_sheetFragments.end()) return true;
        current = current->_parentThemeName.empty()
            ? nullptr : ThemeManager::get().getTheme(current->_parentThemeName);
    }
    return false;
}

StyleSheet* Theme::buildComposedSheet() const {
    // Caller owns the returned sheet. Merge order: each fragment's
    // entries overwrite prior ones on style-id clash. Default presets
    // (button_default etc.) come FIRST so fragments can override.
    StyleSheet* out = new StyleSheet();
    // Pre-seed with the default preset so callers don't have to.
    // StyleSheet's ctor already does this; nothing extra to do here.
    std::unordered_set<const Theme*> visited;
    std::function<void(const Theme*)> append = [&](const Theme* theme) {
        if (theme == nullptr || !visited.insert(theme).second) return;
        if (!theme->_parentThemeName.empty()) {
            append(ThemeManager::get().getTheme(theme->_parentThemeName));
        }
        for (const auto& kv : theme->_sheetFragments) {
            for (const auto& styleKv : kv.second.getAllStylesForCompose()) {
                out->setStyle(styleKv.first, styleKv.second);
            }
        }
    };
    append(this);
    return out;
}

math::FVector4 Theme::resolveColor(
    const std::string& tokenOrLiteral,
    const std::unordered_map<std::string, math::FVector4>* overrides) const
{
    if (tokenOrLiteral.empty()) {
        return math::FVector4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (tokenOrLiteral[0] != '$') {
        // Not a token — try to parse as a literal "[r,g,b,a]" array.
        // Cheap parse: strip brackets, split on commas.
        std::string s = tokenOrLiteral;
        if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
            s = s.substr(1, s.size() - 2);
            std::vector<float> vals;
            std::stringstream ss(s);
            std::string item;
            while (std::getline(ss, item, ',')) {
                // Strip leading + trailing ASCII whitespace. std::getline
                // includes the delimiter-trailing characters in `item`
                // (e.g. "0.5 " for the comma-split of "[0.5, 0.5, ...]"),
                // and std::from_chars does NOT skip leading whitespace
                // the way std::stof does — so without this trim, every
                // comma-separated token after the first would fail to
                // parse and we'd fall back to (0,0,0,1).
                const auto first = item.find_first_not_of(" \t\r\n");
                const auto last = item.find_last_not_of(" \t\r\n");
                if (first == std::string::npos) {
                    // Empty token — treat as 0 (defensive; matches the
                    // previous std::stof behaviour for an empty slice).
                    vals.push_back(0.0f);
                    continue;
                }
                const std::size_t start = first;
                const std::size_t end = last + 1;
                // H-S-3: std::stof accepts "1e9999" → +inf, "NaN", and
                // other float specials that the renderer then propagates
                // as raw bits to the GPU. std::from_chars rejects those
                // (returns ec == errc::result_out_of_range) so a
                // malicious style JSON can't poison downstream draws
                // with NaN gradients. Also avoids the locale-dependent
                // overhead of std::stof which silently ignores trailing
                // non-numeric garbage.
                float v = 0.0f;
                auto [ptr, ec] = std::from_chars(
                    item.data() + start, item.data() + end, v);
                if (ec != std::errc() || ptr != item.data() + end) {
                    return math::FVector4(0.0f, 0.0f, 0.0f, 1.0f);
                }
                if (!std::isfinite(v)) {
                    return math::FVector4(0.0f, 0.0f, 0.0f, 1.0f);
                }
                vals.push_back(v);
            }
            if (vals.size() == 4) {
                return math::FVector4(vals[0], vals[1], vals[2], vals[3]);
            }
        }
        return math::FVector4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    // Strip leading '$'
    const std::string key = tokenOrLiteral.substr(1);
    if (overrides) {
        auto oit = overrides->find(key);
        if (oit != overrides->end()) return oit->second;
    }
    return getColorToken(key);
}

// --- ThemeManager ---

ThemeManager& ThemeManager::get() {
    static ThemeManager s_instance;
    return s_instance;
}

void ThemeManager::registerTheme(const std::string& name, const Theme& t) {
    if (name.empty()) return;
    _themes[name] = t;
    // H-3 follow-up: registerTheme no longer auto-fills _activeName either.
    // Hosts must call setActiveTheme() explicitly. The previous
    // "first registered = active" convenience caused resolveAccentColor
    // and similar token-aware call sites to silently change color the
    // first time any module initialised a default theme — a hard
    // coupling between theme registry state and every widget's draw
    // output. Activating now requires an explicit host decision, which
    // keeps the historical literal-colour baseline stable until a
    // host opts into theming.
}

void ThemeManager::setActiveTheme(const std::string& name) {
    auto it = _themes.find(name);
    if (it == _themes.end()) return;
    _activeName = name;
    // H-3 follow-up: latch explicit-activation so resolveAccentColor
    // can distinguish "the host asked for theming" from "the default
    // theme happens to be registered". Without this latch, widgets
    // would silently change color the moment any code path triggers
    // ensureDefaultThemes() — a coupling nobody signed off on.
    _explicitActiveSet = true;
    applyActiveToStyleManager();
    for (auto& cb : _listeners) {
        cb(name);
    }
}

void ThemeManager::ensureDefaultThemes() {
    // Dark theme — shipped palette. Hosts can register more themes and
    // swap via setActiveTheme().
    if (_themes.find("dark") == _themes.end()) {
        Theme dark;
        dark.setColorToken("color.bg.surface",  math::FVector4(0.10f, 0.10f, 0.12f, 1.0f));
        dark.setColorToken("color.bg.elevated",  math::FVector4(0.16f, 0.16f, 0.18f, 1.0f));
        dark.setColorToken("color.text.primary", math::FVector4(0.92f, 0.92f, 0.95f, 1.0f));
        dark.setColorToken("color.text.muted",   math::FVector4(0.65f, 0.65f, 0.70f, 1.0f));
        dark.setColorToken("color.border",       math::FVector4(0.30f, 0.30f, 0.34f, 1.0f));
        dark.setColorToken("color.accent",       math::FVector4(0.30f, 0.55f, 0.95f, 1.0f));
        dark.setColorToken("color.accent.hover", math::FVector4(0.40f, 0.65f, 1.00f, 1.0f));
        dark.setColorToken("color.danger",       math::FVector4(0.85f, 0.30f, 0.30f, 1.0f));
        dark.setFloatToken("space.xs", 2.0f);
        dark.setFloatToken("space.sm", 4.0f);
        dark.setFloatToken("space.md", 8.0f);
        dark.setFloatToken("space.lg", 16.0f);
        dark.setFloatToken("radius.sm", 2.0f);
        dark.setFloatToken("radius.md", 4.0f);
        dark.setFloatToken("radius.lg", 8.0f);
        _themes["dark"] = dark;   // copy (not move) — the local goes out
                                  // of scope immediately after.
    }
    if (_themes.find("light") == _themes.end()) {
        Theme light;
        light.setColorToken("color.bg.surface",  math::FVector4(0.96f, 0.96f, 0.97f, 1.0f));
        light.setColorToken("color.bg.elevated",  math::FVector4(1.00f, 1.00f, 1.00f, 1.0f));
        light.setColorToken("color.text.primary", math::FVector4(0.10f, 0.10f, 0.12f, 1.0f));
        light.setColorToken("color.text.muted",   math::FVector4(0.40f, 0.40f, 0.45f, 1.0f));
        light.setColorToken("color.border",       math::FVector4(0.78f, 0.78f, 0.82f, 1.0f));
        light.setColorToken("color.accent",       math::FVector4(0.20f, 0.45f, 0.85f, 1.0f));
        light.setColorToken("color.accent.hover", math::FVector4(0.25f, 0.50f, 0.95f, 1.0f));
        light.setColorToken("color.danger",       math::FVector4(0.80f, 0.20f, 0.20f, 1.0f));
        light.setFloatToken("space.xs", 2.0f);
        light.setFloatToken("space.sm", 4.0f);
        light.setFloatToken("space.md", 8.0f);
        light.setFloatToken("space.lg", 16.0f);
        light.setFloatToken("radius.sm", 2.0f);
        light.setFloatToken("radius.md", 4.0f);
        light.setFloatToken("radius.lg", 8.0f);
        _themes["light"] = std::move(light);
    }
    // H-3 follow-up: ensureDefaultThemes no longer auto-activates the
    // "dark" theme. Auto-activation made resolveAccentColor return the
    // theme's token value (0.30, 0.55, 0.95) even when the host never
    // asked for theming, which silently broke the historical visual
    // baseline (0.18, 0.45, 0.78). Hosts must now call
    // setActiveTheme("dark") explicitly to opt into token-driven
    // accent color.
}

const Theme* ThemeManager::getActiveTheme() const {
    if (_activeName.empty()) return nullptr;
    auto it = _themes.find(_activeName);
    return (it == _themes.end()) ? nullptr : &it->second;
}

const Theme* ThemeManager::getTheme(const std::string& name) const {
    auto it = _themes.find(name);
    return (it == _themes.end()) ? nullptr : &it->second;
}

void ThemeManager::addOnThemeChanged(ThemeChangedCallback cb) {
    if (cb) _listeners.push_back(std::move(cb));
}

void ThemeManager::clearOnThemeChangedListeners() {
    _listeners.clear();
}

void ThemeManager::applyActiveToStyleManager() {
    const Theme* t = getActiveTheme();
    if (t == nullptr) return;
    StyleSheet* sheet = t->buildComposedSheet();
    StyleManager::get().setStyleSheet(sheet);
}

void ThemeManager::clearActiveThemeForTest() {
    // H-3 follow-up: tests that call setActiveTheme() (G11 / Gallery /
    // Productization) used to leak that state into every later suite,
    // because ThemeManager is a process-wide singleton and there was
    // no teardown hook. Now suites that mutate the active theme are
    // expected to clear it at teardown so subsequent suites — and any
    // widget that resolves a token via resolveAccentColor() — observe
    // the same baseline they would have before the test ran.
    _activeName.clear();
    _explicitActiveSet = false;
}

// --- Free helper ---

// H-3: resolveAccentColor. The audit found 11 control files hard-coding
// FVector4(0.18f, 0.45f, 0.78f, ..) as the accent color, defeating Theme
// swap. This wrapper routes every such call site through the active
// theme's "color.accent" token. Fallback policy:
//   * No active theme at all -> bit-identical historical default (so
//     first-boot screenshots stay stable; existing visual baselines are
//     unaffected when the host never installs a Theme).
//   * Active theme present but no "color.accent" key -> same fallback
//     (preserves behaviour for themes that don't define the token).
//   * Active theme defines the token -> token value (the whole point).
math::FVector4 resolveAccentColor(float alpha) {
    // H-3 follow-up: consult the active theme's "color.accent" token
    // when one has been registered AND explicitly activated via
    // setActiveTheme(). Without the explicit-activation latch, the
    // first module to call ensureDefaultThemes() would flip every
    // accent fill from the historical (0.18, 0.45, 0.78) to the
    // default "dark" theme's (0.30, 0.55, 0.95) — a silent visual
    // regression nobody asked for. The historical literal stays in
    // effect until a host opts into theming by calling
    // setActiveTheme().
    //
    // Tests: ThemeManager is a process-wide singleton, so a suite
    // that activates a theme will leak the active state into later
    // suites. Tests that depend on the historical literal must call
    // ThemeManager::clearActiveThemeForTest() at setup; tests that
    // depend on token resolution must call setActiveTheme("dark") (or
    // similar) at setup. resetG11State() handles this for the G11 /
    // Gallery / Productization suites; visual-baseline suites in the
    // unittest/ folder do not currently need the cleanup because none
    // of them touches the theme registry, but the latch exists so
    // that contract stays true if new tests do.
    ThemeManager& mgr = ThemeManager::get();
    mgr.ensureDefaultThemes();
    math::FVector4 c(0.18f, 0.45f, 0.78f, alpha);
    if (mgr.hasExplicitActiveTheme()) {
        const Theme* active = mgr.getActiveTheme();
        if (active != nullptr && active->hasColorToken("color.accent")) {
            c = active->getColorToken("color.accent");
            c.w = alpha;
        }
    }
    return c;
}

math::FVector4 expandColorToken(
    const std::string& tokenOrLiteral,
    const std::unordered_map<std::string, math::FVector4>* overrides)
{
    // Lazy-init default themes — same rationale as StyleManager::getStyle:
    // avoid static-init ordering hazards (ThemeManager singleton may not
    // be wired when StyleSheet::loadFromString fires during another
    // module's static init).
    ThemeManager& mgr = ThemeManager::get();
    mgr.ensureDefaultThemes();
    // H-3 follow-up: same explicit-activation latch as resolveAccentColor.
    // When no theme has been explicitly activated via setActiveTheme(),
    // we honour only literal color strings; $token refs resolve to the
    // empty-theme fallback so StyleSheet::loadFromString of a JSON
    // fragment that references "color.bg.surface" returns (0,0,0,1)
    // instead of silently adopting the default theme's value. This
    // keeps hosts that haven't opted into theming on a deterministic
    // baseline.
    if (!mgr.hasExplicitActiveTheme()) {
        Theme empty;
        return empty.resolveColor(tokenOrLiteral, overrides);
    }
    const Theme* active = mgr.getActiveTheme();
    if (active == nullptr) {
        Theme empty;
        return empty.resolveColor(tokenOrLiteral, overrides);
    }
    return active->resolveColor(tokenOrLiteral, overrides);
}

} // namespace ayt::ui
