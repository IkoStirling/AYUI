#include "AYTheme.h"
#include "AYStyle.h"
#include <nlohmann/json.hpp>
#include <sstream>
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
    _colorTokens[key] = v;
}

math::FVector4 Theme::getColorToken(const std::string& key) const {
    auto it = _colorTokens.find(key);
    if (it == _colorTokens.end()) {
        return math::FVector4(0.0f, 0.0f, 0.0f, 1.0f);   // sentinel
    }
    return it->second;
}

bool Theme::hasColorToken(const std::string& key) const {
    return _colorTokens.find(key) != _colorTokens.end();
}

void Theme::setFloatToken(const std::string& key, float v) {
    if (key.empty()) return;
    _floatTokens[key] = v;
}

float Theme::getFloatToken(const std::string& key) const {
    auto it = _floatTokens.find(key);
    if (it == _floatTokens.end()) {
        return 0.0f;
    }
    return it->second;
}

bool Theme::hasFloatToken(const std::string& key) const {
    return _floatTokens.find(key) != _floatTokens.end();
}

void Theme::addSheetFragment(const std::string& fragmentName,
                             const StyleSheet& fragment) {
    if (fragmentName.empty()) return;
    _sheetFragments[fragmentName] = fragment;
}

bool Theme::hasSheetFragment(const std::string& fragmentName) const {
    return _sheetFragments.find(fragmentName) != _sheetFragments.end();
}

StyleSheet* Theme::buildComposedSheet() const {
    // Caller owns the returned sheet. Merge order: each fragment's
    // entries overwrite prior ones on style-id clash. Default presets
    // (button_default etc.) come FIRST so fragments can override.
    StyleSheet* out = new StyleSheet();
    // Pre-seed with the default preset so callers don't have to.
    // StyleSheet's ctor already does this; nothing extra to do here.
    for (const auto& kv : _sheetFragments) {
        for (const auto& styleKv : kv.second.getAllStylesForCompose()) {
            out->setStyle(styleKv.first, styleKv.second);
        }
    }
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
                try { vals.push_back(std::stof(item)); }
                catch (...) { return math::FVector4(0.0f, 0.0f, 0.0f, 1.0f); }
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
    auto it = _colorTokens.find(key);
    if (it != _colorTokens.end()) return it->second;
    return math::FVector4(0.0f, 0.0f, 0.0f, 1.0f);
}

// --- ThemeManager ---

ThemeManager& ThemeManager::get() {
    static ThemeManager s_instance;
    return s_instance;
}

void ThemeManager::registerTheme(const std::string& name, const Theme& t) {
    if (name.empty()) return;
    _themes[name] = t;
    // G11 — registerTheme no longer auto-applies. Hosts decide when to
    // activate by calling setActiveTheme(). This avoids overwriting a
    // host-installed StyleSheet when the first theme gets registered.
    if (_activeName.empty()) {
        _activeName = name;
    }
}

void ThemeManager::setActiveTheme(const std::string& name) {
    auto it = _themes.find(name);
    if (it == _themes.end()) return;
    _activeName = name;
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
    if (_activeName.empty()) {
        _activeName = "dark";
        // G11 — no longer auto-applies. Hosts call setActiveTheme("dark")
        // (or any registered theme name) explicitly to install the
        // composed sheet into the StyleManager.
    }
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

// --- Free helper ---

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
    const Theme* active = mgr.getActiveTheme();
    if (active == nullptr) {
        // No theme → fall back to literal-only parse.
        Theme empty;
        return empty.resolveColor(tokenOrLiteral, overrides);
    }
    return active->resolveColor(tokenOrLiteral, overrides);
}

} // namespace ayt::ui
