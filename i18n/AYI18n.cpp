#include "AYI18n.h"
#include "AYMath/MathUtils.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cstdint>

namespace ayt::ui {

using json = nlohmann::json;

const std::string I18n::DEFAULT_LANG = "en";

// =============================================================================
// UTF-8 -> wchar_t decoder
// =============================================================================
// The std::wstring resolve() overload used to widen by `std::wstring(s.begin(),
// s.end())` — a byte-by-byte copy that produces mojibake on Windows where
// wchar_t is 16-bit and any non-ASCII UTF-8 byte (>= 0x80) becomes a separate
// code unit. The renderer then renders those as garbled Latin-1 / CJK
// glyphs. The right shape decodes UTF-8 to Unicode codepoints and emits
// wchar_t with surrogate pairs for cp > 0xFFFF (Windows). Linux/macOS
// wchar_t is 32-bit so surrogate pairs aren't needed there — the standard
// // "if (cp > 0xFFFF) emit two wchar_t" trick handles both platforms
// (the high surrogate has value 0xD800..0xDBFF, the low 0xDC00..0xDFFF).
// =============================================================================
static std::wstring utf8ToWString(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int extra = 0;
        if      (c < 0x80) { cp = c;                  extra = 0; }
        else if (c < 0xC0) { cp = 0xFFFD;             extra = 0; }  // stray continuation
        else if (c < 0xE0) { cp = c & 0x1F;           extra = 1; }
        else if (c < 0xF0) { cp = c & 0x0F;           extra = 2; }
        else if (c < 0xF8) { cp = c & 0x07;           extra = 3; }
        else                { cp = 0xFFFD;            extra = 0; }  // > 4-byte UTF-8 invalid
        ++i;
        bool bad = false;
        for (int k = 0; k < extra && i < s.size(); ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i]);
            if ((cc & 0xC0) != 0x80) { bad = true; break; }
            cp = (cp << 6) | (cc & 0x3F);
            ++i;
        }
        if (bad) { cp = 0xFFFD; }
        // Encode as UTF-16 surrogate pair on platforms where wchar_t is 16-bit.
        // sizeof(wchar_t) == 2 (Windows): use surrogates. Otherwise (Linux/macOS
        // wchar_t = 32 bits), emit one wchar_t.
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp <= 0xFFFF) {
                out.push_back(static_cast<wchar_t>(cp));
            } else if (cp <= 0x10FFFF) {
                cp -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
            } else {
                out.push_back(static_cast<wchar_t>(0xFFFD));
            }
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

I18n& I18n::get() {
    static I18n instance;
    return instance;
}

void I18n::setCurrentLanguage(const std::string& lang) {
    _currentLang = lang;
}

bool I18n::loadFromString(const char* jsonData, size_t length) {
    try {
        json j = json::parse(jsonData, jsonData + length);

        clear();

        for (auto& [key, value] : j.items()) {
            if (value.is_object()) {
                std::unordered_map<std::string, std::string> langMap;
                for (auto& [lang, text] : value.items()) {
                    if (text.is_string()) {
                        langMap[lang] = text.get<std::string>();
                        if (std::find(_languages.begin(), _languages.end(), lang) == _languages.end()) {
                            _languages.push_back(lang);
                        }
                    }
                }
                _table[key] = std::move(langMap);
            }
        }

        if (_currentLang.empty() && !_languages.empty()) {
            _currentLang = _languages[0];
        }

        return true;
    }
    catch (const std::exception& e) {
        //AYLOG_WARN("I18n parse error: {}", e.what());
        return false;
    }
}

bool I18n::loadFromFile(const char* filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    return loadFromString(content.c_str(), content.size());
}

std::wstring I18n::resolve(const std::string& key) const {
    auto sectionIt = _table.find(key);
    if (sectionIt == _table.end()) {
        return utf8ToWString(key);
    }

    auto langIt = sectionIt->second.find(_currentLang);
    if (langIt == sectionIt->second.end()) {
        auto defaultIt = sectionIt->second.find(DEFAULT_LANG);
        if (defaultIt != sectionIt->second.end()) {
            return utf8ToWString(defaultIt->second);
        }
        return utf8ToWString(key);
    }

    return utf8ToWString(langIt->second);
}

std::string I18n::resolve(const std::string& key, const std::string& defaultValue) const {
    auto sectionIt = _table.find(key);
    if (sectionIt == _table.end()) {
        return defaultValue;
    }

    auto langIt = sectionIt->second.find(_currentLang);
    if (langIt == sectionIt->second.end()) {
        auto defaultIt = sectionIt->second.find(DEFAULT_LANG);
        if (defaultIt != sectionIt->second.end()) {
            return defaultIt->second;
        }
        return defaultValue;
    }

    return langIt->second;
}

bool I18n::hasKey(const std::string& key) const {
    return _table.find(key) != _table.end();
}

void I18n::clear() {
    _table.clear();
    _languages.clear();
}

} // namespace ayt::ui