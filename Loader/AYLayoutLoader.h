#pragma once

#include "AYWidget.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace ayt::ui {
using json = nlohmann::json;

class Widget;
class WidgetFactory;
class I18n;

class UILayoutLoader {
public:
    UILayoutLoader();
    ~UILayoutLoader();

    void setWidgetFactory(WidgetFactory* factory) { _factory = factory; }
    void setI18n(I18n* i18n) { _i18n = i18n; }

    Widget* loadFromFile(const std::string& filepath);
    Widget* loadFromString(const std::string& json);

    Widget* reload(const std::string& id);
    bool isReloadNeeded() const;
    Widget* tryReload();

    void bindEvent(const std::string& widgetId, const std::string& eventType,
                   std::function<void()> handler);
    void clearEventBindings();

    Widget* findWidgetById(const std::string& id) const;

private:
    Widget* buildWidgetTree(const json& j);

    WidgetFactory* _factory;
    I18n* _i18n;

    std::unordered_map<std::string, std::function<void()>> _eventBindings;
    std::unordered_map<std::string, Widget*> _widgetsById;

    std::string _lastJson;
    std::string _lastFilePath;
    std::chrono::steady_clock::time_point _lastLoadTime;
};

} // namespace ayt::ui