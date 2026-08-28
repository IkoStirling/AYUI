#include "AYUI/WidgetSerializer.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/TextLabel.h"
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
#include "AYUI/ComboBox.h"
#include "AYUI/Window.h"
#include "AYUI/Panel.h"
#include "AYUI/Box.h"
#include "AYUI/SplitterHandle.h"
#include "AYUI/Image.h"
#include "AYUI/TabControl.h"
#include "AYUI/GridPanel.h"
#include "AYUI/TreeNode.h"
#include "AYUI/TreeView.h"
#include "AYUI/RichText.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"
#include "AYUI/Dimmer.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/TabStrip.h"
#include <nlohmann/json.hpp>
#include <codecvt>
#include <locale>
#include <memory>

using namespace ayt::ui;
using json = nlohmann::json;

static std::wstring toWstring(const std::string& str) {
    if constexpr (sizeof(wchar_t) == 2) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(str);
    } else {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(str);
    }
}

static std::string toUtf8(const std::wstring& str) {
    if constexpr (sizeof(wchar_t) == 2) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.to_bytes(str);
    } else {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(str);
    }
}

static bool hasStructuredChildPayload(Widget* widget) {
    return dynamic_cast<ScrollView*>(widget) != nullptr
        || dynamic_cast<ListView*>(widget) != nullptr
        || dynamic_cast<ComboBox*>(widget) != nullptr
        || dynamic_cast<TreeView*>(widget) != nullptr
        || dynamic_cast<MenuItem*>(widget) != nullptr
        || dynamic_cast<Menu*>(widget) != nullptr
        || dynamic_cast<MenuBar*>(widget) != nullptr
        || dynamic_cast<ToolBar*>(widget) != nullptr
        || dynamic_cast<StatusBar*>(widget) != nullptr
        || dynamic_cast<GridPanel*>(widget) != nullptr
        || dynamic_cast<TabControl*>(widget) != nullptr
        || dynamic_cast<TabStrip*>(widget) != nullptr
        || dynamic_cast<DockCard*>(widget) != nullptr
        || dynamic_cast<DockArea*>(widget) != nullptr
        || dynamic_cast<DockOverlay*>(widget) != nullptr
        || dynamic_cast<Modal*>(widget) != nullptr;
}

std::string WidgetSerializer::serialize(Widget* root, bool pretty) {
    if (!root) return "{}";

    json j;
    serializeWidgetToJson(root, j);

    if (pretty) {
        return j.dump(4);
    }
    return j.dump();
}

std::string WidgetSerializer::serializeWidget(Widget* widget) {
    if (!widget) return "{}";

    json j;
    serializeWidgetToJson(widget, j);
    return j.dump(4);
}

Widget* WidgetSerializer::deserialize(const std::string& jsonStr) {
    if (jsonStr.empty()) return nullptr;

    try {
        json j = json::parse(jsonStr);
        if (!j.is_object()) return nullptr;

        std::string type = j.value("type", "Widget");
        WidgetFactory& factory = WidgetFactory::get();
        Widget* widget = factory.create(type);
        if (!widget) widget = new Widget();

        widget->setId(j.value("id", ""));

        if (j.contains("position")) {
            float x = j["position"].value("x", 0.0f);
            float y = j["position"].value("y", 0.0f);
            widget->setPosition(math::FVector2(x, y));
        }

        if (j.contains("size")) {
            float w = j["size"].value("w", 100.0f);
            float h = j["size"].value("h", 50.0f);
            widget->setSize(math::FVector2(w, h));
        }

        widget->setVisible(j.value("visible", true));
        widget->setStyleId(j.value("style", ""));
        widget->setOpacity(j.value("opacity", 1.0f));
        widget->setLayoutPositionManaged(j.value("layoutPositionManaged", true));
        widget->setLayoutSizeManaged(j.value("layoutSizeManaged", true));
        widget->setAccessibilityHidden(j.value("accessibilityHidden", false));
        if (j.contains("accessibilityRole") && j["accessibilityRole"].is_string()) {
            AccessibilityRole role;
            if (accessibilityRoleFromName(j["accessibilityRole"].get<std::string>(), role)) {
                widget->setAccessibilityRole(role);
            }
        }
        if (j.contains("accessibilityLabel")) {
            widget->setAccessibilityLabel(toWstring(j["accessibilityLabel"].get<std::string>()));
        }
        if (j.contains("accessibilityDescription")) {
            widget->setAccessibilityDescription(
                toWstring(j["accessibilityDescription"].get<std::string>()));
        }
        if (j.contains("accessibilityValue")) {
            widget->setAccessibilityValue(toWstring(j["accessibilityValue"].get<std::string>()));
        }

        // G11 — per-widget theme token overrides. JSON shape:
        //   "styleOverrides": {
        //     "color.bg.surface": [r, g, b, a],
        //     "color.accent":    [r, g, b, a]
        //   }
        // Each entry calls setStyleTokenOverride(key, value). An empty
        // array clears ALL prior overrides (so a widget that wants to
        // drop its overrides can write `"styleOverrides": []` or omit
        // the key — both paths leave _tokenOverrides empty).
        if (j.contains("styleOverrides")) {
            const auto& so = j["styleOverrides"];
            if (so.is_array()) {
                // Sentinel: empty array = explicit clear.
                widget->clearStyleTokenOverrides();
            } else if (so.is_object()) {
                for (auto it = so.begin(); it != so.end(); ++it) {
                    if (it.value().is_array() && it.value().size() == 4 &&
                        it.value()[0].is_number() &&
                        it.value()[1].is_number() &&
                        it.value()[2].is_number() &&
                        it.value()[3].is_number()) {
                        math::FVector4 c(
                            it.value()[0].get<float>(),
                            it.value()[1].get<float>(),
                            it.value()[2].get<float>(),
                            it.value()[3].get<float>());
                        widget->setStyleTokenOverride(it.key(), c);
                    }
                }
            }
        }

        if (TextLabel* label = dynamic_cast<TextLabel*>(widget)) {
            if (j.contains("text")) {
                label->setText(toWstring(j["text"].get<std::string>()));
            }
            if (j.contains("fontSize")) {
                label->setFontSize(j["fontSize"]);
            }
            if (j.contains("hAlign")) {
                if (j["hAlign"].is_string()) {
                    const std::string a = j["hAlign"].get<std::string>();
                    if (a == "Center") {
                        label->setHorizontalAlignment(TextLabel::HAlignment::Center);
                    } else if (a == "Right") {
                        label->setHorizontalAlignment(TextLabel::HAlignment::Right);
                    } else {
                        label->setHorizontalAlignment(TextLabel::HAlignment::Left);
                    }
                } else if (j["hAlign"].is_number_integer()) {
                    const int a = j["hAlign"].get<int>();
                    if (a == 1) {
                        label->setHorizontalAlignment(TextLabel::HAlignment::Center);
                    } else if (a == 2) {
                        label->setHorizontalAlignment(TextLabel::HAlignment::Right);
                    } else {
                        label->setHorizontalAlignment(TextLabel::HAlignment::Left);
                    }
                }
            }
            if (j.contains("vAlign")) {
                if (j["vAlign"].is_string()) {
                    const std::string a = j["vAlign"].get<std::string>();
                    if (a == "Center" || a == "Middle") {
                        label->setVerticalAlignment(TextLabel::VAlignment::Center);
                    } else if (a == "Bottom") {
                        label->setVerticalAlignment(TextLabel::VAlignment::Bottom);
                    } else {
                        label->setVerticalAlignment(TextLabel::VAlignment::Top);
                    }
                } else if (j["vAlign"].is_number_integer()) {
                    const int a = j["vAlign"].get<int>();
                    if (a == 1) {
                        label->setVerticalAlignment(TextLabel::VAlignment::Center);
                    } else if (a == 2) {
                        label->setVerticalAlignment(TextLabel::VAlignment::Bottom);
                    } else {
                        label->setVerticalAlignment(TextLabel::VAlignment::Top);
                    }
                }
            }
        }

        if (Button* button = dynamic_cast<Button*>(widget)) {
            if (j.contains("text")) {
                button->setText(toWstring(j["text"].get<std::string>()));
            }
        }

        if (CheckBox* cb = dynamic_cast<CheckBox*>(widget)) {
            if (j.contains("text")) {
                cb->setText(toWstring(j["text"].get<std::string>()));
            }
            if (j.contains("checked")) {
                cb->setChecked(j["checked"].get<bool>());
            }
        }

        if (RadioButton* rb = dynamic_cast<RadioButton*>(widget)) {
            if (j.contains("text")) {
                rb->setText(toWstring(j["text"].get<std::string>()));
            }
            if (j.contains("checked")) {
                rb->setChecked(j["checked"].get<bool>());
            }
            if (j.contains("groupId")) {
                rb->setGroupId(j["groupId"].get<int>());
            }
        }

        if (Slider* sl = dynamic_cast<Slider*>(widget)) {
            if (j.contains("min"))  sl->setMin(j["min"].get<float>());
            if (j.contains("max"))  sl->setMax(j["max"].get<float>());
            if (j.contains("value")) sl->setValue(j["value"].get<float>());
        }

        if (ProgressBar* pb = dynamic_cast<ProgressBar*>(widget)) {
            if (j.contains("min"))  pb->setMin(j["min"].get<float>());
            if (j.contains("max"))  pb->setMax(j["max"].get<float>());
            if (j.contains("value")) pb->setValue(j["value"].get<float>());
        }

        if (TextInput* ti = dynamic_cast<TextInput*>(widget)) {
            if (j.contains("text")) {
                ti->setText(toWstring(j["text"].get<std::string>()));
            }
            if (j.contains("password")) {
                ti->setPasswordMode(j["password"].get<bool>());
            }
            if (j.contains("readOnly")) {
                ti->setReadOnly(j["readOnly"].get<bool>());
            }
            if (j.contains("maxLength")) {
                ti->setMaxLength(static_cast<size_t>(j["maxLength"].get<int>()));
            }
            // G6 — HAlign round-trip. 0 = Left, 1 = Center, 2 = Right.
            if (j.contains("hAlign")) {
                const int a = j["hAlign"].get<int>();
                if (a == 1) ti->setHAlign(TextInput::HAlign::Center);
                else if (a == 2) ti->setHAlign(TextInput::HAlign::Right);
                else ti->setHAlign(TextInput::HAlign::Left);
            }
        }

        if (TextArea* ta = dynamic_cast<TextArea*>(widget)) {
            if (j.contains("text")) {
                ta->setText(toWstring(j["text"].get<std::string>()));
            }
            if (j.contains("readOnly")) {
                ta->setReadOnly(j["readOnly"].get<bool>());
            }
            if (j.contains("maxLength")) {
                ta->setMaxLength(static_cast<size_t>(j["maxLength"].get<int>()));
            }
            if (j.contains("lineHeight")) {
                ta->setLineHeight(j["lineHeight"].get<float>());
            }
        }

        if (ScrollBar* sb = dynamic_cast<ScrollBar*>(widget)) {
            if (j.contains("orientation")) {
                std::string o = j["orientation"].get<std::string>();
                if (o == "vertical") sb->setOrientation(ScrollBar::Orientation::Vertical);
                else if (o == "horizontal") sb->setOrientation(ScrollBar::Orientation::Horizontal);
            }
        }

        if (ScrollView* scroll = dynamic_cast<ScrollView*>(widget)) {
            if (j.contains("verticalScrollBar")) {
                scroll->setVerticalScrollBarEnabled(j["verticalScrollBar"].get<bool>());
            }
            if (j.contains("horizontalScrollBar")) {
                scroll->setHorizontalScrollBarEnabled(j["horizontalScrollBar"].get<bool>());
            }
            if (j.contains("contentSize") && j["contentSize"].is_object()) {
                scroll->setContentSize(math::FVector2(
                    j["contentSize"].value("w", 0.0f),
                    j["contentSize"].value("h", 0.0f)));
            }
            if (j.contains("content") && j["content"].is_object()) {
                if (Widget* content = deserialize(j["content"].dump())) {
                    scroll->setContentOwned(content);
                }
            }
            if (j.contains("scrollOffset") && j["scrollOffset"].is_object()) {
                scroll->setScrollOffset(math::FVector2(
                    j["scrollOffset"].value("x", 0.0f),
                    j["scrollOffset"].value("y", 0.0f)));
            }
        }

        if (ListView* lv = dynamic_cast<ListView*>(widget)) {
            if (j.contains("items") && j["items"].is_array()) {
                std::vector<std::wstring> items;
                items.reserve(j["items"].size());
                for (const auto& s : j["items"]) {
                    items.push_back(toWstring(s.get<std::string>()));
                }
                lv->setItems(items);
            }
            if (j.contains("selectedIndex")) {
                lv->setSelectedIndex(j["selectedIndex"].get<int>());
            }
            // G1 — multi-select round-trip. `selectionMode` is an int
            // (0 = Single, 1 = Extended) so we don't depend on enum-string
            // conversions. `selectedIndices` is an array; only meaningful
            // in Extended mode but we apply it regardless so a Single-mode
            // ListView with a single-element array round-trips cleanly.
            if (j.contains("selectionMode")) {
                const int mode = j["selectionMode"].get<int>();
                lv->setSelectionMode(static_cast<ListView::SelectionMode>(
                    mode == 1 ? 1 : 0));
            }
            if (j.contains("selectedIndices") && j["selectedIndices"].is_array()) {
                std::vector<int> v;
                for (const auto& s : j["selectedIndices"]) {
                    v.push_back(s.get<int>());
                }
                lv->setSelectedIndices(v);
            }
            if (j.contains("itemHeight")) {
                lv->setItemHeight(j["itemHeight"].get<float>());
            }
        }

        if (ComboBox* cb = dynamic_cast<ComboBox*>(widget)) {
            if (j.contains("items") && j["items"].is_array()) {
                std::vector<std::wstring> items;
                items.reserve(j["items"].size());
                for (const auto& s : j["items"]) {
                    items.push_back(toWstring(s.get<std::string>()));
                }
                cb->setItems(items);
            }
            if (j.contains("selectedIndex")) {
                cb->setSelectedIndex(j["selectedIndex"].get<int>());
            }
            if (j.contains("maxPopupItems")) {
                cb->setMaxPopupItems(j["maxPopupItems"].get<int>());
            }
        }

        if (TabControl* tc = dynamic_cast<TabControl*>(widget)) {
            // Round-trip: tabs[] { label, content (recursive deserialize) } +
            // selectedIndex + headerHeight. Content widgets are owned by the
            // caller (see DECISION 2 in AYTabControl.h); the JSON just nests
            // them so the round-trip is lossless.
            if (j.contains("tabs") && j["tabs"].is_array()) {
                for (const auto& t : j["tabs"]) {
                    std::string label = t.value("label", "");
                    Widget* content = nullptr;
                    if (t.contains("content") && t["content"].is_object()) {
                        content = deserialize(t["content"].dump());
                    }
                    tc->addTabOwned(toWstring(label), content);
                }
            }
            if (j.contains("selectedIndex")) {
                tc->setSelectedIndex(j["selectedIndex"].get<int>());
            }
            if (j.contains("headerHeight")) {
                tc->setHeaderHeight(j["headerHeight"].get<float>());
            }
        }

        if (Window* window = dynamic_cast<Window*>(widget)) {
            if (j.contains("title")) {
                window->setTitle(toWstring(j["title"].get<std::string>()));
            }
            if (j.contains("titleBarHeight")) {
                window->setTitleBarHeight(j["titleBarHeight"]);
            }
            if (j.contains("movable")) {
                window->setMovable(j["movable"].get<bool>());
            }
            if (j.contains("resizable")) {
                window->setResizable(j["resizable"].get<bool>());
            }
            if (j.contains("minSize") && j["minSize"].is_object()) {
                window->setMinSize(j["minSize"].value("w", 120.0f),
                                   j["minSize"].value("h", 80.0f));
            }
        }

        if (Panel* panel = dynamic_cast<Panel*>(widget)) {
            if (j.contains("borderEnabled")) {
                panel->setBorderEnabled(j["borderEnabled"].get<bool>());
            }
            if (j.contains("padding") && j["padding"].is_object()) {
                panel->setPadding(
                    j["padding"].value("left", 4.0f),
                    j["padding"].value("top", 4.0f),
                    j["padding"].value("right", 4.0f),
                    j["padding"].value("bottom", 4.0f));
            }
            if (j.contains("backgroundEnabled")) {
                panel->setBackgroundEnabled(j["backgroundEnabled"].get<bool>());
            }
        }

        if (GridPanel* gp = dynamic_cast<GridPanel*>(widget)) {
            const int rows = j.value("rowCount", 0);
            const int cols = j.value("columnCount", 0);
            if (rows > 0) gp->setRowCount(rows);
            if (cols > 0) gp->setColumnCount(cols);

            if (j.contains("rowDefs") && j["rowDefs"].is_array()) {
                int row = 0;
                for (const auto& defJson : j["rowDefs"]) {
                    if (row >= gp->getRowCount()) break;
                    GridPanel::RowDef def;
                    def.policy = defJson.value("policy", std::string("Stretch")) == "Fixed"
                        ? GridPanel::SizePolicy::Fixed
                        : GridPanel::SizePolicy::Stretch;
                    def.value = defJson.value("value", 1.0f);
                    gp->setRowDef(row++, def);
                }
            }
            if (j.contains("columnDefs") && j["columnDefs"].is_array()) {
                int col = 0;
                for (const auto& defJson : j["columnDefs"]) {
                    if (col >= gp->getColumnCount()) break;
                    GridPanel::ColDef def;
                    def.policy = defJson.value("policy", std::string("Stretch")) == "Fixed"
                        ? GridPanel::SizePolicy::Fixed
                        : GridPanel::SizePolicy::Stretch;
                    def.value = defJson.value("value", 1.0f);
                    gp->setColumnDef(col++, def);
                }
            }
            if (j.contains("padding") && j["padding"].is_object()) {
                gp->setPadding(j["padding"].value("left", 4.0f),
                               j["padding"].value("top", 4.0f),
                               j["padding"].value("right", 4.0f),
                               j["padding"].value("bottom", 4.0f));
            }
            if (j.contains("spacing") && j["spacing"].is_object()) {
                gp->setSpacing(j["spacing"].value("horizontal", 4.0f),
                               j["spacing"].value("vertical", 4.0f));
            }

            const auto parseHAlign = [](const std::string& value) {
                if (value == "Left") return GridPanel::HAlign::Left;
                if (value == "Center") return GridPanel::HAlign::Center;
                if (value == "Right") return GridPanel::HAlign::Right;
                return GridPanel::HAlign::Fill;
            };
            const auto parseVAlign = [](const std::string& value) {
                if (value == "Top") return GridPanel::VAlign::Top;
                if (value == "Middle") return GridPanel::VAlign::Middle;
                if (value == "Bottom") return GridPanel::VAlign::Bottom;
                return GridPanel::VAlign::Fill;
            };

            const bool hasStructuredCells = j.contains("cells") && j["cells"].is_array();
            if (hasStructuredCells) {
                for (const auto& cellJson : j["cells"]) {
                    const int row = cellJson.value("row", -1);
                    const int col = cellJson.value("col", -1);
                    if (row < 0 || col < 0 || row >= gp->getRowCount()
                        || col >= gp->getColumnCount()
                        || !cellJson.contains("content")
                        || !cellJson["content"].is_object()) {
                        continue;
                    }
                    Widget* child = deserialize(cellJson["content"].dump());
                    if (child == nullptr) continue;
                    gp->setCell(
                        row, col, child,
                        cellJson.value("rowSpan", 1),
                        cellJson.value("colSpan", 1),
                        parseHAlign(cellJson.value("hAlign", std::string("Fill"))),
                        parseVAlign(cellJson.value("vAlign", std::string("Fill"))));
                }
            } else if (rows > 0 && cols > 0 && j.contains("children")
                       && j["children"].is_array()) {
                // Backward compatibility for the old lossy linear payload.
                int row = 0;
                int col = 0;
                for (const auto& childJson : j["children"]) {
                    Widget* child = deserialize(childJson.dump());
                    if (child == nullptr) continue;
                    gp->setCell(row, col, child);
                    if (++col >= cols) {
                        col = 0;
                        ++row;
                    }
                    if (row >= rows) break;
                }
            }
        }

        if (Tooltip* tip = dynamic_cast<Tooltip*>(widget)) {
            if (j.contains("text")) {
                tip->setText(toWstring(j["text"].get<std::string>()));
            }
            if (j.contains("hoverDelay")) {
                tip->setHoverDelay(j["hoverDelay"].get<float>());
            }
        }

        if (Separator* sep = dynamic_cast<Separator*>(widget)) {
            if (j.contains("orientation")) {
                std::string o = j["orientation"].get<std::string>();
                if (o == "vertical") {
                    sep->setOrientation(Separator::Orientation::Vertical);
                } else if (o == "horizontal") {
                    sep->setOrientation(Separator::Orientation::Horizontal);
                }
            }
            if (j.contains("thickness")) {
                sep->setThickness(j["thickness"].get<float>());
            }
            if (j.contains("inset")) {
                sep->setInset(j["inset"].get<float>());
            }
            if (j.contains("color") && j["color"].is_object()) {
                const auto& c = j["color"];
                sep->setColor(math::FVector4(
                    c.value("r", 1.0f), c.value("g", 1.0f),
                    c.value("b", 1.0f), c.value("a", 1.0f)));
            }
        }

        // G7 — ToolBarSeparator inherits all field handling from the
        // Separator branch above (orientation/thickness/inset). The
        // toolbar palette is set in the constructor; no extra fields
        // need to be deserialized here.

        if (MenuItem* mi = dynamic_cast<MenuItem*>(widget)) {
            if (j.contains("text")) {
                mi->setText(toWstring(j["text"].get<std::string>()));
            }
            if (j.contains("shortcut")) {
                mi->setShortcut(toWstring(j["shortcut"].get<std::string>()));
            }
            if (j.contains("submenu") && j["submenu"].is_object()) {
                Widget* submenuWidget = deserialize(j["submenu"].dump());
                Menu* submenu = dynamic_cast<Menu*>(submenuWidget);
                if (submenu != nullptr) {
                    mi->setSubmenu(submenu);
                    mi->addChild(submenu);
                }
                else destroyWidgetTree(submenuWidget);
            }
        }

        if (Menu* menu = dynamic_cast<Menu*>(widget)) {
            if (j.contains("items") && j["items"].is_array()) {
                for (const auto& itemJson : j["items"]) {
                    MenuItem* item = menu->addItem(toWstring(
                        itemJson.value("text", std::string())));
                    if (itemJson.contains("shortcut")) {
                        item->setShortcut(toWstring(
                            itemJson["shortcut"].get<std::string>()));
                    }
                    if (itemJson.contains("submenu") && itemJson["submenu"].is_object()) {
                        Widget* submenuWidget = deserialize(itemJson["submenu"].dump());
                        Menu* submenu = dynamic_cast<Menu*>(submenuWidget);
                        if (submenu != nullptr) menu->attachSubmenu(item, submenu);
                        else destroyWidgetTree(submenuWidget);
                    }
                }
            }
        }

        if (MenuBar* menuBar = dynamic_cast<MenuBar*>(widget)) {
            if (j.contains("anchorSpacing")) menuBar->setAnchorSpacing(j["anchorSpacing"].get<float>());
            if (j.contains("anchorWidth")) menuBar->setAnchorWidth(j["anchorWidth"].get<float>());
            if (j.contains("anchorAutoWidth")) menuBar->setAnchorAutoWidth(j["anchorAutoWidth"].get<bool>());
            if (j.contains("menus") && j["menus"].is_array()) {
                for (const auto& menuJson : j["menus"]) {
                    Menu* menu = menuBar->addMenu(toWstring(
                        menuJson.value("title", std::string())));
                    const json* payload = &menuJson;
                    if (menuJson.contains("menu") && menuJson["menu"].is_object()) {
                        payload = &menuJson["menu"];
                    }
                    if (payload->contains("items") && (*payload)["items"].is_array()) {
                        for (const auto& itemJson : (*payload)["items"]) {
                            MenuItem* item = menu->addItem(toWstring(
                                itemJson.value("text", std::string())));
                            if (itemJson.contains("shortcut")) {
                                item->setShortcut(toWstring(
                                    itemJson["shortcut"].get<std::string>()));
                            }
                            if (itemJson.contains("submenu")
                                && itemJson["submenu"].is_object()) {
                                Widget* submenuWidget = deserialize(itemJson["submenu"].dump());
                                Menu* submenu = dynamic_cast<Menu*>(submenuWidget);
                                if (submenu != nullptr) menu->attachSubmenu(item, submenu);
                                else destroyWidgetTree(submenuWidget);
                            }
                        }
                    }
                }
            }
        }

        if (StatusBar* sb = dynamic_cast<StatusBar*>(widget)) {
            if (j.contains("panels") && j["panels"].is_array()) {
                for (const auto& pj : j["panels"]) {
                    if (pj.contains("type")) {
                        if (Widget* panel = deserialize(pj.dump())) {
                            sb->addPanel(panel);
                        }
                    } else if (pj.contains("text")) {
                        sb->addPanel(toWstring(
                            pj["text"].get<std::string>()));
                    }
                }
            }
        }

        if (TreeNode* tn = dynamic_cast<TreeNode*>(widget)) {
            if (j.contains("label")) {
                tn->setLabel(toWstring(j["label"].get<std::string>()));
            }
            if (j.contains("icon")) {
                tn->setIcon(toWstring(j["icon"].get<std::string>()));
            }
            if (j.contains("hasChildren")) {
                tn->setHasChildren(j["hasChildren"].get<bool>());
            }
            if (j.contains("expanded")) {
                tn->setExpanded(j["expanded"].get<bool>());
            }
            if (j.contains("depth")) {
                tn->setDepth(j["depth"].get<int>());
            }
        }

        if (TreeView* tv = dynamic_cast<TreeView*>(widget)) {
            if (j.contains("tree") && j["tree"].is_array()) {
                std::vector<TreeNodeData> nodes;
                nodes.reserve(j["tree"].size());
                for (const auto& n : j["tree"]) {
                    TreeNodeData d;
                    d.label = n.contains("label")
                        ? toWstring(n["label"].get<std::string>())
                        : std::wstring();
                    d.icon = n.contains("icon")
                        ? toWstring(n["icon"].get<std::string>())
                        : std::wstring();
                    d.hasChildren = n.value("hasChildren", false);
                    d.expanded   = n.value("expanded", false);
                    d.parentIndex = n.value("parentIndex", -1);
                    nodes.push_back(d);
                }
                tv->setTree(nodes);
            }
            if (j.contains("selectedIndex")) {
                tv->setSelectedIndex(j["selectedIndex"].get<int>());
            }
            if (j.contains("itemHeight")) {
                tv->setItemHeight(j["itemHeight"].get<float>());
            }
        }

        if (RichText* rt = dynamic_cast<RichText*>(widget)) {
            rt->clearRuns();
            if (j.contains("defaultColor") && j["defaultColor"].is_object()) {
                rt->setDefaultColor(math::FVector4(
                    j["defaultColor"].value("r", 1.0f),
                    j["defaultColor"].value("g", 1.0f),
                    j["defaultColor"].value("b", 1.0f),
                    j["defaultColor"].value("a", 1.0f)));
            }
            if (j.contains("defaultFontSize")) {
                rt->setDefaultFontSize(j["defaultFontSize"].get<int>());
            }
            if (j.contains("wrapWidth")) {
                rt->setWrapWidth(j["wrapWidth"].get<float>());
            }
            if (j.contains("wrapMode")) {
                const int value = j["wrapMode"].get<int>();
                if (value >= 0 && value <= static_cast<int>(RichTextWrapMode::Character))
                    rt->setWrapMode(static_cast<RichTextWrapMode>(value));
            }
            if (j.contains("alignment")) {
                const int value = j["alignment"].get<int>();
                if (value >= 0 && value <= static_cast<int>(RichTextAlignment::Justify))
                    rt->setAlignment(static_cast<RichTextAlignment>(value));
            }
            if (j.contains("verticalAlignment")) {
                const int value = j["verticalAlignment"].get<int>();
                if (value >= 0 && value <= static_cast<int>(RichTextVerticalAlignment::Bottom))
                    rt->setVerticalAlignment(static_cast<RichTextVerticalAlignment>(value));
            }
            if (j.contains("overflow")) {
                const int value = j["overflow"].get<int>();
                if (value >= 0 && value <= static_cast<int>(RichTextOverflow::Ellipsis))
                    rt->setOverflow(static_cast<RichTextOverflow>(value));
            }
            if (j.contains("lineHeight")) rt->setLineHeight(j["lineHeight"].get<float>());
            if (j.contains("lineSpacing")) rt->setLineSpacing(j["lineSpacing"].get<float>());
            if (j.contains("maxLines")) rt->setMaxLines(j["maxLines"].get<size_t>());
            if (j.contains("runs") && j["runs"].is_array()) {
                for (const auto& r : j["runs"]) {
                    math::FVector4 col = rt->getDefaultColor();
                    int size = rt->getDefaultFontSize();
                    if (r.contains("color") && r["color"].is_object()) {
                        col = math::FVector4(
                            r["color"].value("r", 1.0f),
                            r["color"].value("g", 1.0f),
                            r["color"].value("b", 1.0f),
                            r["color"].value("a", 1.0f));
                    }
                    if (r.contains("fontSize")) {
                        size = r["fontSize"].get<int>();
                    }
                    if (r.contains("text")) {
                        RichRun run;
                        run.text = toWstring(r["text"].get<std::string>());
                        run.color = col;
                        run.fontSize = size;
                        run.bold = r.value("bold", false);
                        run.italic = r.value("italic", false);
                        run.underline = r.value("underline", false);
                        run.strikethrough = r.value("strikethrough", false);
                        run.letterSpacing = r.value("letterSpacing", 0.0f);
                        run.baselineShift = r.value("baselineShift", 0.0f);
                        rt->addRun(run);
                    }
                }
            }
        }

        if (BoxBase* box = dynamic_cast<BoxBase*>(widget)) {
            if (j.contains("spacing")) {
                box->setSpacing(j["spacing"]);
            }
            if (j.contains("padding")) {
                float l = j["padding"].value("left", 4.0f);
                float t = j["padding"].value("top", 4.0f);
                float r = j["padding"].value("right", 4.0f);
                float b = j["padding"].value("bottom", 4.0f);
                box->setPadding(l, t, r, b);
            }
            if (j.contains("gravity") && j["gravity"].is_string()) {
                const std::string gravity = j["gravity"].get<std::string>();
                if (gravity == "TopCenter") box->setGravity(BoxBase::Gravity::TopCenter);
                else if (gravity == "TopRight") box->setGravity(BoxBase::Gravity::TopRight);
                else if (gravity == "CenterLeft") box->setGravity(BoxBase::Gravity::CenterLeft);
                else if (gravity == "Center") box->setGravity(BoxBase::Gravity::Center);
                else if (gravity == "CenterRight") box->setGravity(BoxBase::Gravity::CenterRight);
                else if (gravity == "BottomLeft") box->setGravity(BoxBase::Gravity::BottomLeft);
                else if (gravity == "BottomCenter") box->setGravity(BoxBase::Gravity::BottomCenter);
                else if (gravity == "BottomRight") box->setGravity(BoxBase::Gravity::BottomRight);
                else box->setGravity(BoxBase::Gravity::TopLeft);
            }
        }

        if (Image* img = dynamic_cast<Image*>(widget)) {
            // G10 — round-trip textureName → TextureRegistry::acquire.
            // The width/height we record here are metadata; the actual
            // backend handle is filled in by the host's loader once the
            // PNG is decoded. Missing textureName = anonymous (no
            // registry touch).
            if (j.contains("textureName")) {
                const std::string name =
                    j["textureName"].get<std::string>();
                if (!name.empty()) {
                    img->setTexture(name);
                    // Width/height captured via setTexture(name); if the
                    // registry already has an entry, _tex.width/height
                    // come from there. Otherwise we leave them 0 and the
                    // host fills them in via registerExternal() later.
                    (void)j["textureWidth"];   // metadata only
                    (void)j["textureHeight"];
                }
            }
        }

        if (SplitterHandle* splitter = dynamic_cast<SplitterHandle*>(widget)) {
            if (j.value("orientation", std::string("horizontal")) == "vertical") {
                splitter->setOrientation(SplitterHandle::Orientation::Vertical);
            } else {
                splitter->setOrientation(SplitterHandle::Orientation::Horizontal);
            }
        }

        if (DockCard* card = dynamic_cast<DockCard*>(widget)) {
            if (j.contains("title")) card->setTitle(toWstring(j["title"].get<std::string>()));
            if (j.contains("icon")) card->setIcon(j["icon"].get<std::string>());
            if (j.contains("closable")) card->setClosable(j["closable"].get<bool>());
            if (j.contains("floatable")) card->setFloatable(j["floatable"].get<bool>());
            if (j.contains("collapsed")) card->setCollapsed(j["collapsed"].get<bool>());
            if (j.contains("headerHeight")) card->setHeaderHeight(j["headerHeight"].get<float>());
            if (j.contains("content") && j["content"].is_object()) {
                if (Widget* content = deserialize(j["content"].dump())) {
                    card->setContent(content);
                }
            }
        }

        if (DockOverlay* overlay = dynamic_cast<DockOverlay*>(widget)) {
            if (j.contains("floating") && j["floating"].is_array()) {
                for (const auto& floatingJson : j["floating"]) {
                    Widget* child = deserialize(floatingJson.dump());
                    DockCard* card = dynamic_cast<DockCard*>(child);
                    if (card == nullptr) {
                        destroyWidgetTree(child);
                        continue;
                    }
                    card->setPosition(math::FVector2(
                        floatingJson.value("x", card->getPosition().x),
                        floatingJson.value("y", card->getPosition().y)));
                    card->setSize(math::FVector2(
                        floatingJson.value("w", card->getSize().x),
                        floatingJson.value("h", card->getSize().y)));
                    overlay->addFloatingCard(card);
                }
            }
        }

        if (DockArea* dock = dynamic_cast<DockArea*>(widget)) {
            const auto applySlotValues = [dock](const json& values, bool minimums) {
                if (!values.is_object()) return;
                for (auto it = values.begin(); it != values.end(); ++it) {
                    DockArea::Slot slot = DockArea::Slot::Count;
                    if (!DockArea::parseSlot(it.key(), slot) || !it.value().is_number()) {
                        continue;
                    }
                    if (minimums) dock->setSlotMinSize(slot, it.value().get<float>());
                    else dock->setSlotWeight(slot, it.value().get<float>());
                }
            };
            if (j.contains("slotWeights")) applySlotValues(j["slotWeights"], false);
            if (j.contains("slotMinSizes")) applySlotValues(j["slotMinSizes"], true);

            if (j.contains("cards") && j["cards"].is_array()) {
                for (const auto& cardJson : j["cards"]) {
                    Widget* child = deserialize(cardJson.dump());
                    DockCard* card = dynamic_cast<DockCard*>(child);
                    DockArea::Slot slot = DockArea::Slot::Count;
                    if (card == nullptr
                        || !DockArea::parseSlot(cardJson.value("slot", std::string()), slot)) {
                        destroyWidgetTree(child);
                        continue;
                    }
                    dock->addCard(slot, std::unique_ptr<DockCard>(card));
                }
            }
            if (j.contains("floating") && j["floating"].is_array()
                && dock->getOverlay() != nullptr) {
                for (const auto& floatingJson : j["floating"]) {
                    Widget* child = deserialize(floatingJson.dump());
                    DockCard* card = dynamic_cast<DockCard*>(child);
                    if (card == nullptr) {
                        destroyWidgetTree(child);
                        continue;
                    }
                    card->setPosition(math::FVector2(
                        floatingJson.value("x", card->getPosition().x),
                        floatingJson.value("y", card->getPosition().y)));
                    card->setSize(math::FVector2(
                        floatingJson.value("w", card->getSize().x),
                        floatingJson.value("h", card->getSize().y)));
                    dock->getOverlay()->addFloatingCard(card);
                }
            }
        }

        if (Dimmer* dimmer = dynamic_cast<Dimmer*>(widget)) {
            if (j.contains("scrimColor") && j["scrimColor"].is_object()) {
                const auto& c = j["scrimColor"];
                dimmer->setScrimColor(math::FVector4(
                    c.value("r", 0.0f), c.value("g", 0.0f),
                    c.value("b", 0.0f), c.value("a", 0.5f)));
            }
        }

        const auto restoreModalBase = [&j](Modal* modal) {
            if (j.contains("dismissOnDimmerClick")) {
                modal->setDismissOnDimmerClick(j["dismissOnDimmerClick"].get<bool>());
            }
            if (j.contains("dimmer") && j["dimmer"].is_object()) {
                const auto& dimmerJson = j["dimmer"];
                auto* dimmer = new Dimmer();
                if (dimmerJson.contains("scrimColor")
                    && dimmerJson["scrimColor"].is_object()) {
                    const auto& c = dimmerJson["scrimColor"];
                    dimmer->setScrimColor(math::FVector4(
                        c.value("r", 0.0f), c.value("g", 0.0f),
                        c.value("b", 0.0f), c.value("a", 0.5f)));
                }
                modal->setDimmerOwned(dimmer);
            }
        };
        if (ModalDialog* dialog = dynamic_cast<ModalDialog*>(widget)) {
            restoreModalBase(dialog);
            if (j.contains("acceptText")) {
                dialog->setAcceptText(toWstring(j["acceptText"].get<std::string>()));
            }
            if (j.contains("rejectText")) {
                dialog->setRejectText(toWstring(j["rejectText"].get<std::string>()));
            }
            if (j.contains("bodyContent") && j["bodyContent"].is_object()) {
                if (Widget* body = deserialize(j["bodyContent"].dump())) {
                    dialog->setBodyContentOwned(body);
                }
            }
        } else if (Modal* modal = dynamic_cast<Modal*>(widget)) {
            restoreModalBase(modal);
            if (j.contains("content") && j["content"].is_object()) {
                if (Widget* content = deserialize(j["content"].dump())) {
                    modal->setContentOwned(content);
                }
            }
        }

        if (TabStrip* strip = dynamic_cast<TabStrip*>(widget)) {
            if (j.contains("tabs") && j["tabs"].is_array()) {
                for (const auto& tabJson : j["tabs"]) {
                    if (tabJson.is_string()) {
                        strip->addTab(toWstring(tabJson.get<std::string>()));
                    }
                }
            }
            if (j.contains("selectedIndex")) strip->setSelectedIndex(j["selectedIndex"].get<int>());
            if (j.contains("tabHeight")) strip->setTabHeight(j["tabHeight"].get<float>());
            if (j.contains("spacing")) strip->setSpacing(j["spacing"].get<float>());
            if (j.contains("indicatorTweenMs")) {
                strip->setIndicatorTweenMs(j["indicatorTweenMs"].get<float>());
            }
            if (j.contains("overflowMode")) {
                const int value = j["overflowMode"].get<int>();
                if (value >= 0 && value <= static_cast<int>(TabStrip::OverflowMode::Clip)) {
                    strip->setOverflowMode(static_cast<TabStrip::OverflowMode>(value));
                }
            }
            if (j.contains("minTabWidth")) strip->setMinTabWidth(j["minTabWidth"].get<float>());
        }

        if (!hasStructuredChildPayload(widget)
            && j.contains("children") && j["children"].is_array()) {
            for (const auto& childJson : j["children"]) {
                Widget* child = deserialize(childJson.dump());
                if (child) {
                    widget->addChild(child);
                }
            }
        }

        return widget;
    }
    catch (const std::exception&) {
        return nullptr;
    }
}

void WidgetSerializer::serializeWidgetToJson(Widget* widget, json& j) {
    if (!widget) return;

    // R-8 (B11 fix): the previous version wrote `j["type"] = widget->getStyleId()`
    // here as a "placeholder" on the assumption that every subclass branch
    // below would override it. That breaks two ways:
    //   1. A raw Widget with a non-empty styleId would export its style id
    //      as the type field. The `else` branch at the bottom then writes
    //      `"Widget"` back, masking the bug for the base class — but any
    //      future subclass that forgets to register (R-10 Panel, R-7
    //      CheckBox, etc.) would silently export its styleId as `type`,
    //      and the round-trip in `deserialize` would either fail to find
    //      a registered factory or instantiate the wrong class.
    //   2. If serialization throws partway through the dynamic_cast chain
    //      (rare but possible if a future subclass throws in a getter),
    //      `type` is left as the styleId, which is silently wrong.
    // Fix: don't pre-write `type` at all. The if-else chain below is
    // exhaustive over known subclasses; the trailing `else` covers
    // everything else as the literal "Widget". This way type is set
    // exactly once and only by code that knows the right answer.
    j["id"] = widget->getId();

    // Position
    math::FVector2 pos = widget->getPosition();
    j["position"] = { {"x", pos.x}, {"y", pos.y} };

    // Size
    math::FVector2 size = widget->getSize();
    j["size"] = { {"w", size.x}, {"h", size.y} };

    j["visible"] = widget->isVisible();
    j["style"] = widget->getStyleId();
    j["opacity"] = widget->getOpacity();
    j["layoutPositionManaged"] = widget->isLayoutPositionManaged();
    j["layoutSizeManaged"] = widget->isLayoutSizeManaged();
    if (widget->hasExplicitAccessibilityRole()) {
        j["accessibilityRole"] = accessibilityRoleName(widget->getAccessibilityRole());
    }
    if (!widget->getAccessibilityLabel().empty()) {
        j["accessibilityLabel"] = toUtf8(widget->getAccessibilityLabel());
    }
    if (!widget->getAccessibilityDescription().empty()) {
        j["accessibilityDescription"] = toUtf8(widget->getAccessibilityDescription());
    }
    if (!widget->getAccessibilityValue().empty()) {
        j["accessibilityValue"] = toUtf8(widget->getAccessibilityValue());
    }
    if (widget->isAccessibilityHidden()) j["accessibilityHidden"] = true;

    // G11 — emit per-widget token overrides (when any are set) so the
    // round-trip via deserialize restores them. Empty map = omit the
    // key entirely (deserializer treats absent == empty).
    const auto& overrides = widget->getStyleTokenOverrides();
    if (!overrides.empty()) {
        json so = json::object();
        for (const auto& kv : overrides) {
            const auto& c = kv.second;
            so[kv.first] = { c.x, c.y, c.z, c.w };
        }
        j["styleOverrides"] = so;
    }

    // Type-specific
    if (TextLabel* label = dynamic_cast<TextLabel*>(widget)) {
        j["type"] = "TextLabel";
        j["text"] = toUtf8(label->getText());
        j["fontSize"] = label->getFontSize();
        switch (label->getHorizontalAlignment()) {
        case TextLabel::HAlignment::Center: j["hAlign"] = "Center"; break;
        case TextLabel::HAlignment::Right:  j["hAlign"] = "Right";  break;
        case TextLabel::HAlignment::Left:
        default:                            j["hAlign"] = "Left";   break;
        }
        switch (label->getVerticalAlignment()) {
        case TextLabel::VAlignment::Center: j["vAlign"] = "Center"; break;
        case TextLabel::VAlignment::Bottom: j["vAlign"] = "Bottom"; break;
        case TextLabel::VAlignment::Top:
        default:                            j["vAlign"] = "Top";    break;
        }
    }
    else if (Button* button = dynamic_cast<Button*>(widget)) {
        j["type"] = "Button";
        j["text"] = toUtf8(button->getText());
    }
    else if (CheckBox* cb = dynamic_cast<CheckBox*>(widget)) {
        j["type"] = "CheckBox";
        j["text"] = toUtf8(cb->getText());
        j["checked"] = cb->isChecked();
    }
    else if (RadioButton* rb = dynamic_cast<RadioButton*>(widget)) {
        j["type"] = "RadioButton";
        j["text"] = toUtf8(rb->getText());
        j["checked"] = rb->isChecked();
        j["groupId"] = rb->getGroupId();
    }
    else if (Slider* sl = dynamic_cast<Slider*>(widget)) {
        j["type"] = "Slider";
        j["min"]  = sl->getMin();
        j["max"]  = sl->getMax();
        j["value"] = sl->getValue();
    }
    else if (ProgressBar* pb = dynamic_cast<ProgressBar*>(widget)) {
        j["type"] = "ProgressBar";
        j["min"]  = pb->getMin();
        j["max"]  = pb->getMax();
        j["value"] = pb->getValue();
    }
    else if (dynamic_cast<Spinner*>(widget) != nullptr) {
        j["type"] = "Spinner";
    }
    else if (TextInput* ti = dynamic_cast<TextInput*>(widget)) {
        j["type"] = "TextInput";
        j["text"] = toUtf8(ti->getText());
        j["password"] = ti->isPasswordMode();
        j["readOnly"] = ti->isReadOnly();
        j["maxLength"] = static_cast<int>(ti->getMaxLength());
        // G6 — HAlign round-trip. Always emit so a Left default
        // round-trips cleanly.
        j["hAlign"] = static_cast<int>(ti->getHAlign());
    }
    else if (TextArea* ta = dynamic_cast<TextArea*>(widget)) {
        j["type"] = "TextArea";
        j["text"] = toUtf8(ta->getText());
        j["readOnly"] = ta->isReadOnly();
        j["maxLength"] = static_cast<int>(ta->getMaxLength());
        j["lineHeight"] = ta->getLineHeight();
    }
    else if (ScrollBar* sb = dynamic_cast<ScrollBar*>(widget)) {
        j["type"] = "ScrollBar";
        j["orientation"] = (sb->getOrientation() == ScrollBar::Orientation::Vertical)
            ? "vertical" : "horizontal";
    }
    else if (ScrollView* scroll = dynamic_cast<ScrollView*>(widget)) {
        j["type"] = "ScrollView";
        j["verticalScrollBar"] = scroll->isVerticalScrollBarEnabled();
        j["horizontalScrollBar"] = scroll->isHorizontalScrollBarEnabled();
        const auto contentSize = scroll->getContentSize();
        const auto offset = scroll->getScrollOffset();
        j["contentSize"] = {{"w", contentSize.x}, {"h", contentSize.y}};
        j["scrollOffset"] = {{"x", offset.x}, {"y", offset.y}};
        if (Widget* content = scroll->getContent()) {
            json contentJson;
            serializeWidgetToJson(content, contentJson);
            j["content"] = contentJson;
        }
    }
    else if (ListView* lv = dynamic_cast<ListView*>(widget)) {
        j["type"] = "ListView";
        j["items"] = json::array();
        for (const auto& s : lv->getItemsRef()) {
            j["items"].push_back(toUtf8(s));
        }
        j["selectedIndex"] = lv->getSelectedIndex();
        j["itemHeight"] = lv->getItemHeight();
        // G1 — multi-select round-trip. Always emit both fields so a
        // Single-mode list still round-trips cleanly through extended
        // payloads. `selectedIndex` keeps its v1 semantic; `selectedIndices`
        // carries the full vector (sorted ascending). Hosts reading old
        // payloads without these new fields fall back to single-index.
        j["selectionMode"] = static_cast<int>(lv->getSelectionMode());
        j["selectedIndices"] = json::array();
        for (int idx : lv->getSelectedIndices()) {
            j["selectedIndices"].push_back(idx);
        }
    }
    else if (ComboBox* cb = dynamic_cast<ComboBox*>(widget)) {
        j["type"] = "ComboBox";
        j["items"] = json::array();
        for (const auto& s : cb->getItemsRef()) {
            j["items"].push_back(toUtf8(s));
        }
        j["selectedIndex"] = cb->getSelectedIndex();
        j["maxPopupItems"] = cb->getMaxPopupItems();
    }
    else if (TabControl* tc = dynamic_cast<TabControl*>(widget)) {
        j["type"] = "TabControl";
        j["tabs"] = json::array();
        for (size_t i = 0; i < tc->getTabCount(); ++i) {
            json tabJson;
            tabJson["label"] = toUtf8(tc->getTabLabel(i));
            Widget* content = tc->getTabContent(i);
            if (content != nullptr) {
                json contentJson;
                serializeWidgetToJson(content, contentJson);
                tabJson["content"] = contentJson;
            }
            j["tabs"].push_back(tabJson);
        }
        j["selectedIndex"] = tc->getSelectedIndex();
        j["headerHeight"] = tc->getHeaderHeight();
    }
    else if (TabStrip* strip = dynamic_cast<TabStrip*>(widget)) {
        j["type"] = "TabStrip";
        j["tabs"] = json::array();
        for (int i = 0; i < strip->getTabCount(); ++i) {
            j["tabs"].push_back(toUtf8(strip->getTabLabel(i)));
        }
        j["selectedIndex"] = strip->getSelectedIndex();
        j["tabHeight"] = strip->getTabHeight();
        j["spacing"] = strip->getSpacing();
        j["indicatorTweenMs"] = strip->getIndicatorTweenMs();
        j["overflowMode"] = static_cast<int>(strip->getOverflowMode());
        j["minTabWidth"] = strip->getMinTabWidth();
    }
    else if (ModalDialog* dialog = dynamic_cast<ModalDialog*>(widget)) {
        j["type"] = "ModalDialog";
        j["dismissOnDimmerClick"] = dialog->isDismissOnDimmerClick();
        j["acceptText"] = toUtf8(dialog->getAcceptText());
        j["rejectText"] = toUtf8(dialog->getRejectText());
        if (Dimmer* dimmer = dialog->getDimmer()) {
            const auto& c = dimmer->getScrimColor();
            j["dimmer"] = {
                {"scrimColor", {
                    {"r", c.x}, {"g", c.y}, {"b", c.z}, {"a", c.w}
                }}
            };
        }
        if (Widget* body = dialog->getBodyContent()) {
            json bodyJson;
            serializeWidgetToJson(body, bodyJson);
            j["bodyContent"] = bodyJson;
        }
    }
    else if (Modal* modal = dynamic_cast<Modal*>(widget)) {
        j["type"] = "Modal";
        j["dismissOnDimmerClick"] = modal->isDismissOnDimmerClick();
        if (Dimmer* dimmer = modal->getDimmer()) {
            const auto& c = dimmer->getScrimColor();
            j["dimmer"] = {
                {"scrimColor", {
                    {"r", c.x}, {"g", c.y}, {"b", c.z}, {"a", c.w}
                }}
            };
        }
        if (Widget* content = modal->getContent()) {
            json contentJson;
            serializeWidgetToJson(content, contentJson);
            j["content"] = contentJson;
        }
    }
    else if (Dimmer* dimmer = dynamic_cast<Dimmer*>(widget)) {
        j["type"] = "Dimmer";
        const auto& c = dimmer->getScrimColor();
        j["scrimColor"] = {
            {"r", c.x}, {"g", c.y}, {"b", c.z}, {"a", c.w}
        };
    }
    else if (Window* window = dynamic_cast<Window*>(widget)) {
        j["type"] = "Window";
        j["title"] = toUtf8(window->getTitle());
        j["titleBarHeight"] = window->getTitleBarHeight();
        j["movable"] = window->isMovable();
        j["resizable"] = window->isResizable();
        j["minSize"] = { {"w", window->getMinSize().x}, {"h", window->getMinSize().y} };
    }
    else if (VBox* vbox = dynamic_cast<VBox*>(widget)) {
        j["type"] = "VBox";
        j["spacing"] = vbox->getSpacing();
        switch (vbox->getGravity()) {
        case BoxBase::Gravity::TopCenter: j["gravity"] = "TopCenter"; break;
        case BoxBase::Gravity::TopRight: j["gravity"] = "TopRight"; break;
        case BoxBase::Gravity::CenterLeft: j["gravity"] = "CenterLeft"; break;
        case BoxBase::Gravity::Center: j["gravity"] = "Center"; break;
        case BoxBase::Gravity::CenterRight: j["gravity"] = "CenterRight"; break;
        case BoxBase::Gravity::BottomLeft: j["gravity"] = "BottomLeft"; break;
        case BoxBase::Gravity::BottomCenter: j["gravity"] = "BottomCenter"; break;
        case BoxBase::Gravity::BottomRight: j["gravity"] = "BottomRight"; break;
        case BoxBase::Gravity::TopLeft:
        default: j["gravity"] = "TopLeft"; break;
        }
    }
    else if (DockCard* card = dynamic_cast<DockCard*>(widget)) {
        // D2 — DockCard is a leaf-with-content. It subclasses Panel, so
        // this branch MUST precede the Panel branch below to keep the
        // emitted type field "DockCard" rather than the base "Panel".
        // We serialize the meta (id / title / flags / headerHeight)
        // and inline `content` once, then suppress the auto children[]
        // walk below because content already serializes via the same
        // recursive path.
        j["type"] = "DockCard";
        j["id"]   = card->getId();
        if (!card->getTitle().empty()) {
            j["title"] = toUtf8(card->getTitle());
        }
        if (!card->getIcon().empty()) {
            j["icon"] = card->getIcon();
        }
        j["closable"]     = card->isClosable();
        j["floatable"]    = card->isFloatable();
        j["collapsed"]    = card->isCollapsed();
        j["headerHeight"] = card->getHeaderHeight();
        // D2 — inline the content subtree as a `content` sub-object so
        // the loader can reconstruct it via DockCard::setContent. We
        // also suppress children[] later in this function so a content
        // subtree doesn't double-emit.
        if (card->getContent() != nullptr) {
            json contentJson;
            serializeWidgetToJson(card->getContent(), contentJson);
            j["content"] = contentJson;
        }
    }
    else if (Panel* panel = dynamic_cast<Panel*>(widget)) {
        j["type"] = "Panel";
        j["borderEnabled"] = panel->isBorderEnabled();
        j["backgroundEnabled"] = panel->isBackgroundEnabled();
        const auto& p = panel->getPadding();
        j["padding"] = {
            {"left", p.x}, {"top", p.y}, {"right", p.z}, {"bottom", p.w}
        };
    }
    else if (GridPanel* gp = dynamic_cast<GridPanel*>(widget)) {
        j["type"] = "GridPanel";
        j["rowCount"] = gp->getRowCount();
        j["columnCount"] = gp->getColumnCount();
        j["rowDefs"] = json::array();
        for (int row = 0; row < gp->getRowCount(); ++row) {
            const auto& def = gp->getRowDef(row);
            j["rowDefs"].push_back({
                {"policy", def.policy == GridPanel::SizePolicy::Fixed
                    ? "Fixed" : "Stretch"},
                {"value", def.value}
            });
        }
        j["columnDefs"] = json::array();
        for (int col = 0; col < gp->getColumnCount(); ++col) {
            const auto& def = gp->getColumnDef(col);
            j["columnDefs"].push_back({
                {"policy", def.policy == GridPanel::SizePolicy::Fixed
                    ? "Fixed" : "Stretch"},
                {"value", def.value}
            });
        }
        const auto& padding = gp->getPadding();
        j["padding"] = {
            {"left", padding.x}, {"top", padding.y},
            {"right", padding.z}, {"bottom", padding.w}
        };
        j["spacing"] = {
            {"horizontal", gp->getHorizontalSpacing()},
            {"vertical", gp->getVerticalSpacing()}
        };

        const auto hAlignName = [](GridPanel::HAlign align) {
            switch (align) {
            case GridPanel::HAlign::Left: return "Left";
            case GridPanel::HAlign::Center: return "Center";
            case GridPanel::HAlign::Right: return "Right";
            case GridPanel::HAlign::Fill:
            default: return "Fill";
            }
        };
        const auto vAlignName = [](GridPanel::VAlign align) {
            switch (align) {
            case GridPanel::VAlign::Top: return "Top";
            case GridPanel::VAlign::Middle: return "Middle";
            case GridPanel::VAlign::Bottom: return "Bottom";
            case GridPanel::VAlign::Fill:
            default: return "Fill";
            }
        };
        j["cells"] = json::array();
        for (int row = 0; row < gp->getRowCount(); ++row) {
            for (int col = 0; col < gp->getColumnCount(); ++col) {
                const GridPanel::CellInfo* cell = gp->findCell(row, col);
                if (cell == nullptr || cell->widget == nullptr) continue;
                json contentJson;
                serializeWidgetToJson(cell->widget, contentJson);
                j["cells"].push_back({
                    {"row", row}, {"col", col},
                    {"rowSpan", cell->rowSpan}, {"colSpan", cell->colSpan},
                    {"hAlign", hAlignName(cell->hAlign)},
                    {"vAlign", vAlignName(cell->vAlign)},
                    {"content", contentJson}
                });
            }
        }
    }
    else if (Tooltip* tip = dynamic_cast<Tooltip*>(widget)) {
        j["type"] = "Tooltip";
        j["text"] = toUtf8(tip->getText());
        j["hoverDelay"] = tip->getHoverDelay();
    }
    else if (Separator* sep = dynamic_cast<Separator*>(widget)) {
        // G7 — ToolBarSeparator is a Separator subclass. Check it FIRST
        // so it serializes as "ToolBarSeparator" (its factory type), not
        // as "Separator". Same fields apply — palette is constructor-set.
        if (ToolBarSeparator* tbs = dynamic_cast<ToolBarSeparator*>(widget)) {
            j["type"] = "ToolBarSeparator";
            j["orientation"] =
                (tbs->getOrientation() == Separator::Orientation::Vertical)
                    ? "vertical" : "horizontal";
            const auto& c = tbs->getColor();
            j["color"] = { {"r", c.x}, {"g", c.y}, {"b", c.z}, {"a", c.w} };
            j["thickness"] = tbs->getThickness();
            j["inset"] = tbs->getInset();
        }
        else {
            j["type"] = "Separator";
            j["orientation"] =
                (sep->getOrientation() == Separator::Orientation::Vertical)
                    ? "vertical" : "horizontal";
            const auto& c = sep->getColor();
            j["color"] = { {"r", c.x}, {"g", c.y}, {"b", c.z}, {"a", c.w} };
            j["thickness"] = sep->getThickness();
            j["inset"] = sep->getInset();
        }
    }
    else if (MenuItem* mi = dynamic_cast<MenuItem*>(widget)) {
        j["type"] = "MenuItem";
        j["text"] = toUtf8(mi->getText());
        j["shortcut"] = toUtf8(mi->getShortcut());
        j["hasSubmenu"] = mi->hasSubmenu();
        if (Menu* submenu = mi->getSubmenu()) {
            json submenuJson;
            serializeWidgetToJson(submenu, submenuJson);
            j["submenu"] = submenuJson;
        }
    }
    else if (Menu* menu = dynamic_cast<Menu*>(widget)) {
        j["type"] = "Menu";
        j["items"] = json::array();
        for (size_t i = 0; i < menu->getItemCount(); ++i) {
            MenuItem* item = menu->getItem(i);
            if (item == nullptr) continue;
            json itemJson;
            serializeWidgetToJson(item, itemJson);
            j["items"].push_back(itemJson);
        }
    }
    else if (MenuBar* mb = dynamic_cast<MenuBar*>(widget)) {
        j["type"] = "MenuBar";
        j["anchorSpacing"] = mb->getAnchorSpacing();
        j["anchorWidth"] = mb->getAnchorWidth();
        j["anchorAutoWidth"] = mb->isAnchorAutoWidth();
        j["menus"] = json::array();
        for (size_t i = 0; i < mb->getMenuCount(); ++i) {
            json mj;
            mj["title"] = toUtf8(mb->getMenuTitle(i));
            if (Menu* menu = mb->getMenu(i)) {
                json menuJson;
                serializeWidgetToJson(menu, menuJson);
                mj["menu"] = menuJson;
            }
            j["menus"].push_back(mj);
        }
    }
    else if (ToolBar* tb = dynamic_cast<ToolBar*>(widget)) {
        j["type"] = "ToolBar";
        j["itemCount"] = static_cast<int>(tb->getItemCount());
    }
    else if (StatusBar* sb = dynamic_cast<StatusBar*>(widget)) {
        j["type"] = "StatusBar";
        j["panels"] = json::array();
        for (size_t i = 0; i < sb->getPanelCount(); ++i) {
            Widget* p = sb->getPanelWidget(i);
            if (p != nullptr) {
                json pj;
                serializeWidgetToJson(p, pj);
                j["panels"].push_back(pj);
            }
        }
    }
    else if (HBox* hbox = dynamic_cast<HBox*>(widget)) {
        j["type"] = "HBox";
        j["spacing"] = hbox->getSpacing();
        switch (hbox->getGravity()) {
        case BoxBase::Gravity::TopCenter: j["gravity"] = "TopCenter"; break;
        case BoxBase::Gravity::TopRight: j["gravity"] = "TopRight"; break;
        case BoxBase::Gravity::CenterLeft: j["gravity"] = "CenterLeft"; break;
        case BoxBase::Gravity::Center: j["gravity"] = "Center"; break;
        case BoxBase::Gravity::CenterRight: j["gravity"] = "CenterRight"; break;
        case BoxBase::Gravity::BottomLeft: j["gravity"] = "BottomLeft"; break;
        case BoxBase::Gravity::BottomCenter: j["gravity"] = "BottomCenter"; break;
        case BoxBase::Gravity::BottomRight: j["gravity"] = "BottomRight"; break;
        case BoxBase::Gravity::TopLeft:
        default: j["gravity"] = "TopLeft"; break;
        }
    }
    else if (SplitterHandle* splitter = dynamic_cast<SplitterHandle*>(widget)) {
        j["type"] = "SplitterHandle";
        j["orientation"] =
            splitter->getOrientation() == SplitterHandle::Orientation::Vertical
                ? "vertical" : "horizontal";
    }
    else if (Image* image = dynamic_cast<Image*>(widget)) {
        j["type"] = "Image";
        // G10 — texture handle is now typed + name-based. The backend
        // pointer itself never serializes (it's runtime state); the
        // host re-resolves `textureName` via TextureRegistry at load
        // time. width/height travel with the JSON so editor inspectors
        // can show "missing 64×48 icon" before the texture loads.
        const ImageTextureHandle& tex = image->getTexture();
        if (!tex.name.empty()) {
            j["textureName"]   = tex.name;
            j["textureWidth"]  = tex.width;
            j["textureHeight"] = tex.height;
        }
    }
    else if (TreeNode* tn = dynamic_cast<TreeNode*>(widget)) {
        j["type"] = "TreeNode";
        j["label"] = toUtf8(tn->getLabel());
        j["icon"]  = toUtf8(tn->getIcon());
        j["hasChildren"] = tn->hasChildren();
        j["expanded"] = tn->isExpanded();
        j["depth"] = tn->getDepth();
    }
    else if (TreeView* tv = dynamic_cast<TreeView*>(widget)) {
        j["type"] = "TreeView";
        j["tree"] = json::array();
        // Serialize from the flat view (post-collapse); callers wanting the
        // full source tree should setTree() with the original model.
        for (size_t i = 0; i < tv->getNodeCount(); ++i) {
            const auto& d = tv->getNodeData(i);
            json nj;
            nj["label"] = toUtf8(d.label);
            nj["icon"]  = toUtf8(d.icon);
            nj["hasChildren"] = d.hasChildren;
            nj["expanded"] = d.expanded;
            nj["parentIndex"] = d.parentIndex;
            j["tree"].push_back(nj);
        }
        j["selectedIndex"] = tv->getSelectedIndex();
        j["itemHeight"] = tv->getItemHeight();
    }
    else if (RichText* rt = dynamic_cast<RichText*>(widget)) {
        j["type"] = "RichText";
        const auto& dc = rt->getDefaultColor();
        j["defaultColor"] = {
            {"r", dc.x}, {"g", dc.y}, {"b", dc.z}, {"a", dc.w} };
        j["defaultFontSize"] = rt->getDefaultFontSize();
        j["wrapWidth"] = rt->getWrapWidth();
        j["wrapMode"] = static_cast<int>(rt->getWrapMode());
        j["alignment"] = static_cast<int>(rt->getAlignment());
        j["verticalAlignment"] = static_cast<int>(rt->getVerticalAlignment());
        j["overflow"] = static_cast<int>(rt->getOverflow());
        j["lineHeight"] = rt->getLineHeight();
        j["lineSpacing"] = rt->getLineSpacing();
        j["maxLines"] = rt->getMaxLines();
        j["runs"] = json::array();
        for (size_t i = 0; i < rt->getRunCount(); ++i) {
            const RichRun& r = rt->getRun(i);
            json rj;
            rj["text"] = toUtf8(r.text);
            rj["color"] = {
                {"r", r.color.x}, {"g", r.color.y},
                {"b", r.color.z}, {"a", r.color.w} };
            rj["fontSize"] = r.fontSize;
            rj["bold"] = r.bold;
            rj["italic"] = r.italic;
            rj["underline"] = r.underline;
            rj["strikethrough"] = r.strikethrough;
            rj["letterSpacing"] = r.letterSpacing;
            rj["baselineShift"] = r.baselineShift;
            j["runs"].push_back(rj);
        }
    }
    else if (DockOverlay* overlay = dynamic_cast<DockOverlay*>(widget)) {
        j["type"] = "DockOverlay";
        j["floating"] = json::array();
        for (size_t i = 0; i < overlay->getFloatingCardCount(); ++i) {
            DockCard* card = overlay->getFloatingCard(i);
            if (card == nullptr) continue;
            json cardJson;
            serializeWidgetToJson(card, cardJson);
            const auto pos = card->getPosition();
            const auto size = card->getSize();
            cardJson["x"] = pos.x;
            cardJson["y"] = pos.y;
            cardJson["w"] = size.x;
            cardJson["h"] = size.y;
            j["floating"].push_back(cardJson);
        }
    }
    else if (DockArea* dock = dynamic_cast<DockArea*>(widget)) {
        // D2 — DockArea emit. We list cards[] in 5 groups keyed by
        // their slot (mirrors how `cards[]` is parsed on load), plus
        // a `floating[]` for cards hosted on the overlay. The card
        // body itself is NOT inlined here — it carries its own
        // children tree (header strip + content) and gets serialized
        // by the recursive serializeWidgetToJson when its turn comes
        // via getOverlay()/getCard(). We stop recursion at the
        // DockArea level so editors can read the top-level file as a
        // pure shell description.
        j["type"] = "DockArea";
        // Per-slot slotWeights — emit any slot whose weight was
        // overridden off the header default (0.0) so a re-saved
        // shell only carries deltas.
        json wj = json::object();
        json mj = json::object();
        static const char* slotNames[(int)DockArea::Slot::Count] = {
            "Left", "Right", "Top", "Bottom", "Center"
        };
        for (int i = 0; i < (int)DockArea::Slot::Count; ++i) {
            const float w = dock->getSlotWeight((DockArea::Slot)i);
            if (w > 0.0f) {
                wj[slotNames[i]] = w;
            }
            const float mn = dock->getSlotMinSize((DockArea::Slot)i);
            if (mn > 0.0f) {
                mj[slotNames[i]] = mn;
            }
        }
        if (!wj.empty()) j["slotWeights"] = wj;
        if (!mj.empty()) j["slotMinSizes"] = mj;

        j["cards"] = json::array();
        for (int i = 0; i < (int)DockArea::Slot::Count; ++i) {
            const size_t n = dock->getCardCount((DockArea::Slot)i);
            for (size_t k = 0; k < n; ++k) {
                DockCard* c = dock->getCard((DockArea::Slot)i, k);
                if (c == nullptr) continue;
                json cj;
                serializeWidgetToJson(c, cj);
                cj["slot"] = slotNames[i];
                j["cards"].push_back(cj);
            }
        }

        DockOverlay* overlay = dock->getOverlay();
        if (overlay != nullptr) {
            const size_t fn = overlay->getFloatingCardCount();
            if (fn > 0) {
                j["floating"] = json::array();
                for (size_t k = 0; k < fn; ++k) {
                    DockCard* c = overlay->getFloatingCard(k);
                    if (c == nullptr) continue;
                    json cj;
                    serializeWidgetToJson(c, cj);
                    const math::FVector2 pos = c->getPosition();
                    const math::FVector2 sz  = c->getSize();
                    cj["x"] = pos.x;
                    cj["y"] = pos.y;
                    cj["w"] = sz.x;
                    cj["h"] = sz.y;
                    j["floating"].push_back(cj);
                }
            }
        }
    }
    else {
        j["type"] = "Widget";
    }

    // Children
    // D2 — dock widgets opt out of the auto children[] walk because
    // they hand-serialize their child cards themselves. DockCard's
    // single child (the content widget set via setContent) was
    // already inlined into the `cj["content"]` sub-object above; if
    // we re-emitted it as a generic child the load path would
    // double-add (once via content, once via the children[] block in
    // deserialize). DockArea's children include the 5 slot
    // containers, DockOverlay, AND every card; the editor shell
    // format intentionally hides those intermediate nodes and only
    // exposes the user-visible card list under `cards[]`.
    if (hasStructuredChildPayload(widget)) {
        return;
    }
    const auto& children = widget->getChildren();
    if (!children.empty()) {
        j["children"] = json::array();
        for (Widget* child : children) {
            json childJson;
            serializeWidgetToJson(child, childJson);
            j["children"].push_back(childJson);
        }
    }
}
