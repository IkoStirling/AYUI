#include "AYUI/LayoutEditor/LayoutPublicationModel.h"

#include "AYUI/Image.h"
#include "AYUI/LayoutEditor/LayoutInteractionModel.h"
#include "AYUI/LayoutEditor/LayoutResourceCatalog.h"
#include "AYUI/Style.h"
#include "AYUI/Widget.h"

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>

namespace ayt::ui {
namespace {

using DependencyKey = std::pair<LayoutPublicationDependencyKind, std::string>;

const char* kindName(LayoutPublicationDependencyKind kind) {
    switch (kind) {
    case LayoutPublicationDependencyKind::Texture: return "texture";
    case LayoutPublicationDependencyKind::Style: return "style";
    case LayoutPublicationDependencyKind::EventHandler: return "eventHandler";
    }
    return "unknown";
}

std::string consumerName(const Widget* widget, std::size_t index) {
    if (widget != nullptr && !widget->getId().empty()) return widget->getId();
    return "@widget[" + std::to_string(index) + "]";
}

std::string portable(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

bool absoluteLike(const std::string& value) {
    return !value.empty() && (value.front() == '/' || value.front() == '\\'
        || (value.size() > 1u && value[1] == ':'));
}

bool escapesRoot(const std::string& value) {
    std::istringstream stream(portable(value));
    std::string component;
    while (std::getline(stream, component, '/')) {
        if (component == "..") return true;
    }
    return false;
}

void addIssue(std::vector<LayoutPublicationIssue>& issues,
              LayoutPublicationIssueCode code, std::string consumer,
              std::string key, std::string message) {
    issues.push_back({code, std::move(consumer), std::move(key),
                      std::move(message)});
}

} // namespace

void LayoutPublicationModel::build(
    const std::vector<Widget*>& authoredWidgets,
    const LayoutPublicationContext& context) {
    _documentAsset = portable(context.documentAsset);
    _dependencies.clear();
    _issues.clear();

    if (_documentAsset.empty()) {
        addIssue(_issues, LayoutPublicationIssueCode::MissingDocumentAsset,
                 {}, {}, "Save the Layout to a stable asset path before publication.");
    } else if (absoluteLike(context.documentAsset)
               || escapesRoot(context.documentAsset)) {
        addIssue(_issues, LayoutPublicationIssueCode::NonPortableDocumentAsset,
                 {}, context.documentAsset,
                 "Layout asset identity must stay relative to the content root.");
    }
    if (context.documentDirty) {
        addIssue(_issues, LayoutPublicationIssueCode::DirtyDocument, {},
                 _documentAsset,
                 "Save the Layout before producing a publication manifest.");
    }

    std::map<DependencyKey, LayoutPublicationDependency> closure;
    const auto addDependency = [&](LayoutPublicationDependencyKind kind,
                                   const std::string& key,
                                   const std::string& consumer) ->
                                   LayoutPublicationDependency& {
        const DependencyKey identity{kind, key};
        auto [it, inserted] = closure.try_emplace(identity);
        LayoutPublicationDependency& dependency = it->second;
        if (inserted) {
            dependency.kind = kind;
            dependency.key = key;
        }
        dependency.consumers.push_back(consumer);
        return dependency;
    };

    for (std::size_t index = 0; index < authoredWidgets.size(); ++index) {
        Widget* widget = authoredWidgets[index];
        if (widget == nullptr) continue;
        const std::string consumer = consumerName(widget, index);

        if (!widget->getStyleId().empty()) {
            LayoutPublicationDependency& dependency = addDependency(
                LayoutPublicationDependencyKind::Style,
                widget->getStyleId(), consumer);
            if (context.styleSheet == nullptr) {
                if (context.requireStyleSheet) {
                    addIssue(_issues,
                             LayoutPublicationIssueCode::StyleSheetUnavailable,
                             consumer, dependency.key,
                             "Style closure cannot be resolved without a StyleSheet.");
                }
            } else if (!context.styleSheet->containsStyle(dependency.key)) {
                addIssue(_issues, LayoutPublicationIssueCode::MissingStyle,
                         consumer, dependency.key,
                         "Referenced Style is absent from the publication StyleSheet.");
            } else {
                dependency.resolved = true;
            }
        }

        if (const auto* image = dynamic_cast<const Image*>(widget)) {
            const std::string& original = image->getTextureName();
            if (!original.empty()) {
                const std::string key = portable(original);
                LayoutPublicationDependency& dependency = addDependency(
                    LayoutPublicationDependencyKind::Texture, key, consumer);
                if (absoluteLike(original)) {
                    addIssue(_issues,
                             LayoutPublicationIssueCode::NonPortableResourceKey,
                             consumer, original,
                             "Absolute texture paths are not deployable.");
                } else if (escapesRoot(original)) {
                    addIssue(_issues,
                             LayoutPublicationIssueCode::EscapingResourceKey,
                             consumer, original,
                             "Texture resource key escapes the content root.");
                } else if (context.textureCatalog == nullptr) {
                    if (context.requireTextureCatalog) {
                        addIssue(_issues,
                            LayoutPublicationIssueCode::ResourceCatalogUnavailable,
                            consumer, key,
                            "Texture closure cannot be resolved without a resource catalog.");
                    }
                } else {
                    const LayoutTextureResource* resource =
                        context.textureCatalog->find(key);
                    if (resource == nullptr
                        || resource->state == LayoutResourceState::Missing) {
                        addIssue(_issues,
                                 LayoutPublicationIssueCode::MissingResource,
                                 consumer, key,
                                 "Texture resource is missing from the publication catalog.");
                    } else if (resource->state
                               == LayoutResourceState::Invalid) {
                        addIssue(_issues,
                                 LayoutPublicationIssueCode::InvalidResource,
                                 consumer, key,
                                 "Texture resource exists but is invalid.");
                    } else {
                        dependency.resolved = true;
                    }
                }
            }
        }

        for (const auto& [eventName, handlerName] :
             widget->getEventBindings()) {
            const std::string key = widget->getControllerId() + "#"
                + handlerName + "@" + eventName;
            LayoutPublicationDependency& dependency = addDependency(
                LayoutPublicationDependencyKind::EventHandler, key, consumer);
            if (context.interactionRegistry == nullptr) {
                if (context.requireInteractionRegistry) {
                    addIssue(_issues,
                        LayoutPublicationIssueCode::InteractionRegistryUnavailable,
                        consumer, key,
                        "Event closure cannot be resolved without an interaction registry.");
                }
                continue;
            }
            const LayoutInteractionRegistry& registry =
                *context.interactionRegistry;
            const LayoutControllerContract* controller =
                registry.findController(widget->getControllerId());
            if (controller == nullptr) {
                addIssue(_issues, LayoutPublicationIssueCode::MissingController,
                         consumer, key,
                         "Event binding references an unknown Controller.");
                continue;
            }
            const LayoutEventHandlerContract* handler = registry.findHandler(
                widget->getControllerId(), handlerName);
            if (handler == nullptr) {
                addIssue(_issues,
                         LayoutPublicationIssueCode::MissingEventHandler,
                         consumer, key,
                         "Event binding references an unknown handler.");
            } else if (!registry.handlerAccepts(*handler, eventName)) {
                addIssue(_issues, LayoutPublicationIssueCode::EventTypeMismatch,
                         consumer, key,
                         "Event binding and handler contract are incompatible.");
            } else {
                dependency.resolved = true;
            }
        }
    }

    _dependencies.reserve(closure.size());
    for (auto& [identity, dependency] : closure) {
        (void)identity;
        std::sort(dependency.consumers.begin(), dependency.consumers.end());
        dependency.consumers.erase(
            std::unique(dependency.consumers.begin(),
                        dependency.consumers.end()),
            dependency.consumers.end());
        _dependencies.push_back(std::move(dependency));
    }
    std::sort(_issues.begin(), _issues.end(), [](const auto& a, const auto& b) {
        if (a.consumer != b.consumer) return a.consumer < b.consumer;
        if (a.key != b.key) return a.key < b.key;
        return a.code < b.code;
    });
}

std::string LayoutPublicationModel::serializeManifest(bool pretty) const {
    nlohmann::json document = {
        {"format", "AYUILayoutPublicationManifest"},
        {"version", 1},
        {"layout", _documentAsset},
        {"ready", ready()},
        {"dependencies", nlohmann::json::array()},
    };
    for (const LayoutPublicationDependency& dependency : _dependencies) {
        document["dependencies"].push_back({
            {"kind", kindName(dependency.kind)},
            {"key", dependency.key},
            {"consumers", dependency.consumers},
            {"resolved", dependency.resolved},
        });
    }
    return document.dump(pretty ? 2 : -1);
}

} // namespace ayt::ui
