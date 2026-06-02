#include "AYWidgetSerializer.h"
#include "AYWidgetFactory.h"
#include "AYTextLabel.h"
#include "AYButton.h"
#include "AYWindow.h"
#include "AYBox.h"
#include "AYImage.h"
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

        if (Window* window = dynamic_cast<Window*>(widget)) {
            if (j.contains("title")) {
                window->setTitle(toWstring(j["title"].get<std::string>()));
            }
            if (j.contains("titleBarHeight")) {
                window->setTitleBarHeight(j["titleBarHeight"]);
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

    j["type"] = widget->getStyleId();  // Will be overridden by specific types
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
    else if (Window* window = dynamic_cast<Window*>(widget)) {
        j["type"] = "Window";
        j["title"] = std::string(window->getTitle().begin(), window->getTitle().end());
        j["titleBarHeight"] = window->getTitleBarHeight();
    }
    else if (VBox* vbox = dynamic_cast<VBox*>(widget)) {
        j["type"] = "VBox";
        j["spacing"] = vbox->getSpacing();
    }
    else if (HBox* hbox = dynamic_cast<HBox*>(widget)) {
        j["type"] = "HBox";
        j["spacing"] = hbox->getSpacing();
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