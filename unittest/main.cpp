#include "AYTest.h"
#include "aymath/MathTypes.h"

#include <cstdio>

#include "Test_Widget.cpp"
#include "Test_Button.cpp"
#include "Test_CheckBox.cpp"
#include "Test_RadioButton.cpp"
#include "Test_Slider.cpp"
#include "Test_ProgressBar.cpp"
#include "Test_TextInput.cpp"
#include "Test_ScrollBar.cpp"
#include "Test_InteractiveWidget.cpp"
#include "Test_RenderBackend.cpp"
#include "Test_TextLabel.cpp"
#include "Test_Thickness.cpp"
#include "Test_Image.cpp"
#include "Test_Window.cpp"
#include "Test_Layout.cpp"
#include "Test_I18n.cpp"
#include "Test_Style.cpp"
#include "Test_WidgetFactory.cpp"
#include "Test_LayoutLoader.cpp"
#include "Test_Serializer.cpp"
#include "Test_UIManager.cpp"
#include "Test_R6_CompoundWidget.cpp"
#include "Test_R9_NamespaceEnum.cpp"
#include "Test_Panel.cpp"
#include "Test_ListView.cpp"
#include "Test_ComboBox.cpp"
#include "Test_TabControl.cpp"
#include "Test_GridPanel.cpp"
#include "Test_TextArea.cpp"
#include "Test_SelectableWidget.cpp"
#include "Test_Tooltip.cpp"
#include "Test_Separator.cpp"
#include "Test_MenuItem.cpp"
#include "Test_Menu.cpp"
#include "Test_MenuBar.cpp"
#include "Test_ToolBar.cpp"
#include "Test_StatusBar.cpp"
#include "Test_TreeNode.cpp"
#include "Test_TreeView.cpp"
#include "Test_RichText.cpp"
#include "Test_Dimmer.cpp"
#include "Test_Modal.cpp"
#include "Test_TabStrip.cpp"
#include "Test_ModalDialog.cpp"
#include "Test_DragDrop.cpp"

void runTest()
{
	// Run all tests
	ayt::test::runAllTests("AYUI");
}
using namespace ayt::ui;
using namespace ayt::math;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    // Unbuffered stdout so a crash mid-suite does not hide CASE/PASS lines
    // when output is redirected to a file.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    runTest();
    return 0;
}