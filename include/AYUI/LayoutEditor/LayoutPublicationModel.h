#pragma once

#include <string>
#include <vector>

namespace ayt::ui {

class LayoutInteractionRegistry;
class LayoutResourceCatalog;
class StyleSheet;
class Widget;

enum class LayoutPublicationDependencyKind {
    Texture,
    Style,
    EventHandler,
};

struct LayoutPublicationDependency {
    LayoutPublicationDependencyKind kind =
        LayoutPublicationDependencyKind::Texture;
    std::string key;
    std::vector<std::string> consumers;
    bool resolved = false;
};

enum class LayoutPublicationIssueCode {
    MissingDocumentAsset,
    DirtyDocument,
    NonPortableDocumentAsset,
    NonPortableResourceKey,
    EscapingResourceKey,
    ResourceCatalogUnavailable,
    MissingResource,
    InvalidResource,
    StyleSheetUnavailable,
    MissingStyle,
    InteractionRegistryUnavailable,
    MissingController,
    MissingEventHandler,
    EventTypeMismatch,
};

struct LayoutPublicationIssue {
    LayoutPublicationIssueCode code =
        LayoutPublicationIssueCode::MissingDocumentAsset;
    std::string consumer;
    std::string key;
    std::string message;
};

struct LayoutPublicationContext {
    // Stable path relative to a package/content root, never an authoring-machine
    // absolute filename.
    std::string documentAsset;
    bool documentDirty = false;
    const LayoutResourceCatalog* textureCatalog = nullptr;
    const StyleSheet* styleSheet = nullptr;
    const LayoutInteractionRegistry* interactionRegistry = nullptr;
    bool requireTextureCatalog = true;
    bool requireStyleSheet = true;
    bool requireInteractionRegistry = false;
};

// Builds the deterministic runtime dependency closure of one authored Layout.
// The model is authoring-only: it owns no Widget or resource and performs no
// filesystem writes. A packager can persist serializeManifest() after ready().
class LayoutPublicationModel {
public:
    void build(const std::vector<Widget*>& authoredWidgets,
               const LayoutPublicationContext& context);

    const std::vector<LayoutPublicationDependency>& dependencies() const {
        return _dependencies;
    }
    const std::vector<LayoutPublicationIssue>& issues() const {
        return _issues;
    }
    bool ready() const { return _issues.empty(); }
    std::string serializeManifest(bool pretty = true) const;

private:
    std::string _documentAsset;
    std::vector<LayoutPublicationDependency> _dependencies;
    std::vector<LayoutPublicationIssue> _issues;
};

} // namespace ayt::ui
