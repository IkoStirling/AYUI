#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ayt::ui::packaging {

enum class UIAssetKind {
    Layout,
    Texture,
};

struct UIAssetContribution {
    UIAssetKind kind = UIAssetKind::Layout;
    // Stable identity used by runtime references and the destination package.
    std::string packageKey;
    // Portable path relative to the request content root.
    std::string sourceAsset;
    std::vector<std::string> consumers;
};

enum class UIAssetCollectionIssueCode {
    MissingContentRoot,
    NonPortableAsset,
    EscapingAsset,
    MissingAsset,
    AssetTooLarge,
    InvalidJson,
    InvalidLayout,
    InvalidResourceReference,
    UnresolvedResource,
};

struct UIAssetCollectionIssue {
    UIAssetCollectionIssueCode code =
        UIAssetCollectionIssueCode::MissingContentRoot;
    std::string asset;
    std::string consumer;
    std::string message;
};

// Maps an AYUI-owned logical resource key to a source file relative to the
// content root. Returning an empty string rejects the reference. When omitted,
// portable texture keys map to an equally named source asset.
using UIAssetSourceResolver = std::function<std::string(
    UIAssetKind kind, std::string_view packageKey)>;

struct UIAssetCollectionRequest {
    std::string contentRoot;
    // Only root UI assets are supplied by the package graph. AYUI discovers
    // their transitive dependencies; a central cook tool must not know Widget
    // fields such as Image.textureName.
    std::vector<std::string> entryLayouts;
    UIAssetSourceResolver sourceResolver;
    bool requireSourceFiles = true;
};

struct UIAssetCollectionResult {
    std::vector<UIAssetContribution> assets;
    std::vector<UIAssetCollectionIssue> issues;

    [[nodiscard]] bool ready() const noexcept { return issues.empty(); }
    explicit operator bool() const noexcept { return ready(); }
    std::string serializeManifest(bool pretty = true) const;
};

// Headless package contribution owned by AYUI. It reads no project, Scene or
// Flow metadata and writes no package. A future package coordinator can invoke
// this collector through a common module-contributor adapter and merge the
// returned files with contributions from other modules.
class UIAssetCollector {
public:
    static constexpr std::string_view moduleId() noexcept { return "AYUI"; }

    [[nodiscard]] bool accepts(std::string_view asset) const noexcept;
    UIAssetCollectionResult collect(
        const UIAssetCollectionRequest& request) const;
};

} // namespace ayt::ui::packaging
