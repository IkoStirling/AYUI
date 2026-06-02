#include "AYTest.h"

#include "Test_Widget.cpp"
#include "Test_Button.cpp"
#include "Test_TextLabel.cpp"
#include "Test_Image.cpp"
#include "Test_Window.cpp"
#include "Test_Layout.cpp"
#include "Test_I18n.cpp"
#include "Test_WidgetFactory.cpp"
#include "Test_LayoutLoader.cpp"
#include "Test_Serializer.cpp"

void runTest()
{
	// Run all tests
	ayt::test::runAllTests("AYUI");
}
using namespace ayt::ui;

int main(int argc, char* argv[]) {
	runTest();
}