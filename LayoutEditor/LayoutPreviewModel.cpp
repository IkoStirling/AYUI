#include "AYUI/LayoutEditor/LayoutPreviewModel.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

LayoutPreviewModel::LayoutPreviewModel() {
    _presets = {
        {L"Document", 0.0f, 0.0f, 1.0f, {}, true, false},
        {L"Desktop 1280 x 720", 1280.0f, 720.0f, 1.0f, {}, false, false},
        {L"Desktop HiDPI 2560 x 1440", 2560.0f, 1440.0f, 2.0f,
         {}, false, false},
        {L"Phone 1170 x 2532", 1170.0f, 2532.0f, 3.0f,
         {0.0f, 132.0f, 0.0f, 102.0f}, false, true},
        {L"Tablet 2048 x 2732", 2048.0f, 2732.0f, 2.0f,
         {0.0f, 48.0f, 0.0f, 40.0f}, false, true}
    };
    _settings = _presets.front();
}

bool LayoutPreviewModel::selectPreset(int index) {
    if (index < 0 || index >= static_cast<int>(_presets.size())) return false;
    _presetIndex = index;
    _settings = _presets[static_cast<size_t>(index)];
    normalize();
    return true;
}

void LayoutPreviewModel::setCustom(LayoutPreviewSettings settings) {
    _settings = std::move(settings);
    _settings.name = L"Custom";
    _settings.followDocumentSize = false;
    _presetIndex = -1;
    normalize();
}

void LayoutPreviewModel::setPixelSize(float width, float height) {
    _settings.pixelWidth = width;
    _settings.pixelHeight = height;
    _settings.followDocumentSize = false;
    _settings.name = L"Custom";
    _presetIndex = -1;
    normalize();
}

void LayoutPreviewModel::setDpiScale(float scale) {
    _settings.dpiScale = scale;
    _settings.name = L"Custom";
    _presetIndex = -1;
    normalize();
}

void LayoutPreviewModel::setSafeAreaPixels(const math::FVector4& insets) {
    _settings.safeAreaPixels = insets;
    _settings.name = L"Custom";
    _presetIndex = -1;
    normalize();
}

void LayoutPreviewModel::setSafeAreaVisible(bool visible) {
    _settings.showSafeArea = visible;
}

math::FVector2 LayoutPreviewModel::logicalSize(
    const math::FVector2& documentSize) const {
    if (_settings.followDocumentSize) return documentSize;
    const float scale = std::max(0.25f, _settings.dpiScale);
    return {_settings.pixelWidth / scale, _settings.pixelHeight / scale};
}

math::FVector4 LayoutPreviewModel::logicalSafeArea() const {
    const float scale = std::max(0.25f, _settings.dpiScale);
    return {_settings.safeAreaPixels.x / scale,
            _settings.safeAreaPixels.y / scale,
            _settings.safeAreaPixels.z / scale,
            _settings.safeAreaPixels.w / scale};
}

void LayoutPreviewModel::normalize() {
    if (!std::isfinite(_settings.pixelWidth) || _settings.pixelWidth < 1.0f)
        _settings.pixelWidth = 1.0f;
    if (!std::isfinite(_settings.pixelHeight) || _settings.pixelHeight < 1.0f)
        _settings.pixelHeight = 1.0f;
    if (!std::isfinite(_settings.dpiScale) || _settings.dpiScale < 0.25f)
        _settings.dpiScale = 1.0f;
    auto clampInset = [](float value) {
        return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
    };
    _settings.safeAreaPixels.x = clampInset(_settings.safeAreaPixels.x);
    _settings.safeAreaPixels.y = clampInset(_settings.safeAreaPixels.y);
    _settings.safeAreaPixels.z = clampInset(_settings.safeAreaPixels.z);
    _settings.safeAreaPixels.w = clampInset(_settings.safeAreaPixels.w);
    _settings.safeAreaPixels.x = std::min(
        _settings.safeAreaPixels.x, _settings.pixelWidth);
    _settings.safeAreaPixels.z = std::min(
        _settings.safeAreaPixels.z,
        std::max(0.0f, _settings.pixelWidth - _settings.safeAreaPixels.x));
    _settings.safeAreaPixels.y = std::min(
        _settings.safeAreaPixels.y, _settings.pixelHeight);
    _settings.safeAreaPixels.w = std::min(
        _settings.safeAreaPixels.w,
        std::max(0.0f, _settings.pixelHeight - _settings.safeAreaPixels.y));
}

} // namespace ayt::ui
