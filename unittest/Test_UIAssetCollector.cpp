#include "AYTest.h"
#include "AYTestFixtures.h"

#include "AYUI/Packaging/UIAssetCollector.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace ui_asset_collector_test {

namespace fs = std::filesystem;
using namespace ayt::ui::packaging;

struct TemporaryContentRoot {
    ayt::test::ScratchDirectory scratch{"asset-collector"};
    fs::path path = scratch.path();

    TemporaryContentRoot() {
        std::error_code error;
        fs::create_directories(path / "UI", error);
        fs::create_directories(path / "Textures", error);
    }

    void write(const fs::path& relative, const std::string& contents) const {
        std::ofstream output(path / relative,
            std::ios::binary | std::ios::trunc);
        output << contents;
    }
};

} // namespace ui_asset_collector_test

using namespace ui_asset_collector_test;

TEST_SUITE(AYUI_ModuleAssetCollector)

TEST_CASE(ui_module_collects_nested_layout_assets_without_central_field_list) {
    TemporaryContentRoot content;
    content.write("Textures/hero.png", "texture");
    content.write("UI/main.ui.json", R"({
        "format": "AYUILayout",
        "root": {
            "type": "Panel",
            "id": "root",
            "children": [
                {"type":"Image","id":"hero","textureName":"ui.hero"},
                {"type":"TabControl","pages":[
                    {"title":"Nested","content":{
                        "type":"Image","id":"nested","textureName":"ui.hero"
                    }}
                ]}
            ]
        }
    })");

    UIAssetCollectionRequest request;
    request.contentRoot = content.path.string();
    request.entryLayouts = {"UI/main.ui.json", "UI\\main.ui.json"};
    request.sourceResolver = [](UIAssetKind kind, std::string_view key) {
        return kind == UIAssetKind::Texture && key == "ui.hero"
            ? std::string("Textures/hero.png") : std::string();
    };

    const UIAssetCollectionResult result = UIAssetCollector().collect(request);
    CHECK(result.ready());
    CHECK(result.assets.size() == 2u);

    std::size_t layoutCount = 0u;
    std::size_t textureCount = 0u;
    std::size_t textureConsumerCount = 0u;
    std::string textureSource;
    for (const UIAssetContribution& asset : result.assets) {
        if (asset.kind == UIAssetKind::Layout) ++layoutCount;
        if (asset.kind == UIAssetKind::Texture) {
            ++textureCount;
            textureConsumerCount = asset.consumers.size();
            textureSource = asset.sourceAsset;
        }
    }
    CHECK(layoutCount == 1u);
    CHECK(textureCount == 1u);
    CHECK(textureConsumerCount == 2u);
    CHECK(textureSource == "Textures/hero.png");

    const nlohmann::json manifest = nlohmann::json::parse(
        result.serializeManifest(false));
    CHECK(manifest["format"] == "AYUIModulePackageContribution");
    CHECK(manifest["version"] == 1);
    CHECK(manifest["module"] == "AYUI");
    CHECK(manifest["ready"] == true);
    CHECK(manifest["assets"].size() == 2u);
}

TEST_CASE(ui_module_identity_maps_portable_texture_keys_without_resolver) {
    TemporaryContentRoot content;
    content.write("Textures/icon.png", "texture");
    content.write("UI/icon.ui.json", R"({
        "type":"Image",
        "id":"icon",
        "textureName":"Textures\\icon.png"
    })");

    UIAssetCollectionRequest request;
    request.contentRoot = content.path.string();
    request.entryLayouts = {"UI/icon.ui.json"};

    const UIAssetCollectionResult result = UIAssetCollector().collect(request);
    CHECK(result.ready());
    CHECK(result.assets.size() == 2u);
    CHECK(result.assets[1].kind == UIAssetKind::Texture);
    CHECK(result.assets[1].packageKey == "Textures/icon.png");
    CHECK(result.assets[1].sourceAsset == "Textures/icon.png");
}

TEST_CASE(ui_module_rejects_nonportable_missing_and_malformed_dependencies) {
    TemporaryContentRoot content;
    content.write("UI/broken.ui.json", R"({
        "type":"Panel",
        "children":[
            {"type":"Image","id":"absolute","textureName":"D:/private.png"},
            {"type":"Image","id":"escape","textureName":"../outside.png"},
            {"type":"Image","id":"missing","textureName":"Textures/missing.png"},
            {"type":"Image","id":"malformed","textureName":42}
        ]
    })");
    content.write("UI/not-layout.json", "{}");

    UIAssetCollectionRequest request;
    request.contentRoot = content.path.string();
    request.entryLayouts = {
        "UI/broken.ui.json",
        "UI/missing.ui.json",
        "../outside.ui.json",
        "D:/absolute.ui.json",
        "UI/not-layout.json",
    };

    const UIAssetCollectionResult result = UIAssetCollector().collect(request);
    CHECK(!result.ready());

    std::size_t nonPortable = 0u;
    std::size_t escaping = 0u;
    std::size_t missing = 0u;
    std::size_t invalidLayout = 0u;
    std::size_t malformed = 0u;
    for (const UIAssetCollectionIssue& issue : result.issues) {
        switch (issue.code) {
        case UIAssetCollectionIssueCode::NonPortableAsset:
            ++nonPortable;
            break;
        case UIAssetCollectionIssueCode::EscapingAsset:
            ++escaping;
            break;
        case UIAssetCollectionIssueCode::MissingAsset:
            ++missing;
            break;
        case UIAssetCollectionIssueCode::InvalidLayout:
            ++invalidLayout;
            break;
        case UIAssetCollectionIssueCode::InvalidResourceReference:
            ++malformed;
            break;
        default:
            break;
        }
    }
    CHECK(nonPortable == 2u);
    CHECK(escaping == 2u);
    CHECK(missing == 2u);
    CHECK(invalidLayout == 1u);
    CHECK(malformed == 1u);
}

TEST_CASE(ui_module_collects_transitive_theme_inheritance) {
    TemporaryContentRoot content;
    std::error_code error;
    fs::create_directories(content.path / "Themes", error);
    content.write("Themes/base.theme.json", R"({
        "tokens":{"color":{"surface":[0.1,0.1,0.1,1.0]}}
    })");
    content.write("Themes/game.theme.json", R"({
        "extends":"base",
        "tokens":{"color":{"accent":[0.1,0.5,0.9,1.0]}}
    })");

    UIAssetCollectionRequest request;
    request.contentRoot = content.path.string();
    request.entryThemes = {{"game", "Themes/game.theme.json"}};
    request.sourceResolver = [](UIAssetKind kind, std::string_view key) {
        return kind == UIAssetKind::Theme && key == "base"
            ? std::string("Themes/base.theme.json") : std::string();
    };

    const UIAssetCollectionResult result = UIAssetCollector().collect(request);
    CHECK(result.ready());
    CHECK(result.assets.size() == 2u);

    std::size_t themeCount = 0u;
    std::size_t inheritedConsumerCount = 0u;
    for (const UIAssetContribution& asset : result.assets) {
        if (asset.kind == UIAssetKind::Theme) {
            ++themeCount;
            if (asset.packageKey == "base") {
                inheritedConsumerCount = asset.consumers.size();
            }
        }
    }
    CHECK(themeCount == 2u);
    CHECK(inheritedConsumerCount == 1u);
}

TEST_CASE(ui_module_rejects_theme_inheritance_cycles) {
    TemporaryContentRoot content;
    std::error_code error;
    fs::create_directories(content.path / "Themes", error);
    content.write("Themes/a.theme.json", R"({"extends":"b"})");
    content.write("Themes/b.theme.json", R"({"extends":"a"})");

    UIAssetCollectionRequest request;
    request.contentRoot = content.path.string();
    request.entryThemes = {{"a", "Themes/a.theme.json"}};
    request.sourceResolver = [](UIAssetKind kind, std::string_view key) {
        if (kind != UIAssetKind::Theme) return std::string();
        if (key == "a") return std::string("Themes/a.theme.json");
        if (key == "b") return std::string("Themes/b.theme.json");
        return std::string();
    };

    const UIAssetCollectionResult result = UIAssetCollector().collect(request);
    CHECK(!result.ready());
    std::size_t cycleCount = 0u;
    for (const UIAssetCollectionIssue& issue : result.issues) {
        if (issue.code
            == UIAssetCollectionIssueCode::ThemeInheritanceCycle) {
            ++cycleCount;
        }
    }
    CHECK(cycleCount == 1u);
}

TEST_CASE(ui_module_collector_advertises_its_wire_assets) {
    const UIAssetCollector collector;
    CHECK(collector.accepts("UI/Main.ui.json"));
    CHECK(collector.accepts("UI/Main.UI.JSON"));
    CHECK(collector.accepts("Themes/Game.theme.json"));
    CHECK(!collector.accepts("UI/Main.uiflow.json"));
    CHECK(!collector.accepts("UI/project.ayuicomponents.json"));
}

TEST_SUITE_END
