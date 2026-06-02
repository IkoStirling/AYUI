#include "AYWidgetFactory.h"
#include "AYButton.h"
#include "AYTextLabel.h"
#include "AYImage.h"
#include "AYWindow.h"
#include "AYBox.h"

namespace ayt::ui {

WidgetFactory& WidgetFactory::get() {
    static WidgetFactory instance;
    return instance;
}

void WidgetFactory::registerCreator(const std::string& typeName, Creator creator) {
    _creators[typeName] = creator;
}

Widget* WidgetFactory::create(const std::string& typeName) const {
    auto it = _creators.find(typeName);
    if (it != _creators.end()) {
        return it->second();
    }
    return nullptr;
}

bool WidgetFactory::isRegistered(const std::string& typeName) const {
    return _creators.find(typeName) != _creators.end();
}

void WidgetFactory::unregister(const std::string& typeName) {
    _creators.erase(typeName);
}

// 自动注册默认控件
struct DefaultWidgetRegistrar {
    DefaultWidgetRegistrar() {
        REGISTER_WIDGET("Widget", Widget);
        REGISTER_WIDGET("Button", Button);
        REGISTER_WIDGET("TextLabel", TextLabel);
        REGISTER_WIDGET("Image", Image);
        REGISTER_WIDGET("Window", Window);
        REGISTER_WIDGET("VBox", VBox);
        REGISTER_WIDGET("HBox", HBox);
    }
};

static DefaultWidgetRegistrar s_registrar;

} // namespace ayt::ui