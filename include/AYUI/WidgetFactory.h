#pragma once

#include "AYUI/Widget.h"
#include <unordered_map>
#include <string>
#include <functional>

namespace ayt::ui {

class Widget;

class WidgetFactory {
public:
    using Creator = std::function<Widget*()>;

    static WidgetFactory& get();

    void registerCreator(const std::string& typeName, Creator creator);
    Widget* create(const std::string& typeName) const;

    bool isRegistered(const std::string& typeName) const;
    void unregister(const std::string& typeName);

private:
    WidgetFactory() = default;
    WidgetFactory(const WidgetFactory&) = delete;
    WidgetFactory& operator=(const WidgetFactory&) = delete;

    std::unordered_map<std::string, Creator> _creators;
};

#define REGISTER_WIDGET(TYPE_NAME, CLASS_NAME) \
    ::ayt::ui::WidgetFactory::get().registerCreator(TYPE_NAME, [](){ return new CLASS_NAME(); })

} // namespace ayt::ui