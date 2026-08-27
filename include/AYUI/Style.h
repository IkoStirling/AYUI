#pragma once

#include "AYMath/MathTypes.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::ui {

// Forward declarations
class Widget;

// Style property types
struct ColorStyle {
    math::FVector4 normal;
    math::FVector4 hovered;
    math::FVector4 pressed;
    math::FVector4 disabled;
};

struct BorderStyle {
    float width;
    math::FVector4 color;
    float cornerRadius;
};

struct FontStyle {
    std::string fontFamily;
    int fontSize;
    math::FVector4 color;
    bool bold;
    bool italic;
};

// Complete style for a widget
struct WidgetStyle {
    math::FVector4 backgroundColor;
    math::FVector4 textColor;
    math::FVector4 borderColor;
    BorderStyle border;
    FontStyle font;
    math::FVector4 padding;
    float minWidth;
    float minHeight;
    float maxWidth;
    float maxHeight;
    // Phase C (S4 + C4): text-editing-widget-specific colors.
    // - compositionUnderlineColor: drawn under the IME pre-edit preview
    //   by TextInput + TextArea (PR-2). Default is sky blue, distinct from
    //   selection highlight to avoid visual confusion.
    // - placeholderColor: drawn when TextInput's text is empty + not
    //   focused + placeholder text set (PR-3 C4). Default is muted gray.
    math::FVector4 compositionUnderlineColor = math::FVector4(0.30f, 0.65f, 0.95f, 1.0f);
    math::FVector4 placeholderColor = math::FVector4(0.55f, 0.55f, 0.60f, 0.7f);

    // G11 — optional token references for the THREE color slots that
    // resolveStyle() returns (backgroundColor / borderColor / textColor).
    // When non-empty, the original $token source is preserved alongside
    // the expanded FVector4 value, so a later widget-level override
    // (setStyleTokenOverride) can be re-applied. Empty by default —
    // pre-G11 styles + literal arrays leave these blank.
    std::string bgToken;
    std::string borderColorToken;
    std::string textColorToken;
};

class StyleSheet {
public:
    StyleSheet();
    ~StyleSheet();

    // Load from file or string
    bool loadFromString(const char* data, size_t length);
    bool loadFromFile(const char* filepath);

    // Get style by ID
    const WidgetStyle* getStyle(const std::string& styleId) const;

    // Add or update style
    void setStyle(const std::string& styleId, const WidgetStyle& style);

    // Merge styles (for inheritance)
    WidgetStyle getComputedStyle(const std::string& styleId) const;

    // G11 — iterate every style entry. Used by Theme::buildComposedSheet
    // to merge multiple fragments. Returns a vector of (id, WidgetStyle)
    // pairs in insertion order. Exposed only for Theme composition; day-
    // to-day callers should keep using getStyle(id).
    const std::unordered_map<std::string, WidgetStyle>& getAllStylesForCompose() const {
        return _styles;
    }

private:
    std::unordered_map<std::string, WidgetStyle> _styles;
    WidgetStyle _defaultStyle;

    // Parse helpers
    void parseStyle(const char* data, size_t length);
    void parseProperty(const std::string& styleId, const std::string& name, const std::string& value);
};

class StyleManager {
public:
    static StyleManager& get();

    void setStyleSheet(StyleSheet* sheet);
    StyleSheet* getStyleSheet() const { return _styleSheet; }

    const WidgetStyle* getStyle(const std::string& styleId) const;
    WidgetStyle getComputedStyle(const std::string& styleId) const;

    void applyStyle(Widget* widget) const;

    // AYUI-Perf-2026-08-26: bump the theme-version counter so the
    // resolveStyle() memo knows to discard stale entries. Theme swaps
    // (ThemeManager::setActiveTheme) and StyleSheet mutations both call
    // this; the cached (styleId, themeVersion) → ResolvedStyle entries
    // are otherwise reusable across frames.
    void invalidateResolveCache() { _resolveCacheVersion++; }
    uint64_t getResolveCacheVersion() const { return _resolveCacheVersion; }

private:
    StyleManager();
    StyleManager(const StyleManager&) = delete;
    StyleManager& operator=(const StyleManager&) = delete;

    StyleSheet* _styleSheet;
    // AYUI-Perf-2026-08-26: monotonically increasing cache-version
    // counter. resolveStyle() keys its memo by (styleId, version); the
    // counter is bumped whenever anything that could change the resolved
    // output mutates (theme swap, StyleSheet change).
    uint64_t _resolveCacheVersion = 1;
};

// Helper to define styles in code
struct StyleBuilder {
    static WidgetStyle makeDefault();
    static WidgetStyle makeButton();
    static WidgetStyle makeTextLabel();
    static WidgetStyle makeWindow();
    static WidgetStyle makePanel();
};

// Resolved style payload for the simple "background + border" rendering
// pattern shared by Button / Panel / Window. Centralizes the "if styleId
// is set AND the resolved style is not the makeDefault sentinel AND
// something actually changed, use it" dance that used to be duplicated
// inline in every onRender. `hasStyle` is the signal: false means the
// caller should use its hardcoded fallback. Otherwise the four fields are
// populated from the StyleManager lookup (or the StyleBuilder default when
// no StyleSheet is wired in).
struct ResolvedStyle {
    bool hasStyle = false;
    math::FVector4 backgroundColor;
    math::FVector4 borderColor;
    float borderWidth = 0.0f;
    float cornerRadius = 0.0f;
};

// Resolve a style id against the global StyleManager. Returns a populated
// ResolvedStyle with hasStyle=true when:
//   - the styleId is non-empty,
//   - the StyleManager has a StyleSheet wired in,
//   - the resolved WidgetStyle's backgroundColor is NOT the makeDefault
//     sentinel (0.2, 0.2, 0.2, 1).
//
// The third condition exists because StyleBuilder pre-registers
// button/textlabel/window/panel makers at StyleSheet construction time
// with non-default padding/border/cornerRadius but default backgroundColor
// — those entries should NOT silently override a widget's hardcoded
// fallback colors. Without this guard, a widget that never explicitly
// set its style would silently change color when the StyleManager got
// wired in later.
ResolvedStyle resolveStyle(const std::string& styleId);

// G11 — overload that consults a widget's per-token overrides. When
// `widget` is non-null AND the widget has any setStyleTokenOverride()
// entries, those entries WIN over the active theme when resolving any
// token references embedded in the style's color slots. Pure-literal
// styles (no $tokens) are unaffected. Pass nullptr to skip override
// lookup (equivalent to the single-arg form).
ResolvedStyle resolveStyle(const std::string& styleId, const Widget* widget);

} // namespace ayt::ui