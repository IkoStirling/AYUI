#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ayt::ui {

class Widget;

struct LayoutComponentDefinition {
    std::string id;
    std::string displayName;
    std::string category;
    std::string description;
    std::vector<std::string> tags;
    std::string widgetJson;
    // Stable content fingerprint of the canonical Widget JSON. It changes
    // only when the component source changes, not when library metadata does.
    std::string sourceRevision;
};

struct LayoutComponentOverride {
    // RFC 6901 JSON Pointer into the serialized Widget tree.
    std::string path;
    // One complete JSON value. Keeping it encoded avoids coupling the
    // authoring model to nlohmann::json in the public interface.
    std::string valueJson;
};

struct LayoutComponentInstance {
    std::string componentId;
    std::string sourceRevision;
    std::vector<LayoutComponentOverride> overrides;
};

struct LayoutComponentMaterialization {
    Widget* widget = nullptr;
    std::string appliedRevision;
    bool sourceChanged = false;
    std::vector<std::string> conflicts;
};

// Project/external reusable Widget library. Unlike LayoutReuseLibrary, these
// definitions do not travel inside one layout document. Instances are still
// expanded Widget trees, so runtime loading never depends on this library.
class LayoutComponentLibrary {
public:
    bool define(const std::string& id, const std::string& displayName,
                const std::string& category, Widget* widget,
                std::string* error = nullptr,
                const std::string& description = {},
                const std::vector<std::string>& tags = {});
    bool defineJson(const LayoutComponentDefinition& definition,
                    std::string* error = nullptr);
    bool remove(const std::string& id);
    void clear() { _components.clear(); }

    const LayoutComponentDefinition* find(const std::string& id) const;
    Widget* instantiate(const std::string& id) const;
    bool createInstance(const std::string& id,
                        LayoutComponentInstance& outInstance) const;
    LayoutComponentMaterialization materialize(
        const LayoutComponentInstance& instance) const;
    // Accepts the current source revision only when every override still
    // resolves. The caller owns result.widget on success.
    LayoutComponentMaterialization rebase(
        LayoutComponentInstance& instance) const;
    const std::vector<LayoutComponentDefinition>& components() const {
        return _components;
    }
    std::size_t size() const { return _components.size(); }
    bool empty() const { return _components.empty(); }

    std::string serialize(bool pretty = true) const;
    bool deserialize(const std::string& jsonText,
                     std::string* error = nullptr);

    static std::string serializeInstance(
        const LayoutComponentInstance& instance, bool pretty = true);
    static bool deserializeInstance(const std::string& jsonText,
                                    LayoutComponentInstance& outInstance,
                                    std::string* error = nullptr);

    static bool validateId(const std::string& id,
                           std::string* error = nullptr);

private:
    std::vector<LayoutComponentDefinition> _components;
};

} // namespace ayt::ui
