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
    const std::vector<LayoutComponentDefinition>& components() const {
        return _components;
    }
    std::size_t size() const { return _components.size(); }
    bool empty() const { return _components.empty(); }

    std::string serialize(bool pretty = true) const;
    bool deserialize(const std::string& jsonText,
                     std::string* error = nullptr);

    static bool validateId(const std::string& id,
                           std::string* error = nullptr);

private:
    std::vector<LayoutComponentDefinition> _components;
};

} // namespace ayt::ui
