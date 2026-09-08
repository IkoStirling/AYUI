#pragma once

#include "AYUI/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class Button;
class ComboBox;
class TextInput;

struct ColorPaletteBank {
    std::wstring name;
    std::vector<math::FVector4> colors;
};

// Engine-wide color authoring control. The control deliberately owns no file
// IO: hosts can persist paletteBanks() wherever project/user preferences live.
// It provides an HSV field, hue and alpha strips, hexadecimal input, named
// swatch banks, and an eyedropper request hook.
class ColorPicker final : public CompoundWidget {
public:
    ColorPicker();
    ~ColorPicker() override;

    void setColor(const math::FVector4& color, bool notify = true);
    [[nodiscard]] const math::FVector4& color() const noexcept {
        return _color;
    }

    void setPaletteBanks(std::vector<ColorPaletteBank> banks);
    [[nodiscard]] const std::vector<ColorPaletteBank>& paletteBanks() const {
        return _banks;
    }
    bool addPaletteBank(std::wstring name,
                        std::vector<math::FVector4> colors = {});
    bool removePaletteBank(size_t index);
    void setActivePalette(size_t index);
    [[nodiscard]] size_t activePalette() const noexcept {
        return _activeBank;
    }
    bool rememberColor(size_t slot);
    void rememberColor();

    void sampleColor(const math::FVector4& sampled) {
        setColor(sampled, true);
        if (_onColorCommitted) _onColorCommitted(_color);
    }
    void setOnColorChanged(std::function<void(const math::FVector4&)> cb) {
        _onColorChanged = std::move(cb);
    }
    void setOnPaletteChanged(
        std::function<void(const std::vector<ColorPaletteBank>&)> cb) {
        _onPaletteChanged = std::move(cb);
    }
    void setOnEyedropperRequested(std::function<void()> cb) {
        _onEyedropperRequested = std::move(cb);
    }
    void setOnColorInteractionStarted(std::function<void()> cb) {
        _onColorInteractionStarted = std::move(cb);
    }
    void setOnColorCommitted(
        std::function<void(const math::FVector4&)> cb) {
        _onColorCommitted = std::move(cb);
    }

    [[nodiscard]] std::wstring hexCode() const;
    bool setHexCode(const std::wstring& code, bool notify = true);

    static math::FVector4 hsvToRgb(float hueDegrees, float saturation,
                                   float value, float alpha = 1.0f);
    static void rgbToHsv(const math::FVector4& color, float& hueDegrees,
                         float& saturation, float& value);
    static bool parseHexCode(const std::wstring& code,
                             math::FVector4& color);
    static std::wstring formatHexCode(const math::FVector4& color);

    void layoutChildren() override;
    bool onMouseButtonDown(const UIMouseEvent& event) override;
    bool onMouseMove(const UIMouseEvent& event) override;
    bool onMouseButtonUp(const UIMouseEvent& event) override;
    UiCursorHint getCursorHint() const override;

protected:
    void onRender(IRenderBackend& renderer) override;

private:
    enum class DragTarget { None, SaturationValue, Hue, Alpha };

    math::FRectangle saturationValueRect() const;
    math::FRectangle hueRect() const;
    math::FRectangle alphaRect() const;
    math::FRectangle swatchRect(size_t index) const;
    size_t visibleSwatchCount() const;
    void updateFromPointer(const math::FVector2& point);
    void updateChildText();
    void rebuildPaletteNames();
    void emitPaletteChanged();
    void destroyOwnedChild(Widget* child);

    math::FVector4 _color{0.20f, 0.55f, 0.95f, 1.0f};
    float _hue = 212.0f;
    float _saturation = 0.79f;
    float _value = 0.95f;
    DragTarget _drag = DragTarget::None;
    bool _syncingText = false;
    size_t _activeBank = 0u;
    std::vector<ColorPaletteBank> _banks;

    ComboBox* _paletteChooser = nullptr;
    TextInput* _hexInput = nullptr;
    Button* _eyedropperButton = nullptr;
    Button* _rememberButton = nullptr;

    std::function<void(const math::FVector4&)> _onColorChanged;
    std::function<void(const std::vector<ColorPaletteBank>&)>
        _onPaletteChanged;
    std::function<void()> _onEyedropperRequested;
    std::function<void()> _onColorInteractionStarted;
    std::function<void(const math::FVector4&)> _onColorCommitted;
};

Widget* createColorPickerWidget();

} // namespace ayt::ui
