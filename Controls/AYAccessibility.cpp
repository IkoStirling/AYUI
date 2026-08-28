#include "AYUI/Accessibility.h"
#include "AYUI/UIManager.h"

#include "AYUI/Button.h"
#include "AYUI/CheckBox.h"
#include "AYUI/ComboBox.h"
#include "AYUI/FocusableWidget.h"
#include "AYUI/Image.h"
#include "AYUI/InteractiveWidget.h"
#include "AYUI/ListView.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuBar.h"
#include "AYUI/MenuItem.h"
#include "AYUI/Modal.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/RadioButton.h"
#include "AYUI/RichText.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/SelectableWidget.h"
#include "AYUI/Separator.h"
#include "AYUI/Slider.h"
#include "AYUI/TabStrip.h"
#include "AYUI/TextArea.h"
#include "AYUI/TextInput.h"
#include "AYUI/TextLabel.h"
#include "AYUI/ToolBar.h"
#include "AYUI/TreeNode.h"
#include "AYUI/TreeView.h"
#include "AYUI/Window.h"

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <sstream>

namespace ayt::ui {
namespace {

std::wstring numberValue(float value) {
    std::wostringstream stream;
    stream.precision(6);
    stream << value;
    return stream.str();
}

AccessibilityRole inferRole(const Widget* widget) {
    if (widget->hasExplicitAccessibilityRole()) return widget->getAccessibilityRole();
    if (dynamic_cast<const Modal*>(widget)) return AccessibilityRole::Dialog;
    if (dynamic_cast<const Window*>(widget)) return AccessibilityRole::Window;
    if (dynamic_cast<const CheckBox*>(widget)) return AccessibilityRole::CheckBox;
    if (dynamic_cast<const RadioButton*>(widget)) return AccessibilityRole::RadioButton;
    if (dynamic_cast<const Button*>(widget)) {
        return dynamic_cast<const TabStrip*>(widget->getParent())
            ? AccessibilityRole::Tab : AccessibilityRole::Button;
    }
    if (dynamic_cast<const TextInput*>(widget)) return AccessibilityRole::TextField;
    if (dynamic_cast<const TextArea*>(widget)) return AccessibilityRole::TextArea;
    if (dynamic_cast<const RichText*>(widget)) return AccessibilityRole::RichText;
    if (dynamic_cast<const TextLabel*>(widget)) return AccessibilityRole::StaticText;
    if (dynamic_cast<const Image*>(widget)) return AccessibilityRole::Image;
    if (dynamic_cast<const Slider*>(widget)) return AccessibilityRole::Slider;
    if (dynamic_cast<const ProgressBar*>(widget)) return AccessibilityRole::ProgressBar;
    if (dynamic_cast<const ScrollBar*>(widget)) return AccessibilityRole::ScrollBar;
    if (dynamic_cast<const ComboBox*>(widget)) return AccessibilityRole::ComboBox;
    if (dynamic_cast<const TabStrip*>(widget)) return AccessibilityRole::TabList;
    if (dynamic_cast<const TreeNode*>(widget)) return AccessibilityRole::TreeItem;
    if (dynamic_cast<const TreeView*>(widget)) return AccessibilityRole::Tree;
    if (dynamic_cast<const ListView::Row*>(widget)) return AccessibilityRole::ListItem;
    if (dynamic_cast<const ListView*>(widget)) return AccessibilityRole::List;
    if (dynamic_cast<const MenuItem*>(widget)) return AccessibilityRole::MenuItem;
    if (dynamic_cast<const MenuBar*>(widget)) return AccessibilityRole::MenuBar;
    if (dynamic_cast<const Menu*>(widget)) return AccessibilityRole::Menu;
    if (dynamic_cast<const ToolBar*>(widget)) return AccessibilityRole::ToolBar;
    if (dynamic_cast<const Separator*>(widget)) return AccessibilityRole::Separator;
    return widget->getChildren().empty() ? AccessibilityRole::Generic
                                         : AccessibilityRole::Group;
}

std::wstring inferLabel(const Widget* widget) {
    if (!widget->getAccessibilityLabel().empty()) return widget->getAccessibilityLabel();
    if (const auto* control = dynamic_cast<const Button*>(widget)) return control->getText();
    if (const auto* control = dynamic_cast<const CheckBox*>(widget)) return control->getText();
    if (const auto* control = dynamic_cast<const RadioButton*>(widget)) return control->getText();
    if (const auto* control = dynamic_cast<const TextLabel*>(widget)) return control->getText();
    if (const auto* control = dynamic_cast<const MenuItem*>(widget)) return control->getText();
    if (const auto* control = dynamic_cast<const TreeNode*>(widget)) return control->getLabel();
    if (const auto* control = dynamic_cast<const ListView::Row*>(widget)) return control->getText();
    if (const auto* control = dynamic_cast<const Window*>(widget)) return control->getTitle();
    if (const auto* control = dynamic_cast<const RichText*>(widget)) {
        std::wstring text;
        for (size_t i = 0; i < control->getRunCount(); ++i) text += control->getRun(i).text;
        return text;
    }
    return {};
}

std::wstring inferValue(const Widget* widget) {
    if (!widget->getAccessibilityValue().empty()) return widget->getAccessibilityValue();
    if (const auto* control = dynamic_cast<const TextInput*>(widget)) {
        return control->isPasswordMode()
            ? std::wstring(control->getText().size(), L'\x2022') : control->getText();
    }
    if (const auto* control = dynamic_cast<const TextArea*>(widget)) return control->getText();
    if (const auto* control = dynamic_cast<const Slider*>(widget)) return numberValue(control->getValue());
    if (const auto* control = dynamic_cast<const ProgressBar*>(widget)) return numberValue(control->getValue());
    if (const auto* control = dynamic_cast<const ScrollBar*>(widget)) return numberValue(control->getValue());
    if (const auto* control = dynamic_cast<const ComboBox*>(widget)) return control->getSelectedItem();
    return {};
}

uint32_t inferStates(const Widget* widget, const UIManager& manager) {
    uint32_t states = AccessibilityState_Enabled;
    if (const auto* interactive = dynamic_cast<const InteractiveWidget*>(widget)) {
        if (!interactive->isEnabled()) states &= ~AccessibilityState_Enabled;
    }
    if (const auto* combo = dynamic_cast<const ComboBox*>(widget)) {
        if (!combo->isEnabled()) states &= ~AccessibilityState_Enabled;
        states |= AccessibilityState_Focusable;
        if (combo->isPopupOpen()) states |= AccessibilityState_Expanded;
    }
    if (dynamic_cast<const FocusableWidget*>(widget)) states |= AccessibilityState_Focusable;
    if (manager.getFocusedWidget() == widget) states |= AccessibilityState_Focused;
    if (const auto* check = dynamic_cast<const CheckBox*>(widget); check && check->isChecked()) {
        states |= AccessibilityState_Checked;
    }
    if (const auto* radio = dynamic_cast<const RadioButton*>(widget); radio && radio->isChecked()) {
        states |= AccessibilityState_Checked | AccessibilityState_Selected;
    }
    if (const auto* selectable = dynamic_cast<const SelectableWidget*>(widget);
        selectable && selectable->isSelected()) {
        states |= AccessibilityState_Selected;
    }
    if (const auto* node = dynamic_cast<const TreeNode*>(widget); node && node->isExpanded()) {
        states |= AccessibilityState_Expanded;
    }
    if (const auto* input = dynamic_cast<const TextInput*>(widget)) {
        if (input->isReadOnly()) states |= AccessibilityState_ReadOnly;
        if (input->isPasswordMode()) states |= AccessibilityState_Password;
    }
    if (dynamic_cast<const TextArea*>(widget)) states |= AccessibilityState_Multiline;
    if (const auto* progress = dynamic_cast<const ProgressBar*>(widget);
        progress && progress->isIndeterminate()) {
        states |= AccessibilityState_Indeterminate;
    }
    const auto b = widget->getWorldBounds();
    const auto viewport = manager.getClientSize();
    if (b.maxX <= 0.0f || b.maxY <= 0.0f || b.minX >= viewport.x || b.minY >= viewport.y) {
        states |= AccessibilityState_Offscreen;
    }
    return states;
}

uint32_t inferActions(const Widget* widget) {
    uint32_t actions = 0;
    if (dynamic_cast<const FocusableWidget*>(widget)) {
        actions |= accessibilityActionMask(AccessibilityAction::Focus);
    }
    if (dynamic_cast<const InteractiveWidget*>(widget)
        || dynamic_cast<const ComboBox*>(widget)) {
        actions |= accessibilityActionMask(AccessibilityAction::Press);
    }
    if (dynamic_cast<const CheckBox*>(widget) || dynamic_cast<const RadioButton*>(widget)) {
        actions |= accessibilityActionMask(AccessibilityAction::Toggle);
    }
    if (dynamic_cast<const SelectableWidget*>(widget)) {
        actions |= accessibilityActionMask(AccessibilityAction::Select);
    }
    if (dynamic_cast<const Slider*>(widget) || dynamic_cast<const ScrollBar*>(widget)
        || dynamic_cast<const ComboBox*>(widget)) {
        actions |= accessibilityActionMask(AccessibilityAction::Increment)
                 | accessibilityActionMask(AccessibilityAction::Decrement);
    }
    if (const auto* node = dynamic_cast<const TreeNode*>(widget); node && node->hasChildren()) {
        actions |= accessibilityActionMask(AccessibilityAction::Expand)
                 | accessibilityActionMask(AccessibilityAction::Collapse);
    }
    return actions;
}

bool appendNode(const Widget* widget, const UIManager& manager,
                AccessibilityNode& parent) {
    if (widget == nullptr || !widget->isVisible() || widget->isAccessibilityHidden()) return false;
    AccessibilityNode node;
    node.id = widget->getAccessibilityId();
    node.role = inferRole(widget);
    node.label = inferLabel(widget);
    node.description = widget->getAccessibilityDescription();
    node.value = inferValue(widget);
    node.bounds = widget->getWorldBounds();
    node.states = inferStates(widget, manager);
    node.actions = inferActions(widget);
    for (const Widget* child : widget->getChildren()) appendNode(child, manager, node);
    parent.children.push_back(std::move(node));
    return true;
}

Widget* findNode(Widget* root, uint64_t id) {
    if (root == nullptr) return nullptr;
    if (root->getAccessibilityId() == id) return root;
    for (Widget* child : root->getChildren()) {
        if (Widget* found = findNode(child, id)) return found;
    }
    return nullptr;
}

} // namespace

const char* accessibilityRoleName(AccessibilityRole role) {
    static const char* names[] = {
        "Generic", "Window", "Dialog", "Group", "Button", "CheckBox",
        "RadioButton", "StaticText", "TextField", "TextArea", "Image",
        "List", "ListItem", "Tree", "TreeItem", "TabList", "Tab", "Slider",
        "ProgressBar", "ComboBox", "MenuBar", "Menu", "MenuItem", "ToolBar",
        "Separator", "ScrollBar", "RichText"
    };
    const size_t index = static_cast<size_t>(role);
    return index < (sizeof(names) / sizeof(names[0])) ? names[index] : names[0];
}

bool accessibilityRoleFromName(const std::string& name, AccessibilityRole& outRole) {
    for (uint32_t i = 0; i <= static_cast<uint32_t>(AccessibilityRole::RichText); ++i) {
        const auto role = static_cast<AccessibilityRole>(i);
        const char* candidate = accessibilityRoleName(role);
        const size_t length = std::char_traits<char>::length(candidate);
        const bool equal = name.size() == length
            && std::equal(name.begin(), name.end(), candidate,
                [](char left, char right) {
                    return std::tolower(static_cast<unsigned char>(left))
                        == std::tolower(static_cast<unsigned char>(right));
                });
        if (equal) {
            outRole = role;
            return true;
        }
    }
    return false;
}

AccessibilityNode UIManager::buildAccessibilityTree() const {
    AccessibilityNode snapshot;
    snapshot.role = AccessibilityRole::Group;
    snapshot.label = L"AYUI";
    snapshot.bounds = math::FRectangle(0.0f, 0.0f, _clientWidth, _clientHeight);
    snapshot.states = AccessibilityState_Enabled;
    appendNode(_root, *this, snapshot);
    if (_overlayRoot != nullptr) {
        for (const Widget* child : _overlayRoot->getChildren()) appendNode(child, *this, snapshot);
    }
    return snapshot;
}

bool UIManager::performAccessibilityAction(uint64_t nodeId,
                                           AccessibilityAction action) {
    Widget* widget = findNode(_root, nodeId);
    if (widget == nullptr) widget = findNode(_overlayRoot, nodeId);
    if (widget == nullptr || !widget->isVisible() || widget->isAccessibilityHidden()) return false;
    if (widget->_accessibilityActionHandler
        && widget->_accessibilityActionHandler(action)) return true;

    if (action == AccessibilityAction::Focus) {
        if (dynamic_cast<FocusableWidget*>(widget) == nullptr) return false;
        setFocus(widget);
        return true;
    }
    if (auto* check = dynamic_cast<CheckBox*>(widget);
        check && action == AccessibilityAction::Toggle) {
        check->setChecked(!check->isChecked());
        return true;
    }
    if (auto* radio = dynamic_cast<RadioButton*>(widget);
        radio && (action == AccessibilityAction::Toggle || action == AccessibilityAction::Select)) {
        radio->setChecked(true);
        return true;
    }
    if (auto* slider = dynamic_cast<Slider*>(widget);
        slider && (action == AccessibilityAction::Increment
                   || action == AccessibilityAction::Decrement)) {
        const float step = std::max((slider->getMax() - slider->getMin()) / 10.0f, 0.001f);
        slider->setValue(slider->getValue()
            + (action == AccessibilityAction::Increment ? step : -step));
        return true;
    }
    if (auto* scroll = dynamic_cast<ScrollBar*>(widget);
        scroll && (action == AccessibilityAction::Increment
                   || action == AccessibilityAction::Decrement)) {
        const float step = std::max((scroll->getMax() - scroll->getMin()) / 10.0f, 0.001f);
        scroll->setValue(scroll->getValue()
            + (action == AccessibilityAction::Increment ? step : -step));
        return true;
    }
    if (auto* combo = dynamic_cast<ComboBox*>(widget);
        combo && (action == AccessibilityAction::Increment
                  || action == AccessibilityAction::Decrement)) {
        if (combo->getItemCount() == 0) return false;
        const int delta = action == AccessibilityAction::Increment ? 1 : -1;
        combo->setSelectedIndex(std::clamp(combo->getSelectedIndex() + delta, 0,
            static_cast<int>(combo->getItemCount()) - 1));
        return true;
    }
    if (auto* node = dynamic_cast<TreeNode*>(widget)) {
        if (action == AccessibilityAction::Expand && node->hasChildren()) {
            node->setExpanded(true);
            return true;
        }
        if (action == AccessibilityAction::Collapse && node->hasChildren()) {
            node->setExpanded(false);
            return true;
        }
    }
    if (action == AccessibilityAction::Press || action == AccessibilityAction::Select) {
        const auto b = widget->getWorldBounds();
        const math::FVector2 center((b.minX + b.maxX) * 0.5f,
                                    (b.minY + b.maxY) * 0.5f);
        const bool down = widget->onMouseButtonDown(UIMouseEvent(center, 0));
        const bool up = widget->onMouseButtonUp(UIMouseEvent(center, 0));
        return down || up;
    }
    return false;
}

} // namespace ayt::ui
