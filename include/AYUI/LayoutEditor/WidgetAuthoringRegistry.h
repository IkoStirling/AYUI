#pragma once

#include "AYMath/MathTypes.h"
#include "AYUI/LayoutEditor/PropertySchema.h"
#include "AYUI/SvgIcon.h"

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class Widget;

enum class WidgetAuthoringCategory {
    BasicContent,
    Collections,
    Layout,
    Overlay
};

struct WidgetAuthoringDescriptor {
    using Matcher = std::function<bool(const Widget&)>;
    using UniqueIdFactory =
        std::function<std::string(const std::string&, Widget*)>;
    using Initializer = std::function<void(Widget&, const UniqueIdFactory&)>;

    std::string typeName;
    // Registration owner used to atomically unload/reload editor plugins.
    // Built-in entries use "AYUI.Core".
    std::string ownerId;
    std::string displayName;
    std::string paletteButtonId;
    WidgetAuthoringCategory category = WidgetAuthoringCategory::BasicContent;
    std::string idPrefix = "w";
    math::FVector2 defaultSize{120.0f, 28.0f};
    std::string iconPath;
    SvgDocument::Ptr icon;
    PropertySchema properties;
    Matcher matches;
    Initializer initialize;
};

class WidgetAuthoringRegistry {
public:
    static WidgetAuthoringRegistry& get();

    const std::vector<WidgetAuthoringDescriptor>& descriptors() const {
        return _descriptors;
    }
    const WidgetAuthoringDescriptor* find(const std::string& typeName) const;
    const WidgetAuthoringDescriptor* findByPaletteButton(
        const std::string& buttonId) const;
    const WidgetAuthoringDescriptor* findForWidget(const Widget* widget) const;

    Widget* create(const std::string& typeName,
                   const WidgetAuthoringDescriptor::UniqueIdFactory& idFactory) const;
    bool registerDescriptor(WidgetAuthoringDescriptor descriptor,
                            const std::string& ownerId = "AYUI.Core",
                            std::string* error = nullptr);
    bool replaceOwnerDescriptors(
        const std::string& ownerId,
        std::vector<WidgetAuthoringDescriptor> descriptors,
        std::string* error = nullptr);
    bool unregisterOwner(const std::string& ownerId);

private:
    WidgetAuthoringRegistry();
    std::vector<WidgetAuthoringDescriptor> _descriptors;
};

} // namespace ayt::ui
