#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ayt::ui {

class Widget;
class UIAnimationLibrary;

struct LayoutReusableBlock {
    std::string name;
    std::string widgetJson;
};

// Document-local reusable snippets. Instances are expanded Widget trees, so
// runtime loading has no lookup dependency and each insertion can diverge.
// The definitions travel beside the root in an optional JSON envelope.
class LayoutReuseLibrary {
public:
    bool define(const std::string& name, Widget* widget,
                std::string* error = nullptr);
    bool defineJson(const std::string& name, const std::string& widgetJson,
                    std::string* error = nullptr);
    bool remove(const std::string& name);
    void clear() { _blocks.clear(); }

    const LayoutReusableBlock* find(const std::string& name) const;
    Widget* instantiate(const std::string& name) const;
    const std::vector<LayoutReusableBlock>& blocks() const { return _blocks; }
    std::size_t size() const { return _blocks.size(); }
    bool empty() const { return _blocks.empty(); }

    std::string encodeDocument(Widget* root, bool pretty = true) const;
    std::string encodeDocument(Widget* root,
                               const UIAnimationLibrary& animations,
                               bool pretty = true) const;
    bool decodeDocument(const std::string& documentJson,
                        std::string& outRootJson,
                        std::string* error = nullptr);
    bool decodeDocument(const std::string& documentJson,
                        std::string& outRootJson,
                        UIAnimationLibrary& outAnimations,
                        std::string* error = nullptr);

    static bool validateName(const std::string& name,
                             std::string* error = nullptr);

private:
    std::vector<LayoutReusableBlock> _blocks;
};

} // namespace ayt::ui
