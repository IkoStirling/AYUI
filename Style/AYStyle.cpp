#include "AYStyle.h"
#include "aymath/MathTypes.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace ayt::ui {

namespace {

using json = nlohmann::json;

// Parse a single WidgetStyle from a JSON object. Tolerates missing keys
// (StyleBuilder::makeDefault fills the rest). Malformed numeric arrays fall
// back to the default (returns false so the caller can flag a partial parse).
bool parseWidgetStyle(const json& j, WidgetStyle& out) {
    out = StyleBuilder::makeDefault();

    if (j.contains("backgroundColor") && j["backgroundColor"].is_array() &&
        j["backgroundColor"].size() == 4) {
        const auto& c = j["backgroundColor"];
        out.backgroundColor = math::FVector4(
            c[0].get<float>(), c[1].get<float>(),
            c[2].get<float>(), c[3].get<float>());
    }
    if (j.contains("textColor") && j["textColor"].is_array() &&
        j["textColor"].size() == 4) {
        const auto& c = j["textColor"];
        out.textColor = math::FVector4(
            c[0].get<float>(), c[1].get<float>(),
            c[2].get<float>(), c[3].get<float>());
    }
    if (j.contains("border") && j["border"].is_object()) {
        const auto& b = j["border"];
        if (b.contains("width")) {
            out.border.width = b["width"].get<float>();
        }
        if (b.contains("cornerRadius")) {
            out.border.cornerRadius = b["cornerRadius"].get<float>();
        }
        if (b.contains("color") && b["color"].is_array() && b["color"].size() == 4) {
            const auto& c = b["color"];
            out.border.color = math::FVector4(
                c[0].get<float>(), c[1].get<float>(),
                c[2].get<float>(), c[3].get<float>());
        }
    }
    // Phase C: text-editing-widget colors. Default-valued (already set by
    // StyleBuilder::makeDefault), so JSON that omits them keeps the sky
    // blue underline + muted gray placeholder. We read them only if the
    // author explicitly overrides — the parser stays backward-compatible.
    if (j.contains("compositionUnderlineColor") &&
        j["compositionUnderlineColor"].is_array() &&
        j["compositionUnderlineColor"].size() == 4) {
        const auto& c = j["compositionUnderlineColor"];
        out.compositionUnderlineColor = math::FVector4(
            c[0].get<float>(), c[1].get<float>(),
            c[2].get<float>(), c[3].get<float>());
    }
    if (j.contains("placeholderColor") &&
        j["placeholderColor"].is_array() &&
        j["placeholderColor"].size() == 4) {
        const auto& c = j["placeholderColor"];
        out.placeholderColor = math::FVector4(
            c[0].get<float>(), c[1].get<float>(),
            c[2].get<float>(), c[3].get<float>());
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
    return out;
}

} // namespace ayt::ui