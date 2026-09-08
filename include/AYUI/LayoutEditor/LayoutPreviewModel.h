#pragma once

#include "AYMath/MathTypes.h"

#include <string>
#include <vector>

namespace ayt::ui {

struct LayoutPreviewSettings {
    std::wstring name = L"Document";
    float pixelWidth = 0.0f;
    float pixelHeight = 0.0f;
    float dpiScale = 1.0f;
    math::FVector4 safeAreaPixels{0.0f, 0.0f, 0.0f, 0.0f};
    bool followDocumentSize = true;
    bool showSafeArea = false;
};

class LayoutPreviewModel {
public:
    LayoutPreviewModel();

    const std::vector<LayoutPreviewSettings>& presets() const {
        return _presets;
    }
    int presetIndex() const { return _presetIndex; }
    const LayoutPreviewSettings& settings() const { return _settings; }

    bool selectPreset(int index);
    void setCustom(LayoutPreviewSettings settings);
    void setPixelSize(float width, float height);
    void setDpiScale(float scale);
    void setSafeAreaPixels(const math::FVector4& insets);
    void setSafeAreaVisible(bool visible);

    math::FVector2 logicalSize(const math::FVector2& documentSize) const;
    math::FVector4 logicalSafeArea() const;

private:
    void normalize();

    std::vector<LayoutPreviewSettings> _presets;
    LayoutPreviewSettings _settings;
    int _presetIndex = 0;
};

} // namespace ayt::ui
