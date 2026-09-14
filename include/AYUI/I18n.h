#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::ui {

class I18n {
public:
    using LangTable = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

    static I18n& get();

    void setCurrentLanguage(const std::string& lang);
    const std::string& getCurrentLanguage() const { return _currentLang; }

    bool loadFromString(const char* jsonData, size_t length);
    bool loadFromFile(const char* filepath);

    std::wstring resolve(const std::string& key) const;
    std::string resolve(const std::string& key, const std::string& defaultValue) const;

    bool hasKey(const std::string& key) const;

    const std::vector<std::string>& getAvailableLanguages() const { return _languages; }

    void clear();

private:
    I18n() = default;
    I18n(const I18n&) = delete;
    I18n& operator=(const I18n&) = delete;

    std::string _currentLang;
    LangTable _table;
    std::vector<std::string> _languages;

    static const std::string DEFAULT_LANG;
};

inline bool isI18nKey(const std::string& text) {
    return text.length() > 3 && text.substr(0, 3) == "ui.";
}

// Audit H-S-4 (i18n auto-detect phishing): pre-fix the layout loader
// auto-detected any user-supplied `text` field starting with "ui." as
// a translation key. A hostile layout JSON could put
//   "text": "ui.btn.confirm"
// and the loader would treat it as a key, not a literal — silently
// translating a user-visible label that the user had no reason to
// route through i18n at all.
//
// The fix: explicit opt-in via the TR() macro. TR() wraps a literal
// into a distinct I18nKey tag type that the loader recognizes only
// when it explicitly asks for a translation. Bare strings are always
// treated as literal fallbacks.
//
// Migration: callers that previously relied on the auto-detect should
// pass the literal via `textKey` (layout JSON) or wrap it in
// `TR("...")` (C++ call sites).
struct I18nKey {
    const char* value;
};

// Explicit translation-key wrapper. Use TR() to mark a literal as an
// i18n key at the call site; the loader recognises it via the I18nKey
// overload of isI18nKey.
#ifndef TR
#define TR(literal) (::ayt::ui::I18nKey{ (literal) })
#endif

// I18nKey overload — any wrapped value is explicit by construction.
inline bool isI18nKey(const I18nKey& /*key*/) {
    return true;
}

} // namespace ayt::ui