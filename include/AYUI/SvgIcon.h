#pragma once

#include "AYUI/LeafWidget.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ayt::ui {

namespace detail { struct SvgDocumentData; }

// Immutable, backend-independent SVG icon document.  This intentionally
// implements a safe icon subset rather than a browser DOM: <svg>/<path>, the
// complete SVG path command set, viewBox, fill/stroke/currentColor and stroke
// cap/join. Unsupported geometry/effects fail parsing instead of being
// silently omitted.
class SvgDocument final {
public:
    using Ptr = std::shared_ptr<const SvgDocument>;

    static Ptr parse(std::string_view source, std::string* error = nullptr);
    static Ptr loadFromFile(const std::filesystem::path& path,
                            std::string* error = nullptr);

    const math::FRectangle& getViewBox() const;
    size_t getPathCount() const;
    bool empty() const;

    // Draws with SVG preserveAspectRatio="xMidYMid meet" semantics. Paints
    // authored as currentColor resolve to currentColor; explicit SVG colors
    // remain unchanged.
    bool draw(IRenderBackend& renderer, const math::FRectangle& bounds,
              const math::FVector4& currentColor) const;

private:
    explicit SvgDocument(std::shared_ptr<const detail::SvgDocumentData> data);
    std::shared_ptr<const detail::SvgDocumentData> _data;
};

class SvgIcon final : public LeafWidget {
public:
    SvgIcon();
    ~SvgIcon() override = default;

    void setDocument(SvgDocument::Ptr document);
    const SvgDocument::Ptr& getDocument() const { return _document; }

    bool loadFromFile(const std::filesystem::path& path,
                      std::string* error = nullptr);

    void setColor(const math::FVector4& color);
    const math::FVector4& getColor() const { return _color; }

    void setContentPadding(float padding);
    float getContentPadding() const { return _contentPadding; }

protected:
    void onRender(IRenderBackend& renderer) override;

private:
    SvgDocument::Ptr _document;
    math::FVector4 _color{1.0f, 1.0f, 1.0f, 1.0f};
    float _contentPadding = 0.0f;
};

} // namespace ayt::ui
