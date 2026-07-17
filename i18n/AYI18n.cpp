#include "AYI18n.h"
#include "aymath/MathUtils.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace ayt::ui {

using json = nlohmann::json;

const std::string I18n::DEFAULT_LANG = "en";

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
        return std::wstring(key.begin(), key.end());
    }

    auto langIt = sectionIt->second.find(_currentLang);
    if (langIt == sectionIt->second.end()) {
        auto defaultIt = sectionIt->second.find(DEFAULT_LANG);
        if (defaultIt != sectionIt->second.end()) {
            return std::wstring(defaultIt->second.begin(), defaultIt->second.end());
        }
        return std::wstring(key.begin(), key.end());
    }

    return std::wstring(langIt->second.begin(), langIt->second.end());
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