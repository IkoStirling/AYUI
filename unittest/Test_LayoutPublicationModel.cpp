#include "AYTest.h"

#include "AYUI/Button.h"
#include "AYUI/Image.h"
#include "AYUI/LayoutEditor/LayoutInteractionModel.h"
#include "AYUI/LayoutEditor/LayoutPublicationModel.h"
#include "AYUI/LayoutEditor/LayoutResourceCatalog.h"
#include "AYUI/Panel.h"
#include "AYUI/Style.h"

#include <nlohmann/json.hpp>

using namespace ayt::ui;

TEST_SUITE(AYUI_LayoutPublication)

TEST_CASE(layout_publication_builds_deterministic_resolved_closure) {
    Panel root;
    root.setId("document_root");
    root.setStyleId("surface");

    Image first;
    first.setId("hero");
    first.setTexture("Assets/UI/hero.png");
    root.addChildExternal(&first);
    Image second;
    second.setId("hero_copy");
    second.setTexture("Assets/UI/hero.png");
    root.addChildExternal(&second);

    Button button;
    button.setId("start");
    button.setControllerId("Menu");
    button.setEventBinding("onClick", "startGame");
    root.addChildExternal(&button);

    LayoutResourceCatalog resources;
    resources.setEntries({{"Assets/UI/hero.png", L"Hero", {}, {},
                           LayoutResourceState::Ready}});
    StyleSheet styles;
    styles.setStyle("surface", StyleBuilder::makePanel());
    LayoutInteractionRegistry interactions;
    interactions.replace({{"Menu", {{"startGame", {"onClick"}}}}});

    LayoutPublicationContext context;
    context.documentAsset = "UI/main.ui.json";
    context.textureCatalog = &resources;
    context.styleSheet = &styles;
    context.interactionRegistry = &interactions;
    context.requireInteractionRegistry = true;

    LayoutPublicationModel publication;
    publication.build({&root, &first, &second, &button}, context);
    CHECK(publication.ready());
    CHECK(publication.dependencies().size() == 3u);

    std::size_t textureConsumers = 0u;
    for (const LayoutPublicationDependency& dependency :
         publication.dependencies()) {
        CHECK(dependency.resolved);
        if (dependency.kind == LayoutPublicationDependencyKind::Texture) {
            textureConsumers = dependency.consumers.size();
        }
    }
    CHECK(textureConsumers == 2u);

    const nlohmann::json manifest = nlohmann::json::parse(
        publication.serializeManifest(false));
    CHECK(manifest["format"] == "AYUILayoutPublicationManifest");
    CHECK(manifest["version"] == 1);
    CHECK(manifest["layout"] == "UI/main.ui.json");
    CHECK(manifest["ready"] == true);
    CHECK(manifest["dependencies"].size() == 3u);
}

TEST_CASE(layout_publication_blocks_dirty_nonportable_and_unresolved_inputs) {
    Image absolute;
    absolute.setId("absolute");
    absolute.setTexture("D:/draft/private.png");
    Image escaping;
    escaping.setId("escaping");
    escaping.setTexture("../outside.png");
    Image missing;
    missing.setId("missing");
    missing.setTexture("Assets/UI/missing.png");
    Image invalid;
    invalid.setId("invalid");
    invalid.setTexture("Assets/UI/invalid.png");
    invalid.setStyleId("missing_style");
    Button event;
    event.setId("event");
    event.setControllerId("MissingController");
    event.setEventBinding("onClick", "run");

    LayoutResourceCatalog resources;
    resources.setEntries({
        {"Assets/UI/invalid.png", L"Invalid", {}, {},
         LayoutResourceState::Invalid},
    });
    StyleSheet styles;
    LayoutInteractionRegistry interactions;

    LayoutPublicationContext context;
    context.documentAsset = "UI/broken.ui.json";
    context.documentDirty = true;
    context.textureCatalog = &resources;
    context.styleSheet = &styles;
    context.interactionRegistry = &interactions;
    context.requireInteractionRegistry = true;

    LayoutPublicationModel publication;
    publication.build({&absolute, &escaping, &missing, &invalid, &event},
                      context);
    CHECK(!publication.ready());

    std::size_t dirty = 0u;
    std::size_t absolutePath = 0u;
    std::size_t escapingPath = 0u;
    std::size_t missingResource = 0u;
    std::size_t invalidResource = 0u;
    std::size_t missingStyle = 0u;
    std::size_t missingController = 0u;
    for (const LayoutPublicationIssue& issue : publication.issues()) {
        switch (issue.code) {
        case LayoutPublicationIssueCode::DirtyDocument: ++dirty; break;
        case LayoutPublicationIssueCode::NonPortableResourceKey:
            ++absolutePath; break;
        case LayoutPublicationIssueCode::EscapingResourceKey:
            ++escapingPath; break;
        case LayoutPublicationIssueCode::MissingResource:
            ++missingResource; break;
        case LayoutPublicationIssueCode::InvalidResource:
            ++invalidResource; break;
        case LayoutPublicationIssueCode::MissingStyle: ++missingStyle; break;
        case LayoutPublicationIssueCode::MissingController:
            ++missingController; break;
        default: break;
        }
    }
    CHECK(dirty == 1u);
    CHECK(absolutePath == 1u);
    CHECK(escapingPath == 1u);
    CHECK(missingResource == 1u);
    CHECK(invalidResource == 1u);
    CHECK(missingStyle == 1u);
    CHECK(missingController == 1u);

    const nlohmann::json manifest = nlohmann::json::parse(
        publication.serializeManifest(false));
    CHECK(manifest["ready"] == false);
}

TEST_CASE(layout_publication_requires_resolvers_only_when_policy_requests_them) {
    Image image;
    image.setId("preview");
    image.setTexture("virtual://preview");
    image.setStyleId("external_style");

    LayoutPublicationContext strict;
    strict.documentAsset = "UI/preview.ui.json";
    LayoutPublicationModel publication;
    publication.build({&image}, strict);
    CHECK(!publication.ready());
    CHECK(publication.issues().size() == 2u);

    strict.requireTextureCatalog = false;
    strict.requireStyleSheet = false;
    publication.build({&image}, strict);
    CHECK(publication.ready());
    CHECK(publication.dependencies().size() == 2u);
    CHECK(!publication.dependencies()[0].resolved);
    CHECK(!publication.dependencies()[1].resolved);
}

TEST_SUITE_END
