#include "AYLayoutLoader.h"
#include "AYWidgetFactory.h"
#include "AYI18n.h"
#include "AYButton.h"
#include "AYTextLabel.h"
#include "AYWindow.h"
#include "AYBox.h"

#include <fstream>
#include <sstream>

namespace ayt::ui {

UILayoutLoader::UILayoutLoader()
    : _factory(&WidgetFactory::get())
    , _i18n(&I18n::get())
{
}

UILayoutLoader::~UILayoutLoader() {
}

Widget* UILayoutLoader::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return nullptr;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    _lastJson = ss.str();
    _lastFilePath = filepath;

    // Update load time for file change detection
    _lastLoadTime = std::chrono::steady_clock::now();

    return loadFromString(_lastJson);
}

Widget* UILayoutLoader::loadFromString(const std::string& jsonStr) {
    _widgetsById.clear();

    try {
        json j = json::parse(jsonStr);
        return buildWidgetTree(j);
    }
    catch (const std::exception& e) {
        //AYLOG_WARN("UILayoutLoader parse error: {}", e.what());
        return nullptr;
    }
}

Widget* UILayoutLoader::reload(const std::string& id) {
    AYUNREFERENCED_PARAM(id);
    if (_lastFilePath.empty()) return nullptr;
    return loadFromFile(_lastFilePath);
}

bool UILayoutLoader::isReloadNeeded() const {
    if (_lastFilePath.empty()) return false;

    std::ifstream file(_lastFilePath);
    if (!file.is_open()) return false;

    // Compare file last write time with last load time
    auto fileTime = std::chrono::steady_clock::time_point();
    auto now = std::chrono::steady_clock::now();

    // Simple heuristic: if file was modified after last load, reload is needed
    // In production, use filesystem::last_write_time
    return false;  // Placeholder - actual implementation requires filesystem API
}

Widget* UILayoutLoader::tryReload() {
    if (!isReloadNeeded()) return nullptr;
    if (_lastFilePath.empty()) return nullptr;
    return loadFromFile(_lastFilePath);
}

void UILayoutLoader::bindEvent(const std::string& widgetId, const std::string& eventType,
                                std::function<void()> handler) {
    std::string key = widgetId + "." + eventType;
    _eventBindings[key] = handler;
}

void UILayoutLoader::clearEventBindings() {
    _eventBindings.clear();
}

Widget* UILayoutLoader::findWidgetById(const std::string& id) const {
    auto it = _widgetsById.find(id);
    return (it != _widgetsById.end()) ? it->second : nullptr;
}

Widget* UILayoutLoader::buildWidgetTree(const json& j) {
    if (!j.is_object()) return nullptr;

    std::string type = j.value("type", "Widget");

    Widget* widget = _factory->create(type);
    if (!widget) {
        widget = new Widget();
    }

    // ID
    std::string id = j.value("id", "");
    if (!id.empty()) {
        widget->setId(id);
        _widgetsById[id] = widget;
    }

    // Position
    if (j.contains("position") && j["position"].is_object()) {
        float x = j["position"].value("x", 0.0f);
        float y = j["position"].value("y", 0.0f);
        widget->setPosition(math::FVector2(x, y));
    }

    // Size
    if (j.contains("size") && j["size"].is_object()) {
        float w = j["size"].value("w", 100.0f);
        float h = j["size"].value("h", 50.0f);
        widget->setSize(math::FVector2(w, h));
    }

    // Visible
    widget->setVisible(j.value("visible", true));

    // Style
    std::string style = j.value("style", "");
    if (!style.empty()) {
        widget->setStyleId(style);
    }

    // Text with i18n support
    std::string text = j.value("text", "");
    if (!text.empty()) {
        if (_i18n && isI18nKey(text)) {
            std::wstring wtext = _i18n->resolve(text);
            text = std::string(wtext.begin(), wtext.end());
        }

        if (Button* button = dynamic_cast<Button*>(widget)) {
            button->setText(std::wstring(text.begin(), text.end()));
        }
        if (TextLabel* label = dynamic_cast<TextLabel*>(widget)) {
            label->setText(std::wstring(text.begin(), text.end()));
        }
        if (Window* window = dynamic_cast<Window*>(widget)) {
            window->setTitle(std::wstring(text.begin(), text.end()));
        }
    }

    // onClick binding
    if (!id.empty() && j.contains("onClick")) {
        std::string handlerName = j["onClick"].get<std::string>();
        std::string key = id + ".onClick";
        auto it = _eventBindings.find(key);
        if (it != _eventBindings.end()) {
            if (Button* button = dynamic_cast<Button*>(widget)) {
                button->setOnClicked(it->second);
            }
        }
    }

    // Children
    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& childJson : j["children"]) {
            Widget* child = buildWidgetTree(childJson);
            if (child) {
                widget->addChild(child);
            }
        }
    }

    return widget;
}

} // namespace ayt::ui