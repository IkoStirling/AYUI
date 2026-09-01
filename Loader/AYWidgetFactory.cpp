#include "AYUI/WidgetFactory.h"
#include "AYUI/Button.h"
#include "AYUI/CheckBox.h"
#include "AYUI/RadioButton.h"
#include "AYUI/Slider.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/Spinner.h"
#include "AYUI/TextInput.h"
#include "AYUI/TextArea.h"
#include "AYUI/Tooltip.h"
#include "AYUI/Separator.h"
#include "AYUI/MenuItem.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuBar.h"
#include "AYUI/ToolBar.h"
#include "AYUI/ToolBarSeparator.h"
#include "AYUI/StatusBar.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/ScrollView.h"
#include "AYUI/ListView.h"
#include "AYUI/TileView.h"
#include "AYUI/ComboBox.h"
#include "AYUI/TextLabel.h"
#include "AYUI/TreeNode.h"
#include "AYUI/TreeView.h"
#include "AYUI/RichText.h"
#include "AYUI/Image.h"
#include "AYUI/Window.h"
#include "AYUI/Panel.h"
#include "AYUI/Box.h"
#include "AYUI/SplitterHandle.h"
#include "AYUI/TabControl.h"
#include "AYUI/GridPanel.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"
#include "AYUI/Dimmer.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/TabStrip.h"

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

// Register every JSON-creatable built-in in one place. UIManager keeps a
// defensive registration pass for unusual static-link configurations, but
// UILayoutLoader must also work correctly without constructing UIManager.
struct DefaultWidgetRegistrar {
    DefaultWidgetRegistrar() {
        REGISTER_WIDGET("Widget", Widget);
        REGISTER_WIDGET("Button", Button);
        WidgetFactory::get().registerCreator("TextLabel", createTextLabelWidget);
        WidgetFactory::get().registerCreator("CheckBox", createCheckBoxWidget);
        WidgetFactory::get().registerCreator("RadioButton", createRadioButtonWidget);
        WidgetFactory::get().registerCreator("Slider", createSliderWidget);
        WidgetFactory::get().registerCreator("ProgressBar", createProgressBarWidget);
        WidgetFactory::get().registerCreator("Spinner", createSpinnerWidget);
        WidgetFactory::get().registerCreator("TextInput", createTextInputWidget);
        WidgetFactory::get().registerCreator("TextArea", createTextAreaWidget);
        WidgetFactory::get().registerCreator("ScrollBar", createScrollBarWidget);
        WidgetFactory::get().registerCreator("ScrollView", createScrollViewWidget);
        WidgetFactory::get().registerCreator("ListView", createListViewWidget);
        WidgetFactory::get().registerCreator("TileView", createTileViewWidget);
        WidgetFactory::get().registerCreator("ComboBox", createComboBoxWidget);
        WidgetFactory::get().registerCreator("TabControl", createTabControlWidget);
        WidgetFactory::get().registerCreator("TreeNode", createTreeNodeWidget);
        WidgetFactory::get().registerCreator("TreeView", createTreeViewWidget);
        WidgetFactory::get().registerCreator("RichText", createRichTextWidget);
        WidgetFactory::get().registerCreator("Tooltip", createTooltipWidget);
        WidgetFactory::get().registerCreator("Separator", createSeparatorWidget);
        WidgetFactory::get().registerCreator("MenuItem", createMenuItemWidget);
        WidgetFactory::get().registerCreator("Menu", createMenuWidget);
        WidgetFactory::get().registerCreator("MenuBar", createMenuBarWidget);
        WidgetFactory::get().registerCreator("ToolBar", createToolBarWidget);
        WidgetFactory::get().registerCreator("ToolBarSeparator", createToolBarSeparatorWidget);
        WidgetFactory::get().registerCreator("StatusBar", createStatusBarWidget);
        REGISTER_WIDGET("Image", Image);
        WidgetFactory::get().registerCreator("Window", createWindowWidget);
        REGISTER_WIDGET("Panel", Panel);
        REGISTER_WIDGET("VBox", VBox);
        REGISTER_WIDGET("HBox", HBox);
        REGISTER_WIDGET("SplitterHandle", SplitterHandle);
        REGISTER_WIDGET("GridPanel", GridPanel);
        WidgetFactory::get().registerCreator("DockArea", createDockAreaWidget);
        REGISTER_WIDGET("DockCard", DockCard);
        REGISTER_WIDGET("DockOverlay", DockOverlay);
        REGISTER_WIDGET("Dimmer", Dimmer);
        REGISTER_WIDGET("Modal", Modal);
        WidgetFactory::get().registerCreator("ModalDialog", createModalDialogWidget);
        WidgetFactory::get().registerCreator("TabStrip", createTabStripWidget);
    }
};

static DefaultWidgetRegistrar s_registrar;

} // namespace ayt::ui
