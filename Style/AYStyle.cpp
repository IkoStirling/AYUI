#include "AYStyle.h"
#include "AYTheme.h"
#include "AYWidget.h"
#include "aymath/MathTypes.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace ayt::ui {

namespace {

using json = nlohmann::json;

// G11 — resolve a JSON color value. Three accepted shapes:
//   * 4-element numeric array  — `[r, g, b, a]`, copied through verbatim.
//   * bare string starting '$' — token reference, expanded via Theme.
//   * anything else             — caller falls back to default.
//
// Token expansion uses the active theme (ThemeManager::get().getActiveTheme()).
// Per-widget overrides are NOT consulted here — StyleSheet parsing happens
// before any widget exists. Widget overrides get applied at resolveStyle()
// time (see below) by re-expanding the chosen style's color slots.
//
// The fourth out-parameter is filled in with the bare token name (no
// leading '$') when the value was a `$token` reference, OR left empty
// for literal arrays. WidgetStyle stores this so a later widget-level
// override can find the slot.
math::FVector4 parseColorJson(const json& v, std::string& outTokenRef) {
    outTokenRef.clear();
    if (v.is_array() && v.size() == 4 &&
        v[0].is_number() && v[1].is_number() &&
        v[2].is_number() && v[3].is_number()) {
        return math::FVector4(
            v[0].get<float>(), v[1].get<float>(),
            v[2].get<float>(), v[3].get<float>());
    }
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        if (!s.empty() && s[0] == '$') {
            outTokenRef = s.substr(1);
            return expandColorToken(s);
        }
    }
    return math::FVector4(0.0f, 0.0f, 0.0f, 1.0f);
}

// Single-output overload used by callers that don't care about the
// original token name (compositionUnderlineColor / placeholderColor
// fall through here because WidgetStyle doesn't expose a token slot
// for them).
math::FVector4 parseColorJson(const json& v) {
    std::string dummy;
    return parseColorJson(v, dummy);
}

// Parse a single WidgetStyle from a JSON object. Tolerates missing keys
// (StyleBuilder::makeDefault fills the rest). Malformed numeric arrays fall
// back to the default (returns false so the caller can flag a partial parse).
bool parseWidgetStyle(const json& j, WidgetStyle& out) {
    out = StyleBuilder::makeDefault();

    if (j.contains("backgroundColor")) {
        out.backgroundColor = parseColorJson(j["backgroundColor"], out.bgToken);
    }
    if (j.contains("textColor")) {
        out.textColor = parseColorJson(j["textColor"], out.textColorToken);
    }
    if (j.contains("border") && j["border"].is_object()) {
        const auto& b = j["border"];
        if (b.contains("width")) {
            out.border.width = b["width"].get<float>();
        }
        if (b.contains("cornerRadius")) {
            out.border.cornerRadius = b["cornerRadius"].get<float>();
        }
        if (b.contains("color")) {
            out.border.color = parseColorJson(b["color"], out.borderColorToken);
        }
    }
    // Phase C: text-editing-widget colors. Default-valued (already set by
    // StyleBuilder::makeDefault), so JSON that omits them keeps the sky
    // blue underline + muted gray placeholder. We read them only if the
    // author explicitly overrides — the parser stays backward-compatible.
    if (j.contains("compositionUnderlineColor")) {
        out.compositionUnderlineColor = parseColorJson(j["compositionUnderlineColor"]);
    }
    if (j.contains("placeholderColor")) {
        out.placeholderColor = parseColorJson(j["placeholderColor"]);
    }
    return true;
}

} // namespace

StyleSheet::StyleSheet() {
    _defaultStyle = StyleBuilder::makeDefault();

    // R-5: pre-register the StyleBuilder makers so layout JSON can reference
    // them by id ("button_default", "textlabel_default", "window_default",
    // "panel_default") without any explicit loadFromString call. Layout JSON
    // may override individual fields via loadFromString.
    _styles["button_default"]   = StyleBuilder::makeButton();
    _styles["textlabel_default"] = StyleBuilder::makeTextLabel();
    _styles["window_default"]   = StyleBuilder::makeWindow();
    _styles["panel_default"]    = StyleBuilder::makePanel();
}

StyleSheet::~StyleSheet() {
}

bool StyleSheet::loadFromString(const char* data, size_t length) {
    if (data == nullptr || length == 0) {
        return false;
    }
    try {
        std::string s(data, length);
        json j = json::parse(s);

        // Format per design.md §4.5:
        // { "styles": { "name": { "backgroundColor": [...], "border": {...} } } }
        if (!j.contains("styles") || !j["styles"].is_object()) {
            return false;
        }
        const auto& styles = j["styles"];
        for (auto it = styles.begin(); it != styles.end(); ++it) {
            WidgetStyle parsed;
            if (parseWidgetStyle(it.value(), parsed)) {
                _styles[it.key()] = parsed;
            }
        }
        return true;
    }
    catch (const std::exception&) {
        return false;
    }
}

bool StyleSheet::loadFromFile(const char* filepath) {
    if (filepath == nullptr) {
        return false;
    }
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    const std::string buf = ss.str();
    return loadFromString(buf.data(), buf.size());
}

const WidgetStyle* StyleSheet::getStyle(const std::string& styleId) const {
    if (styleId.empty()) {
        return &_defaultStyle;
    }
    auto it = _styles.find(styleId);
    if (it != _styles.end()) {
        return &it->second;
    }
    return &_defaultStyle;
}

void StyleSheet::setStyle(const std::string& styleId, const WidgetStyle& style) {
    _styles[styleId] = style;
}

WidgetStyle StyleSheet::getComputedStyle(const std::string& styleId) const {
    // v1: flat inheritance only. Nested themes ("parent": "...") deferred.
    return *getStyle(styleId);
}

StyleManager::StyleManager()
    : _styleSheet(nullptr)
{
}

StyleManager& StyleManager::get() {
    static StyleManager instance;
    return instance;
}

void StyleManager::setStyleSheet(StyleSheet* sheet) {
    _styleSheet = sheet;
}

const WidgetStyle* StyleManager::getStyle(const std::string& styleId) const {
    if (_styleSheet) {
        return _styleSheet->getStyle(styleId);
    }
    return nullptr;
}

WidgetStyle StyleManager::getComputedStyle(const std::string& styleId) const {
    if (_styleSheet) {
        return _styleSheet->getComputedStyle(styleId);
    }
    return StyleBuilder::makeDefault();
}

void StyleManager::applyStyle(Widget* widget) const {
    // v1: applyStyle is a future-hook. Style resolution today happens inline
    // at the widget's onRender (Button does this in fc8a4b1+1). Centralizing
    // here would require the widget tree to walk every widget on every style
    // sheet change — currently layouts are reloaded on change (R-4) which
    // re-creates widgets, so the per-widget draw-time lookup is enough.
    AYUNREFERENCED_PARAM(widget);
}

WidgetStyle StyleBuilder::makeDefault() {
    WidgetStyle style;
    style.backgroundColor = math::FVector4(0.2f, 0.2f, 0.2f, 1.0f);
    style.textColor = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    style.borderColor = math::FVector4(0.3f, 0.3f, 0.3f, 1.0f);
    style.border.width = 1.0f;
    style.border.color = math::FVector4(0.5f, 0.5f, 0.5f, 1.0f);
    style.border.cornerRadius = 0.0f;
    style.font.fontFamily = "Arial";
    style.font.fontSize = 14;
    style.font.color = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    style.font.bold = false;
    style.font.italic = false;
    style.padding = math::FVector4(4.0f, 4.0f, 4.0f, 4.0f);
    style.minWidth = 0.0f;
    style.minHeight = 0.0f;
    style.maxWidth = 0.0f;
    style.maxHeight = 0.0f;
    return style;
}

WidgetStyle StyleBuilder::makeButton() {
    WidgetStyle style = makeDefault();
    style.backgroundColor = math::FVector4(0.3f, 0.3f, 0.3f, 1.0f);
    style.border.width = 1.0f;
    style.border.color = math::FVector4(0.5f, 0.5f, 0.5f, 1.0f);
    style.border.cornerRadius = 4.0f;
    style.padding = math::FVector4(8.0f, 4.0f, 8.0f, 4.0f);
    return style;
}

WidgetStyle StyleBuilder::makeTextLabel() {
    WidgetStyle style = makeDefault();
    style.backgroundColor = math::FVector4(0.0f, 0.0f, 0.0f, 0.0f);
    style.textColor = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    style.padding = math::FVector4(2.0f, 2.0f, 2.0f, 2.0f);
    return style;
}

WidgetStyle StyleBuilder::makeWindow() {
    WidgetStyle style = makeDefault();
    style.backgroundColor = math::FVector4(0.15f, 0.15f, 0.15f, 0.95f);
    style.border.width = 2.0f;
    style.border.color = math::FVector4(0.4f, 0.4f, 0.4f, 1.0f);
    style.border.cornerRadius = 6.0f;
    style.padding = math::FVector4(0.0f, 0.0f, 0.0f, 0.0f);
    return style;
}

WidgetStyle StyleBuilder::makePanel() {
    WidgetStyle style = makeDefault();
    // Distinct from makeDefault's (0.2, 0.2, 0.2, 1) — Panel needs to be
    // visually separable from a plain Widget background, otherwise the
    // resolveStyle "bgIsDefault" sentinel would silently drop every
    // panel_default entry to the hardcoded fallback and the style would
    // never take effect at draw time.
    style.backgroundColor = math::FVector4(0.18f, 0.18f, 0.20f, 1.0f);
    style.border.width = 1.0f;
    style.border.color = math::FVector4(0.3f, 0.3f, 0.3f, 1.0f);
    style.padding = math::FVector4(4.0f, 4.0f, 4.0f, 4.0f);
    return style;
}

ResolvedStyle resolveStyle(const std::string& styleId) {
    return resolveStyle(styleId, nullptr);
}

ResolvedStyle resolveStyle(const std::string& styleId, const Widget* widget) {
    ResolvedStyle out;
    if (styleId.empty()) {
        return out;
    }
    const WidgetStyle* s = StyleManager::get().getStyle(styleId);
    if (s == nullptr) {
        return out;
    }
    // Sentinel: makeDefault's backgroundColor. If the resolved style
    // matches, treat as "no explicit background override" so the
    // hardcoded fallback still wins. See the long-form comment on
    // ResolvedStyle in AYStyle.h for the rationale.
    const WidgetStyle def = StyleBuilder::makeDefault();
    const bool bgIsDefault = (s->backgroundColor.x == def.backgroundColor.x &&
                              s->backgroundColor.y == def.backgroundColor.y &&
                              s->backgroundColor.z == def.backgroundColor.z &&
                              s->backgroundColor.w == def.backgroundColor.w);
    if (bgIsDefault) {
        return out;
    }
    out.hasStyle = true;
    out.backgroundColor = s->backgroundColor;
    out.borderColor = s->border.color;
    out.borderWidth = s->border.width;
    out.cornerRadius = s->border.cornerRadius;

    // G11 — re-expand captured $token slots against the active theme,
    // applying the widget's per-token overrides. Slot capture happens
    // in parseWidgetStyle (above): only slots that arrived as $token
    // references have a non-empty *Token field. Literal slots (numeric
    // arrays) keep their original color — overrides don't reach them
    // by design (you can't "override a literal" without changing the
    // sheet). Caller passes nullptr to skip override lookup.
    //
    // Code-review Sweep3-#3: previously we early-returned when
    // (widget != nullptr && overrides empty()), which left the frozen-
    // at-load-time s->backgroundColor / s->border.color in place when
    // the active theme had been swapped AFTER the StyleSheet was
    // loaded. The point of the *Token capture was exactly to keep the
    // token reference alive for re-expansion; short-circuiting when
    // the widget had no overrides defeated the design. Fix: always
    // consult the theme when ANY token slot is captured, regardless
    // of whether the widget carries overrides.
    const bool hasAnyToken = !s->bgToken.empty() ||
                             !s->borderColorToken.empty() ||
                             !s->textColorToken.empty();
    if (!hasAnyToken &&
        (widget == nullptr || widget->getStyleTokenOverrides().empty())) {
        // No tokens captured AND no overrides → the literal s-> values
        // are the final answer. Skip the theme lookup entirely.
        return out;
    }
    const Theme* theme = ThemeManager::get().getActiveTheme();
    if (theme == nullptr) {
        return out;
    }
    auto apply = [&](const std::string& tok,
                     math::FVector4& slot) {
        if (tok.empty()) return;
        math::FVector4 v = theme->resolveColor(
            std::string("$") + tok,
            widget ? &widget->getStyleTokenOverrides() : nullptr);
        slot = v;
    };
    apply(s->bgToken, out.backgroundColor);
    apply(s->borderColorToken, out.borderColor);
    // Code-review Sweep3-#7: ResolvedStyle doesn't carry textColor, so
    // we can't write the re-expanded value here. Hosts that need
    // theme-overridable textColor should extend ResolvedStyle with a
    // textColor field and call apply(s->textColorToken, out.textColor).
    // For now, the StyleManager direct lookup (StyleManager::get().getStyle
    // -> WidgetStyle::textColor) still surfaces the value frozen at
    // load time — the same Sweep3-#6 caveat applies.
    return out;
}

} // namespace ayt::ui