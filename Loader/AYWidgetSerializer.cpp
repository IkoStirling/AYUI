#include "AYWidgetSerializer.h"
#include "AYWidgetFactory.h"
#include "AYTextLabel.h"
#include "AYButton.h"
#include "AYCheckBox.h"
#include "AYRadioButton.h"
#include "AYSlider.h"
#include "AYProgressBar.h"
#include "AYTextInput.h"
#include "AYTextArea.h"
#include "AYTooltip.h"
#include "AYSeparator.h"
#include "AYMenuItem.h"
#include "AYMenu.h"
#include "AYMenuBar.h"
#include "AYToolBar.h"
#include "AYStatusBar.h"
#include "AYScrollBar.h"
#include "AYScrollView.h"
#include "AYListView.h"
#include "AYComboBox.h"
#include "AYWindow.h"
#include "AYPanel.h"
#include "AYBox.h"
#include "AYSplitterHandle.h"
#include "AYImage.h"
#include "AYTabControl.h"
#include "AYGridPanel.h"
#include <nlohmann/json.hpp>
#include <codecvt>
#include <locale>

using namespace ayt::ui;
using json = nlohmann::json;

static std::wstring toWstring(const std::string& str) {
    return std::wstring(str.begin(), str.end());
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

        if (TextLabel* label = dynamic_cast<TextLabel*>(widget)) {
            if (j.contains("text")) {
                label->setText(toWstring(j["text"].get<std::string>()));
            }
            if (j.contains("fontSize")) {
                label->setFontSize(j["fontSize"]);
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
                    tc->addTab(toWstring(label), content);
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
        }

        if (GridPanel* gp = dynamic_cast<GridPanel*>(widget)) {
            int rows = j.value("rowCount", 0);
            int cols = j.value("columnCount", 0);
            if (rows > 0) gp->setRowCount(rows);
            if (cols > 0) gp->setColumnCount(cols);
            // Children are re-attached via the standard children[] walk
            // below. With v1's lossy serializer (DECISION 5), each child
            // is placed in the next (row, col) cell in linear order; this
            // matches what hosts using the JSON path typically want
            // (grid filled left-to-right, top-to-bottom).
            if (rows > 0 && cols > 0 && j.contains("children") &&
                j["children"].is_array()) {
                int r = 0, c = 0;
                for (const auto& childJson : j["children"]) {
                    Widget* child = deserialize(childJson.dump());
                    if (child != nullptr) {
                        gp->setCell(r, c, child);
                        ++c;
                        if (c >= cols) { c = 0; ++r; }
                        if (r >= rows) break;
                    }
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
        }

        if (MenuItem* mi = dynamic_cast<MenuItem*>(widget)) {
            if (j.contains("text")) {
                mi->setText(toWstring(j["text"].get<std::string>()));
            }
            if (j.contains("shortcut")) {
                mi->setShortcut(toWstring(j["shortcut"].get<std::string>()));
            }
        }

        if (StatusBar* sb = dynamic_cast<StatusBar*>(widget)) {
            if (j.contains("panels") && j["panels"].is_array()) {
                for (const auto& pj : j["panels"]) {
                    if (pj.contains("text")) {
                        sb->addPanel(toWstring(
                            pj["text"].get<std::string>()));
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
        }

        if (j.contains("children") && j["children"].is_array()) {
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

    // Type-specific
    if (TextLabel* label = dynamic_cast<TextLabel*>(widget)) {
        j["type"] = "TextLabel";
        j["text"] = std::string(label->getText().begin(), label->getText().end());
        j["fontSize"] = label->getFontSize();
    }
    else if (Button* button = dynamic_cast<Button*>(widget)) {
        j["type"] = "Button";
        j["text"] = std::string(button->getText().begin(), button->getText().end());
    }
    else if (CheckBox* cb = dynamic_cast<CheckBox*>(widget)) {
        j["type"] = "CheckBox";
        j["text"] = std::string(cb->getText().begin(), cb->getText().end());
        j["checked"] = cb->isChecked();
    }
    else if (RadioButton* rb = dynamic_cast<RadioButton*>(widget)) {
        j["type"] = "RadioButton";
        j["text"] = std::string(rb->getText().begin(), rb->getText().end());
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
    else if (TextInput* ti = dynamic_cast<TextInput*>(widget)) {
        j["type"] = "TextInput";
        j["text"] = std::string(ti->getText().begin(), ti->getText().end());
        j["password"] = ti->isPasswordMode();
        j["readOnly"] = ti->isReadOnly();
        j["maxLength"] = static_cast<int>(ti->getMaxLength());
    }
    else if (TextArea* ta = dynamic_cast<TextArea*>(widget)) {
        j["type"] = "TextArea";
        j["text"] = std::string(ta->getText().begin(), ta->getText().end());
        j["readOnly"] = ta->isReadOnly();
        j["maxLength"] = static_cast<int>(ta->getMaxLength());
        j["lineHeight"] = ta->getLineHeight();
    }
    else if (ScrollBar* sb = dynamic_cast<ScrollBar*>(widget)) {
        j["type"] = "ScrollBar";
        j["orientation"] = (sb->getOrientation() == ScrollBar::Orientation::Vertical)
            ? "vertical" : "horizontal";
    }
    else if (dynamic_cast<ScrollView*>(widget) != nullptr) {
        j["type"] = "ScrollView";
    }
    else if (ListView* lv = dynamic_cast<ListView*>(widget)) {
        j["type"] = "ListView";
        j["items"] = json::array();
        for (const auto& s : lv->getItemsRef()) {
            j["items"].push_back(std::string(s.begin(), s.end()));
        }
        j["selectedIndex"] = lv->getSelectedIndex();
        j["itemHeight"] = lv->getItemHeight();
    }
    else if (ComboBox* cb = dynamic_cast<ComboBox*>(widget)) {
        j["type"] = "ComboBox";
        j["items"] = json::array();
        for (const auto& s : cb->getItemsRef()) {
            j["items"].push_back(std::string(s.begin(), s.end()));
        }
        j["selectedIndex"] = cb->getSelectedIndex();
        j["maxPopupItems"] = cb->getMaxPopupItems();
    }
    else if (TabControl* tc = dynamic_cast<TabControl*>(widget)) {
        j["type"] = "TabControl";
        j["tabs"] = json::array();
        for (size_t i = 0; i < tc->getTabCount(); ++i) {
            json tabJson;
            tabJson["label"] = std::string(tc->getTabLabel(i).begin(),
                                           tc->getTabLabel(i).end());
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
    else if (Window* window = dynamic_cast<Window*>(widget)) {
        j["type"] = "Window";
        j["title"] = std::string(window->getTitle().begin(), window->getTitle().end());
        j["titleBarHeight"] = window->getTitleBarHeight();
        j["movable"] = window->isMovable();
        j["resizable"] = window->isResizable();
        j["minSize"] = { {"w", window->getMinSize().x}, {"h", window->getMinSize().y} };
    }
    else if (VBox* vbox = dynamic_cast<VBox*>(widget)) {
        j["type"] = "VBox";
        j["spacing"] = vbox->getSpacing();
    }
    else if (Panel* panel = dynamic_cast<Panel*>(widget)) {
        j["type"] = "Panel";
        j["borderEnabled"] = panel->isBorderEnabled();
    }
    else if (GridPanel* gp = dynamic_cast<GridPanel*>(widget)) {
        j["type"] = "GridPanel";
        j["rowCount"] = gp->getRowCount();
        j["columnCount"] = gp->getColumnCount();
        // v1 simplification: cell positions (row, col) and spans are NOT
        // serialized. Children are written via the standard children[] walk,
        // so a round-trip preserves the widget tree but loses its grid
        // attachment. Hosts needing round-trip fidelity should attach cells
        // programmatically after deserialize, or wait for v1.1 (see
        // Controls/AYGridPanel.h DECISION 5 + Test_GridPanel G3).
    }
    else if (Tooltip* tip = dynamic_cast<Tooltip*>(widget)) {
        j["type"] = "Tooltip";
        j["text"] = std::string(tip->getText().begin(), tip->getText().end());
        j["hoverDelay"] = tip->getHoverDelay();
    }
    else if (Separator* sep = dynamic_cast<Separator*>(widget)) {
        j["type"] = "Separator";
        j["orientation"] =
            (sep->getOrientation() == Separator::Orientation::Vertical)
                ? "vertical" : "horizontal";
        const auto& c = sep->getColor();
        j["color"] = { {"r", c.x}, {"g", c.y}, {"b", c.z}, {"a", c.w} };
        j["thickness"] = sep->getThickness();
        j["inset"] = sep->getInset();
    }
    else if (MenuItem* mi = dynamic_cast<MenuItem*>(widget)) {
        j["type"] = "MenuItem";
        j["text"] = std::string(mi->getText().begin(), mi->getText().end());
        j["shortcut"] = std::string(mi->getShortcut().begin(), mi->getShortcut().end());
        j["hasSubmenu"] = mi->hasSubmenu();
    }
    else if (Menu* menu = dynamic_cast<Menu*>(widget)) {
        j["type"] = "Menu";
        j["open"] = menu->isOpen();
        // Children (MenuItems) are walked via the standard children[] block
        // below, so we don't enumerate items here.
    }
    else if (MenuBar* mb = dynamic_cast<MenuBar*>(widget)) {
        j["type"] = "MenuBar";
        j["menus"] = json::array();
        for (size_t i = 0; i < mb->getMenuCount(); ++i) {
            json mj;
            mj["title"] = std::string(mb->getMenuTitle(i).begin(),
                                       mb->getMenuTitle(i).end());
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
            TextLabel* p = sb->getPanel(i);
            if (p != nullptr) {
                json pj;
                pj["text"] = std::string(p->getText().begin(), p->getText().end());
                j["panels"].push_back(pj);
            }
        }
    }
    else if (HBox* hbox = dynamic_cast<HBox*>(widget)) {
        j["type"] = "HBox";
        j["spacing"] = hbox->getSpacing();
    }
    else if (dynamic_cast<SplitterHandle*>(widget) != nullptr) {
        j["type"] = "SplitterHandle";
    }
    else if (Image* image = dynamic_cast<Image*>(widget)) {
        j["type"] = "Image";
        // texture handle would need separate serialization
    }
    else {
        j["type"] = "Widget";
    }

    // Children
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