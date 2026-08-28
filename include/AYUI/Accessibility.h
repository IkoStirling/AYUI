#pragma once

#include "AYMath/MathTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ayt::ui {

class Widget;

// Platform-neutral accessibility semantics.  AYUI deliberately exposes a
// snapshot instead of binding directly to UI Automation / AT-SPI / NSAccessibility;
// a host can translate the same tree to the native bridge used by its window.
enum class AccessibilityRole : uint8_t {
    Generic,
    Window,
    Dialog,
    Group,
    Button,
    CheckBox,
    RadioButton,
    StaticText,
    TextField,
    TextArea,
    Image,
    List,
    ListItem,
    Tree,
    TreeItem,
    TabList,
    Tab,
    Slider,
    ProgressBar,
    ComboBox,
    MenuBar,
    Menu,
    MenuItem,
    ToolBar,
    Separator,
    ScrollBar,
    RichText,
};

enum class AccessibilityAction : uint8_t {
    Focus,
    Press,
    Toggle,
    Select,
    Increment,
    Decrement,
    Expand,
    Collapse,
    Dismiss,
};

enum AccessibilityState : uint32_t {
    AccessibilityState_None          = 0,
    AccessibilityState_Enabled       = 1u << 0,
    AccessibilityState_Focusable     = 1u << 1,
    AccessibilityState_Focused       = 1u << 2,
    AccessibilityState_Checked       = 1u << 3,
    AccessibilityState_Selected      = 1u << 4,
    AccessibilityState_Expanded      = 1u << 5,
    AccessibilityState_ReadOnly      = 1u << 6,
    AccessibilityState_Password      = 1u << 7,
    AccessibilityState_Multiline     = 1u << 8,
    AccessibilityState_Indeterminate = 1u << 9,
    AccessibilityState_Offscreen     = 1u << 10,
};

constexpr uint32_t accessibilityActionMask(AccessibilityAction action) {
    return 1u << static_cast<uint32_t>(action);
}

struct AccessibilityNode {
    uint64_t id = 0;
    AccessibilityRole role = AccessibilityRole::Generic;
    std::wstring label;
    std::wstring description;
    std::wstring value;
    math::FRectangle bounds;
    uint32_t states = AccessibilityState_None;
    uint32_t actions = 0;
    std::vector<AccessibilityNode> children;
};

const char* accessibilityRoleName(AccessibilityRole role);
bool accessibilityRoleFromName(const std::string& name, AccessibilityRole& outRole);

} // namespace ayt::ui
