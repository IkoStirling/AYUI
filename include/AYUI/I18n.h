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

} // namespace ayt::ui