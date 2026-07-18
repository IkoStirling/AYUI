#include "AYTest.h"

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

void runTest()
{
	// Run all tests
	ayt::test::runAllTests("AYUI");
}
using namespace ayt::ui;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    runTest();
    return 0;
}