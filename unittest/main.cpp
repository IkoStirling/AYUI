#include "AYTest.h"
#include "AYMath/MathTypes.h"

// Test translation units are included below intentionally; keep this file
// as the rebuild anchor when any included test source changes.
// The anchor also keeps focused audit probes reproducible after edits.

#include <cstdio>

#include "Test_Widget.cpp"
#include "Test_Button.cpp"
#include "Test_B3RoundedFill.cpp"
#include "Test_CheckBox.cpp"
#include "Test_RadioButton.cpp"
#include "Test_RadioGroup.cpp"
#include "Test_Slider.cpp"
#include "Test_ProgressBar.cpp"
#include "Test_TextInput.cpp"
#include "Test_ScrollBar.cpp"
#include "Test_InteractiveWidget.cpp"
#include "Test_RenderBackend.cpp"
#include "Test_MockBlendGradient.cpp"
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
#include "Test_ToolBarSeparator.cpp"
#include "Test_StatusBar.cpp"
#include "Test_StatusBar_G8.cpp"
#include "Test_TreeNode.cpp"
#include "Test_TreeView.cpp"
#include "Test_RichText.cpp"
#include "Test_Dimmer.cpp"
#include "Test_Modal.cpp"
#include "Test_TabStrip.cpp"
#include "Test_ModalDialog.cpp"
#include "Test_DragDrop.cpp"
#include "Test_Style_G9.cpp"
#include "Test_ImageTexture_G10.cpp"
#include "Test_Theme_G11.cpp"
#include "Test_Constraint_G13.cpp"
#include "Test_UndoRedo_P1.cpp"
#include "Test_MenuShortcut_P3.cpp"
#include "Test_DockArea.cpp"
#include "Test_UIManagerPerWindow.cpp"
#include "Test_DockAreaLoader.cpp"
#include "Test_DockFloat.cpp"
#include "Test_LayoutPersistence.cpp"  // D4 (2026-07-26)
#include "Test_CardPromotion.cpp"      // D5.5 (2026-07-26)
#include "Test_Leak.cpp"               // code-review 2026-08-02 leak-detection regression
#include "Test_TextAreaMeasure.cpp"    // PR-A1 measureText 贯�?
#include "Test_Clipboard.cpp"          // PR-A2 clipboard 抽接�?
#include "Test_TextInput_Selection.cpp" // PR-A3 selection keyboard + undo
#include "Test_WindowResize.cpp"        // PR-B1 Window 4-edge + 4-corner resize
#include "Test_Theme_Gallery.cpp"       // PR-B2 measurePrefixWidth + Theme swap
#include "Test_WheelRouting.cpp"        // PR-B3 nested wheel routing
#include "Test_ContainerContract.cpp"   // PR-Container-Shared-Contract (clip+offset+hitTest unified)
#include "Test_ScrollView.cpp"          // PR-Container-Shared-Contract (ScrollView bar clamp through shared helper)
#include "Test_TypeaheadBuffer.cpp"     // PR-TypeaheadBuffer (ComboBox + Menu shared prefix-accumulator)
#include "Test_SyncVerticalBar.cpp"     // PR-SyncVerticalBar (header-only helper + 4 owner migration)
#include "Test_ContainerContract_Cut2.cpp" // PR-Container-Contract-Cut2 (helper + 4 container migration + MockRenderer clip)
#include "Test_Scene_Suite_G.cpp"          // Gallery-regression layer (3 scenario-level UTs for UT blind spots)
#include "Test_DockTabGroup.cpp"           // dock-tree Phase 2 (leaf widget)
#include "Test_DockTree.cpp"               // dock-tree Phase 3 (nested split / join / prune)
#include "Test_OpacityAnimation.cpp"       // PR-anim (renderer opacity stack + widget fade tweens)
#include "Test_Tween.cpp"                  // UI-anim cut 1 (easeCurve table lock + AnimState)
#include "Test_ColorAnimation.cpp"         // UI-anim cut 1 (InteractiveWidget color transitions)
#include "Test_PopupFade.cpp"              // UI-anim cut 1 (popup fade in/out via pending-close queue)
#include "Test_Spinner.cpp"                // UI-anim cut 2 (Spinner orbit dots + ProgressBar indeterminate)
#include "Test_TextStyle_Fields.cpp"       // AYUI-Audit-2026-08-26: TextStyle field recording (MockRenderer drawText(styled))
#include "Test_CardStyle_Render.cpp"       // AYUI-Audit-2026-08-26: drawCard round-trip (MockRenderer Card recording)
#include "Test_WidgetSerializer_AllTypes.cpp" // AYUI-Audit-2026-08-26: parameterized round-trip across 29 widget types
#include "Test_DirtyRect.cpp"                // AYUI-Audit-2026-08-26 Batch B: dirty-rect system (markDirty / render short-circuit)
#include "Test_GetWorldBoundsCache.cpp"      // AYUI-Audit-2026-08-26 Batch C: worldBounds cache + markDescendantsBoundsDirty
#include "Test_TextAreaLifecycle.cpp"        // AYUI-Audit-2026-08-26 Batch A: ~TextArea _document leak regression
#include "Test_MenuBarLifecycle.cpp"         // AYUI-Audit-2026-08-26 Batch A: ~MenuBar dtor UAF regression

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
