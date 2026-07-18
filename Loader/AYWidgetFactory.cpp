#include "AYWidgetFactory.h"
#include "AYButton.h"
#include "AYCheckBox.h"
#include "AYRadioButton.h"
#include "AYSlider.h"
#include "AYProgressBar.h"
#include "AYTextInput.h"
#include "AYTextArea.h"
#include "AYScrollBar.h"
#include "AYScrollView.h"
#include "AYListView.h"
#include "AYComboBox.h"
#include "AYTextLabel.h"
#include "AYImage.h"
#include "AYWindow.h"
#include "AYPanel.h"
#include "AYBox.h"
#include "AYSplitterHandle.h"
#include "AYTabControl.h"
#include "AYGridPanel.h"

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
        WidgetFactory::get().registerCreator("TextLabel", createTextLabelWidget);
        WidgetFactory::get().registerCreator("CheckBox", createCheckBoxWidget);
        WidgetFactory::get().registerCreator("RadioButton", createRadioButtonWidget);
        WidgetFactory::get().registerCreator("Slider", createSliderWidget);
        WidgetFactory::get().registerCreator("ProgressBar", createProgressBarWidget);
        WidgetFactory::get().registerCreator("TextInput", createTextInputWidget);
        WidgetFactory::get().registerCreator("TextArea", createTextAreaWidget);
        WidgetFactory::get().registerCreator("ScrollBar", createScrollBarWidget);
        WidgetFactory::get().registerCreator("ScrollView", createScrollViewWidget);
        WidgetFactory::get().registerCreator("ListView", createListViewWidget);
        WidgetFactory::get().registerCreator("ComboBox", createComboBoxWidget);
        WidgetFactory::get().registerCreator("TabControl", createTabControlWidget);
        REGISTER_WIDGET("Image", Image);
        REGISTER_WIDGET("Window", Window);
        REGISTER_WIDGET("Panel", Panel);
        REGISTER_WIDGET("VBox", VBox);
        REGISTER_WIDGET("HBox", HBox);
        REGISTER_WIDGET("SplitterHandle", SplitterHandle);
        REGISTER_WIDGET("GridPanel", GridPanel);
    }
};

static DefaultWidgetRegistrar s_registrar;

} // namespace ayt::ui