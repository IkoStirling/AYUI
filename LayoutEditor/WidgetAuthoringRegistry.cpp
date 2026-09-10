#include "AYUI/LayoutEditor/WidgetAuthoringRegistry.h"

#include "AYUI/Box.h"
#include "AYUI/Button.h"
#include "AYUI/CheckBox.h"
#include "AYUI/ComboBox.h"
#include "AYUI/GridPanel.h"
#include "AYUI/Image.h"
#include "AYUI/ListView.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/Panel.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/RadioButton.h"
#include "AYUI/RichText.h"
#include "AYUI/ScrollView.h"
#include "AYUI/Separator.h"
#include "AYUI/Slider.h"
#include "AYUI/Spinner.h"
#include "AYUI/TabControl.h"
#include "AYUI/TabStrip.h"
#include "AYUI/TextArea.h"
#include "AYUI/TextInput.h"
#include "AYUI/TextLabel.h"
#include "AYUI/TileView.h"
#include "AYUI/Tooltip.h"
#include "AYUI/TreeView.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/Window.h"

#include <typeinfo>
#include <utility>

namespace ayt::ui {
namespace {

using P = AuthoringProperty;
using C = WidgetAuthoringCategory;

PropertySchema baseSchema() {
    return PropertySchema{P::Id, P::X, P::Y, P::Width, P::Height, P::Style};
}

PropertySchema interaction(std::initializer_list<P> events) {
    PropertySchema result{P::Controller};
    for (P event : events) result.add(event);
    return result;
}

template <typename T>
WidgetAuthoringDescriptor descriptor(
    const char* type, const char* display, const char* button, C category,
    const char* prefix, math::FVector2 size, const char* icon,
    PropertySchema schema,
    WidgetAuthoringDescriptor::Initializer initializer = {}) {
    WidgetAuthoringDescriptor result;
    result.typeName = type;
    result.displayName = display;
    result.paletteButtonId = button;
    result.category = category;
    result.idPrefix = prefix;
    result.defaultSize = size;
    result.iconPath = icon;
    result.properties = std::move(schema);
    result.matches = [](const Widget& widget) {
        return typeid(widget) == typeid(T);
    };
    result.initialize = std::move(initializer);
    return result;
}

void setInitialText(Widget& widget, const std::wstring& text) {
    if (auto* value = dynamic_cast<Button*>(&widget)) value->setText(text);
    else if (auto* value = dynamic_cast<TextLabel*>(&widget)) value->setText(text);
    else if (auto* value = dynamic_cast<TextInput*>(&widget)) value->setText(text);
    else if (auto* value = dynamic_cast<CheckBox*>(&widget)) value->setText(text);
    else if (auto* value = dynamic_cast<RadioButton*>(&widget)) value->setText(text);
    else if (auto* value = dynamic_cast<TextArea*>(&widget)) value->setText(text);
    else if (auto* value = dynamic_cast<Tooltip*>(&widget)) value->setText(text);
    else if (auto* value = dynamic_cast<Window*>(&widget)) value->setTitle(text);
    else if (auto* value = dynamic_cast<RichText*>(&widget)) {
        value->clearRuns();
        value->addRun(text, value->getDefaultColor(), value->getDefaultFontSize());
    }
}

WidgetAuthoringDescriptor::Initializer textInitializer(const wchar_t* text) {
    const std::wstring initial = text;
    return [initial](Widget& widget,
                     const WidgetAuthoringDescriptor::UniqueIdFactory&) {
        setInitialText(widget, initial);
    };
}

} // namespace

WidgetAuthoringRegistry& WidgetAuthoringRegistry::get() {
    static WidgetAuthoringRegistry registry;
    return registry;
}

WidgetAuthoringRegistry::WidgetAuthoringRegistry() {
    auto base = [] { return baseSchema(); };
    auto with = [](PropertySchema schema,
                   std::initializer_list<P> properties) {
        for (P property : properties) schema.add(property);
        return schema;
    };

    registerDescriptor(descriptor<Button>(
        "Button", "Button", "btn_add_button", C::BasicContent, "btn",
        {120.0f, 28.0f}, "M4 7h16v10H4z M8 12h8",
        with(base().add(interaction({P::OnClick})), {P::Text}),
        textInitializer(L"Button")));
    registerDescriptor(descriptor<TextLabel>(
        "TextLabel", "Text Label", "btn_add_label", C::BasicContent, "lbl",
        {120.0f, 28.0f}, "M5 6h14 M12 6v12 M8 18h8",
        with(base(), {P::Text, P::TextHAlign, P::TextVAlign}),
        textInitializer(L"Label")));
    registerDescriptor(descriptor<RichText>(
        "RichText", "Rich Text", "btn_add_richtext", C::BasicContent,
        "richtext", {200.0f, 80.0f},
        "M4 5h16 M4 9h11 M4 13h16 M4 17h9",
        with(base(), {P::Text, P::RichTextWrapMode, P::RichTextOverflow,
                      P::LineHeight, P::MaxLines}),
        textInitializer(L"Rich text")));
    registerDescriptor(descriptor<TextInput>(
        "TextInput", "Text Input", "btn_add_input", C::BasicContent, "input",
        {120.0f, 28.0f}, "M3 6h18v12H3z M7 9v6",
        with(base().add(interaction({P::OnTextChanged, P::OnSubmit})),
             {P::Text, P::TextHAlign, P::Password, P::ReadOnly})));
    registerDescriptor(descriptor<TextArea>(
        "TextArea", "Text Area", "btn_add_textarea", C::BasicContent,
        "textarea", {200.0f, 80.0f},
        "M4 4h16v16H4z M7 8h10 M7 12h10 M7 16h7",
        with(base().add(interaction({P::OnTextChanged})), {P::Text, P::ReadOnly}),
        textInitializer(L"Multiline text")));
    registerDescriptor(descriptor<CheckBox>(
        "CheckBox", "Check Box", "btn_add_checkbox", C::BasicContent, "chk",
        {120.0f, 28.0f}, "M4 5h15v15H4z M7 12l3 3 6-7",
        with(base().add(interaction({P::OnClick, P::OnToggled})),
             {P::Text, P::Checked}), textInitializer(L"Check")));
    registerDescriptor(descriptor<RadioButton>(
        "RadioButton", "Radio Button", "btn_add_radio", C::BasicContent,
        "radio", {120.0f, 28.0f},
        "M12 4a8 8 0 1 0 0 16 8 8 0 0 0 0-16z M12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6z",
        with(base().add(interaction({P::OnClick, P::OnToggled})),
             {P::Text, P::Checked}), textInitializer(L"Option")));
    registerDescriptor(descriptor<Slider>(
        "Slider", "Slider", "btn_add_slider", C::BasicContent, "slider",
        {160.0f, 24.0f}, "M4 8h10 M18 8h2 M4 16h3 M11 16h9 M14 5v6 M7 13v6",
        with(base().add(interaction({P::OnClick, P::OnValueChanged})),
             {P::ValueMin, P::ValueMax, P::Value})));
    registerDescriptor(descriptor<ProgressBar>(
        "ProgressBar", "Progress Bar", "btn_add_progress", C::BasicContent,
        "prog", {120.0f, 28.0f}, "M3 8h18v8H3z M5 10h9v4H5z",
        with(base(), {P::ValueMin, P::ValueMax, P::Value})));
    registerDescriptor(descriptor<Spinner>(
        "Spinner", "Spinner", "btn_add_spinner", C::BasicContent, "spinner",
        {120.0f, 28.0f}, "M12 3a9 9 0 1 1-6.4 2.7 M5 3v5h5", base()));
    registerDescriptor(descriptor<Image>(
        "Image", "Image", "btn_add_image", C::BasicContent, "image",
        {128.0f, 128.0f}, "M3 4h18v16H3z M6 16l4-5 3 3 2-2 3 4 M8 8h.01",
        with(base(), {P::Texture, P::ImageTint, P::ImageUvMinX,
                      P::ImageUvMinY, P::ImageUvMaxX, P::ImageUvMaxY}),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory&) {
            static_cast<Image&>(widget).setColor({0.16f, 0.19f, 0.24f, 1.0f});
        }));

    const PropertySchema collection = interaction({P::OnSelectionChanged});
    registerDescriptor(descriptor<ComboBox>(
        "ComboBox", "Combo Box", "btn_add_combo", C::Collections, "combo",
        {120.0f, 28.0f}, "M3 6h18v12H3z M16 10l2 2 2-2",
        with(base().add(collection), {P::Items}),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory&) {
            auto& value = static_cast<ComboBox&>(widget);
            value.setItems({L"Item 1", L"Item 2", L"Item 3"});
            value.setSelectedIndex(0);
        }));
    registerDescriptor(descriptor<ListView>(
        "ListView", "List View", "btn_add_list", C::Collections, "list",
        {220.0f, 160.0f}, "M5 6h2 M10 6h9 M5 12h2 M10 12h9 M5 18h2 M10 18h9",
        with(base().add(collection).add(P::OnItemActivated),
             {P::Items, P::SelectionMode, P::ItemHeight}),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory&) {
            static_cast<ListView&>(widget).setItems({L"Item 1", L"Item 2", L"Item 3"});
        }));
    registerDescriptor(descriptor<TileView>(
        "TileView", "Tile View", "btn_add_tiles", C::Collections, "tiles",
        {360.0f, 260.0f}, "M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z",
        with(base().add(collection).add(P::OnItemActivated),
             {P::Items, P::SelectionMode, P::TileWidth, P::TileHeight,
              P::TileSpacing}),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory&) {
            static_cast<TileView&>(widget).setItems({L"Tile 1", L"Tile 2", L"Tile 3", L"Tile 4"});
        }));
    registerDescriptor(descriptor<TreeView>(
        "TreeView", "Tree View", "btn_add_tree", C::Collections, "tree",
        {220.0f, 160.0f}, "M5 5h4 M9 5v6h4 M9 11v6h4 M13 8h6 M13 14h6 M13 20h6",
        with(base().add(collection), {P::Items, P::ItemHeight}),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory&) {
            static_cast<TreeView&>(widget).setTree({
                {L"Root", {}, true, true, -1},
                {L"Child", {}, false, false, 0},
            });
        }));
    registerDescriptor(descriptor<TabStrip>(
        "TabStrip", "Tab Strip", "btn_add_tabstrip", C::Collections,
        "tab_strip", {320.0f, 32.0f},
        "M3 7h6v4H3z M9 7h6v4H9z M15 7h6v4h-6z M3 11h18v8H3z",
        with(base().add(collection),
             {P::Items, P::TabOverflowMode, P::MinTabWidth}),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory&) {
            auto& strip = static_cast<TabStrip&>(widget);
            strip.addTab(L"Tab 1");
            strip.addTab(L"Tab 2");
        }));
    registerDescriptor(descriptor<TabControl>(
        "TabControl", "Tab Control", "btn_add_tabs", C::Collections, "tabs",
        {360.0f, 220.0f}, "M3 6h7v4H3z M10 6h7v4h-7z M3 10h18v10H3z",
        with(base().add(collection), {P::Items}),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory& ids) {
            auto& tabs = static_cast<TabControl&>(widget);
            for (int i = 0; i < 2; ++i) {
                auto* page = new Panel();
                page->setId(ids("tab_page", &widget));
                page->setBorderEnabled(false);
                tabs.addTabOwned(i == 0 ? L"Tab 1" : L"Tab 2", page);
            }
        }));

    registerDescriptor(descriptor<Panel>(
        "Panel", "Panel", "btn_add_panel", C::Layout, "panel",
        {200.0f, 120.0f}, "M4 4h16v16H4z M4 9h16", base()));
    registerDescriptor(descriptor<VBox>(
        "VBox", "Vertical Box", "btn_add_vbox", C::Layout, "vbox",
        {220.0f, 160.0f}, "M5 4h14v4H5z M5 10h14v4H5z M5 16h14v4H5z",
        with(base(), {P::Gravity, P::Spacing, P::Padding})));
    registerDescriptor(descriptor<HBox>(
        "HBox", "Horizontal Box", "btn_add_hbox", C::Layout, "hbox",
        {220.0f, 160.0f}, "M4 5h4v14H4z M10 5h4v14h-4z M16 5h4v14h-4z",
        with(base(), {P::Gravity, P::Spacing, P::Padding})));
    registerDescriptor(descriptor<GridPanel>(
        "GridPanel", "Grid Panel", "btn_add_grid", C::Layout, "grid",
        {200.0f, 120.0f}, "M4 4h16v16H4z M4 10h16 M4 16h16 M10 4v16 M16 4v16",
        with(base(), {P::GridRows, P::GridColumns, P::GridSpacingX,
                      P::GridSpacingY}),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory&) {
            auto& grid = static_cast<GridPanel&>(widget);
            grid.setRowCount(2);
            grid.setColumnCount(2);
        }));
    registerDescriptor(descriptor<ScrollView>(
        "ScrollView", "Scroll View", "btn_add_scroll", C::Layout, "scroll",
        {200.0f, 120.0f}, "M4 4h16v16H4z M17 7v7 M17 17h.01",
        with(base(), {P::VerticalScrollBarVisibility,
                      P::HorizontalScrollBarVisibility}),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory& ids) {
            auto& scroll = static_cast<ScrollView&>(widget);
            auto* content = new Panel();
            content->setId(ids("scroll_content", &widget));
            content->setBorderEnabled(false);
            content->setSize({240.0f, 320.0f});
            scroll.setContentOwned(content);
        }));
    registerDescriptor(descriptor<Separator>(
        "Separator", "Separator", "btn_add_separator", C::Layout, "separator",
        {180.0f, 1.0f}, "M3 12h18", base()));

    registerDescriptor(descriptor<Tooltip>(
        "Tooltip", "Tooltip", "btn_add_tooltip", C::Overlay, "tooltip",
        {120.0f, 28.0f}, "M4 5h16v11H9l-4 4v-4H4z",
        with(base(), {P::Text}), textInitializer(L"Tooltip")));
    registerDescriptor(descriptor<Window>(
        "Window", "Window", "btn_add_window", C::Overlay, "window",
        {360.0f, 240.0f}, "M3 4h18v16H3z M3 9h18 M6 6h.01",
        with(base().add(interaction({P::OnClose})), {P::Text}),
        textInitializer(L"Window")));
    registerDescriptor(descriptor<Modal>(
        "Modal", "Modal", "btn_add_modal", C::Overlay, "modal",
        {400.0f, 280.0f}, "M4 6h16v14H4z M7 3h14v14",
        base().add(interaction({P::OnClose})),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory& ids) {
            auto& modal = static_cast<Modal&>(widget);
            auto* content = new Panel();
            content->setId(ids("modal_content", &widget));
            content->setBorderEnabled(true);
            modal.setContentOwned(content);
        }));
    registerDescriptor(descriptor<ModalDialog>(
        "ModalDialog", "Modal Dialog", "btn_add_dialog", C::Overlay, "dialog",
        {400.0f, 280.0f}, "M4 5h16v15H4z M8 9h8 M8 13h8 M11 17h6",
        base().add(interaction({P::OnClose})),
        [](Widget& widget, const WidgetAuthoringDescriptor::UniqueIdFactory& ids) {
            auto& dialog = static_cast<ModalDialog&>(widget);
            auto* body = new Panel();
            body->setId(ids("dialog_body", &widget));
            body->setBorderEnabled(false);
            dialog.setBodyContentOwned(body);
        }));
}

const WidgetAuthoringDescriptor* WidgetAuthoringRegistry::find(
    const std::string& typeName) const {
    for (const WidgetAuthoringDescriptor& descriptor : _descriptors) {
        if (descriptor.typeName == typeName) return &descriptor;
    }
    return nullptr;
}

const WidgetAuthoringDescriptor* WidgetAuthoringRegistry::findByPaletteButton(
    const std::string& buttonId) const {
    for (const WidgetAuthoringDescriptor& descriptor : _descriptors) {
        if (descriptor.paletteButtonId == buttonId) return &descriptor;
    }
    return nullptr;
}

const WidgetAuthoringDescriptor* WidgetAuthoringRegistry::findForWidget(
    const Widget* widget) const {
    if (widget == nullptr) return nullptr;
    for (const WidgetAuthoringDescriptor& descriptor : _descriptors) {
        if (descriptor.matches && descriptor.matches(*widget)) return &descriptor;
    }
    return nullptr;
}

Widget* WidgetAuthoringRegistry::create(
    const std::string& typeName,
    const WidgetAuthoringDescriptor::UniqueIdFactory& idFactory) const {
    const WidgetAuthoringDescriptor* metadata = find(typeName);
    if (metadata == nullptr) return nullptr;
    Widget* widget = WidgetFactory::get().create(typeName);
    if (widget == nullptr) return nullptr;
    widget->setId(idFactory(metadata->idPrefix, widget));
    widget->setPosition({24.0f, 24.0f});
    widget->setSize(metadata->defaultSize);
    if (metadata->initialize) metadata->initialize(*widget, idFactory);
    return widget;
}

void WidgetAuthoringRegistry::registerDescriptor(
    WidgetAuthoringDescriptor descriptor) {
    const std::string svg =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" "
        "stroke=\"currentColor\" stroke-width=\"1.7\" "
        "stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"" +
        descriptor.iconPath + "\"/></svg>";
    descriptor.icon = SvgDocument::parse(svg);
    for (WidgetAuthoringDescriptor& existing : _descriptors) {
        if (existing.typeName == descriptor.typeName) {
            existing = std::move(descriptor);
            return;
        }
    }
    _descriptors.push_back(std::move(descriptor));
}

} // namespace ayt::ui
