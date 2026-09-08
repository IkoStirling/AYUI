#include "AYUI/ColorPicker.h"

#include "AYUI/Button.h"
#include "AYUI/ComboBox.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Style.h"
#include "AYUI/TextInput.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace ayt::ui {
namespace {

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

bool nearColor(const math::FVector4& a, const math::FVector4& b)
{
    constexpr float epsilon = 1.0f / 510.0f;
    return std::abs(a.x - b.x) <= epsilon
        && std::abs(a.y - b.y) <= epsilon
        && std::abs(a.z - b.z) <= epsilon
        && std::abs(a.w - b.w) <= epsilon;
}

math::FVector4 checkerColor(int x, int y)
{
    return ((x + y) & 1) == 0
        ? math::FVector4{0.28f, 0.29f, 0.32f, 1.0f}
        : math::FVector4{0.16f, 0.17f, 0.19f, 1.0f};
}

} // namespace

ColorPicker::ColorPicker()
{
    setSize({260.0f, 330.0f});
    setDisplayListPolicy(DisplayListPolicy::Immediate);

    _paletteChooser = new ComboBox();
    _paletteChooser->setLayoutPositionManaged(false);
    _paletteChooser->setLayoutSizeManaged(false);
    _paletteChooser->setStyleId("color_picker_palette");
    _paletteChooser->setOnSelectionChanged([this](int index) {
        if (index >= 0) {
            setActivePalette(static_cast<size_t>(index));
        }
    });
    addChildExternal(_paletteChooser);

    _hexInput = new TextInput();
    _hexInput->setLayoutPositionManaged(false);
    _hexInput->setLayoutSizeManaged(false);
    _hexInput->setStyleId("color_picker_input");
    _hexInput->setOnTextChanged([this](const std::wstring& text) {
        if (!_syncingText) {
            if (setHexCode(text, true) && _onColorCommitted) {
                _onColorCommitted(_color);
            }
        }
    });
    addChildExternal(_hexInput);

    _eyedropperButton = new Button();
    _eyedropperButton->setText(L"Pick");
    _eyedropperButton->setLayoutPositionManaged(false);
    _eyedropperButton->setLayoutSizeManaged(false);
    _eyedropperButton->setStyleId("color_picker_button");
    _eyedropperButton->setOnClicked([this]() {
        if (_onEyedropperRequested) {
            _onEyedropperRequested();
        }
    });
    addChildExternal(_eyedropperButton);

    _rememberButton = new Button();
    _rememberButton->setText(L"Save");
    _rememberButton->setLayoutPositionManaged(false);
    _rememberButton->setLayoutSizeManaged(false);
    _rememberButton->setStyleId("color_picker_button");
    _rememberButton->setOnClicked([this]() { rememberColor(); });
    addChildExternal(_rememberButton);

    setPaletteBanks({
        {L"Recent", {
            {0.95f, 0.95f, 0.95f, 1.0f},
            {0.12f, 0.13f, 0.15f, 1.0f},
            {0.91f, 0.25f, 0.22f, 1.0f},
            {0.98f, 0.66f, 0.16f, 1.0f},
            {0.24f, 0.72f, 0.40f, 1.0f},
            {0.20f, 0.55f, 0.95f, 1.0f},
            {0.58f, 0.35f, 0.92f, 1.0f}}},
        {L"Warm", {
            {0.35f, 0.09f, 0.08f, 1.0f},
            {0.66f, 0.17f, 0.12f, 1.0f},
            {0.95f, 0.38f, 0.16f, 1.0f},
            {1.00f, 0.72f, 0.25f, 1.0f},
            {0.98f, 0.88f, 0.62f, 1.0f}}},
        {L"Cool", {
            {0.05f, 0.18f, 0.27f, 1.0f},
            {0.08f, 0.40f, 0.52f, 1.0f},
            {0.15f, 0.67f, 0.70f, 1.0f},
            {0.28f, 0.50f, 0.92f, 1.0f},
            {0.52f, 0.35f, 0.88f, 1.0f}}},
    });
    setColor(_color, false);
}

ColorPicker::~ColorPicker()
{
    destroyOwnedChild(_paletteChooser);
    destroyOwnedChild(_hexInput);
    destroyOwnedChild(_eyedropperButton);
    destroyOwnedChild(_rememberButton);
    _paletteChooser = nullptr;
    _hexInput = nullptr;
    _eyedropperButton = nullptr;
    _rememberButton = nullptr;
}

void ColorPicker::destroyOwnedChild(Widget* child)
{
    if (child == nullptr) {
        return;
    }
    if (child->getParent() == this) {
        removeChild(child);
    }
    delete child;
}

math::FVector4 ColorPicker::hsvToRgb(float hueDegrees, float saturation,
                                     float value, float alpha)
{
    float hue = std::fmod(hueDegrees, 360.0f);
    if (hue < 0.0f) {
        hue += 360.0f;
    }
    const float s = clamp01(saturation);
    const float v = clamp01(value);
    const float chroma = v * s;
    const float sector = hue / 60.0f;
    const float x = chroma * (1.0f - std::abs(
        std::fmod(sector, 2.0f) - 1.0f));
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (sector < 1.0f) {
        r = chroma; g = x;
    } else if (sector < 2.0f) {
        r = x; g = chroma;
    } else if (sector < 3.0f) {
        g = chroma; b = x;
    } else if (sector < 4.0f) {
        g = x; b = chroma;
    } else if (sector < 5.0f) {
        r = x; b = chroma;
    } else {
        r = chroma; b = x;
    }
    const float match = v - chroma;
    return {r + match, g + match, b + match, clamp01(alpha)};
}

void ColorPicker::rgbToHsv(const math::FVector4& color, float& hueDegrees,
                           float& saturation, float& value)
{
    const float r = clamp01(color.x);
    const float g = clamp01(color.y);
    const float b = clamp01(color.z);
    const float maximum = std::max({r, g, b});
    const float minimum = std::min({r, g, b});
    const float delta = maximum - minimum;
    value = maximum;
    saturation = maximum <= 0.0f ? 0.0f : delta / maximum;
    if (delta <= 1e-6f) {
        hueDegrees = 0.0f;
    } else if (maximum == r) {
        hueDegrees = 60.0f * std::fmod((g - b) / delta, 6.0f);
    } else if (maximum == g) {
        hueDegrees = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        hueDegrees = 60.0f * (((r - g) / delta) + 4.0f);
    }
    if (hueDegrees < 0.0f) {
        hueDegrees += 360.0f;
    }
}

bool ColorPicker::parseHexCode(const std::wstring& code,
                               math::FVector4& color)
{
    std::wstring digits;
    digits.reserve(code.size());
    for (wchar_t ch : code) {
        if (ch == L'#' || std::iswspace(ch)) {
            continue;
        }
        if (!std::iswxdigit(ch)) {
            return false;
        }
        digits.push_back(ch);
    }
    if (digits.size() != 6u && digits.size() != 8u) {
        return false;
    }
    try {
        const unsigned long packed = std::stoul(digits, nullptr, 16);
        const bool hasAlpha = digits.size() == 8u;
        const unsigned long rgb = hasAlpha ? packed >> 8u : packed;
        const unsigned long alpha = hasAlpha ? packed & 0xffu : 0xffu;
        color = {
            static_cast<float>((rgb >> 16u) & 0xffu) / 255.0f,
            static_cast<float>((rgb >> 8u) & 0xffu) / 255.0f,
            static_cast<float>(rgb & 0xffu) / 255.0f,
            static_cast<float>(alpha) / 255.0f};
        return true;
    } catch (...) {
        return false;
    }
}

std::wstring ColorPicker::formatHexCode(const math::FVector4& color)
{
    const auto channel = [](float value) {
        return static_cast<unsigned int>(std::round(clamp01(value) * 255.0f));
    };
    std::wostringstream out;
    out << L'#' << std::uppercase << std::hex << std::setfill(L'0')
        << std::setw(2) << channel(color.x)
        << std::setw(2) << channel(color.y)
        << std::setw(2) << channel(color.z)
        << std::setw(2) << channel(color.w);
    return out.str();
}

std::wstring ColorPicker::hexCode() const
{
    return formatHexCode(_color);
}

void ColorPicker::setColor(const math::FVector4& color, bool notify)
{
    const math::FVector4 clamped{
        clamp01(color.x), clamp01(color.y), clamp01(color.z),
        clamp01(color.w)};
    if (nearColor(_color, clamped)) {
        updateChildText();
        return;
    }
    _color = clamped;
    rgbToHsv(_color, _hue, _saturation, _value);
    updateChildText();
    markDirty();
    if (notify && _onColorChanged) {
        _onColorChanged(_color);
    }
}

bool ColorPicker::setHexCode(const std::wstring& code, bool notify)
{
    math::FVector4 parsed;
    if (!parseHexCode(code, parsed)) {
        return false;
    }
    setColor(parsed, notify);
    return true;
}

void ColorPicker::setPaletteBanks(std::vector<ColorPaletteBank> banks)
{
    banks.erase(std::remove_if(banks.begin(), banks.end(),
        [](const ColorPaletteBank& bank) { return bank.name.empty(); }),
        banks.end());
    if (banks.empty()) {
        banks.push_back({L"Recent", {}});
    }
    for (ColorPaletteBank& bank : banks) {
        if (bank.colors.size() > 16u) {
            bank.colors.resize(16u);
        }
        for (math::FVector4& color : bank.colors) {
            color = {clamp01(color.x), clamp01(color.y), clamp01(color.z),
                     clamp01(color.w)};
        }
    }
    _banks = std::move(banks);
    _activeBank = std::min(_activeBank, _banks.size() - 1u);
    rebuildPaletteNames();
    markDirty();
}

bool ColorPicker::addPaletteBank(std::wstring name,
                                 std::vector<math::FVector4> colors)
{
    if (name.empty()) {
        return false;
    }
    const auto duplicate = std::find_if(_banks.begin(), _banks.end(),
        [&name](const ColorPaletteBank& bank) { return bank.name == name; });
    if (duplicate != _banks.end()) {
        return false;
    }
    if (colors.size() > 16u) {
        colors.resize(16u);
    }
    _banks.push_back({std::move(name), std::move(colors)});
    rebuildPaletteNames();
    setActivePalette(_banks.size() - 1u);
    emitPaletteChanged();
    return true;
}

bool ColorPicker::removePaletteBank(size_t index)
{
    if (_banks.size() <= 1u || index >= _banks.size()) {
        return false;
    }
    _banks.erase(_banks.begin() + static_cast<std::ptrdiff_t>(index));
    _activeBank = std::min(_activeBank, _banks.size() - 1u);
    rebuildPaletteNames();
    emitPaletteChanged();
    markDirty();
    return true;
}

void ColorPicker::setActivePalette(size_t index)
{
    if (index >= _banks.size() || _activeBank == index) {
        return;
    }
    _activeBank = index;
    if (_paletteChooser != nullptr
        && _paletteChooser->getSelectedIndex() != static_cast<int>(index)) {
        _paletteChooser->setSelectedIndex(static_cast<int>(index));
    }
    markDirty();
}

bool ColorPicker::rememberColor(size_t slot)
{
    if (_banks.empty() || slot >= 16u) {
        return false;
    }
    std::vector<math::FVector4>& colors = _banks[_activeBank].colors;
    if (colors.size() <= slot) {
        colors.resize(slot + 1u, _color);
        emitPaletteChanged();
        markDirty();
        return true;
    }
    if (nearColor(colors[slot], _color)) {
        return false;
    }
    colors[slot] = _color;
    emitPaletteChanged();
    markDirty();
    return true;
}

void ColorPicker::rememberColor()
{
    if (_banks.empty()) {
        return;
    }
    std::vector<math::FVector4>& colors = _banks[_activeBank].colors;
    const auto existing = std::find_if(colors.begin(), colors.end(),
        [this](const math::FVector4& color) { return nearColor(color, _color); });
    if (existing != colors.end()) {
        return;
    }
    if (colors.size() >= 16u) {
        colors.erase(colors.begin());
    }
    colors.push_back(_color);
    emitPaletteChanged();
    markDirty();
}

void ColorPicker::emitPaletteChanged()
{
    if (_onPaletteChanged) {
        _onPaletteChanged(_banks);
    }
}

void ColorPicker::rebuildPaletteNames()
{
    if (_paletteChooser == nullptr) {
        return;
    }
    std::vector<std::wstring> names;
    names.reserve(_banks.size());
    for (const ColorPaletteBank& bank : _banks) {
        names.push_back(bank.name);
    }
    _paletteChooser->setItems(names);
    _paletteChooser->setSelectedIndex(static_cast<int>(_activeBank));
}

void ColorPicker::updateChildText()
{
    if (_hexInput == nullptr) {
        return;
    }
    const std::wstring next = hexCode();
    if (_hexInput->getText() == next) {
        return;
    }
    _syncingText = true;
    _hexInput->setText(next);
    _syncingText = false;
}

void ColorPicker::layoutChildren()
{
    const float width = std::max(80.0f, getSize().x);
    if (_paletteChooser != nullptr) {
        _paletteChooser->setPosition({8.0f, 8.0f});
        _paletteChooser->setSize({width - 16.0f, 26.0f});
    }
    const float inputWidth = std::max(72.0f, width - 116.0f);
    if (_hexInput != nullptr) {
        _hexInput->setPosition({8.0f, 224.0f});
        _hexInput->setSize({inputWidth, 28.0f});
    }
    if (_eyedropperButton != nullptr) {
        _eyedropperButton->setPosition({12.0f + inputWidth, 224.0f});
        _eyedropperButton->setSize({44.0f, 28.0f});
    }
    if (_rememberButton != nullptr) {
        _rememberButton->setPosition({60.0f + inputWidth, 224.0f});
        _rememberButton->setSize({48.0f, 28.0f});
    }
}

math::FRectangle ColorPicker::saturationValueRect() const
{
    const math::FRectangle b = getWorldBounds();
    return {b.minX + 8.0f, b.minY + 42.0f,
            b.maxX - 8.0f, b.minY + 170.0f};
}

math::FRectangle ColorPicker::hueRect() const
{
    const math::FRectangle b = getWorldBounds();
    return {b.minX + 8.0f, b.minY + 180.0f,
            b.maxX - 8.0f, b.minY + 194.0f};
}

math::FRectangle ColorPicker::alphaRect() const
{
    const math::FRectangle b = getWorldBounds();
    return {b.minX + 8.0f, b.minY + 202.0f,
            b.maxX - 8.0f, b.minY + 216.0f};
}

size_t ColorPicker::visibleSwatchCount() const
{
    return _banks.empty() ? 0u
        : std::min<size_t>(16u, _banks[_activeBank].colors.size());
}

math::FRectangle ColorPicker::swatchRect(size_t index) const
{
    const math::FRectangle b = getWorldBounds();
    const size_t columns = static_cast<size_t>(std::max(
        1.0f, std::floor((getSize().x - 12.0f) / 24.0f)));
    const float x = b.minX + 8.0f + static_cast<float>(index % columns) * 24.0f;
    const float y = b.minY + 264.0f + static_cast<float>(index / columns) * 24.0f;
    return {x, y, x + 20.0f, y + 20.0f};
}

void ColorPicker::updateFromPointer(const math::FVector2& point)
{
    if (_drag == DragTarget::None) {
        return;
    }
    const math::FRectangle rect = _drag == DragTarget::SaturationValue
        ? saturationValueRect()
        : (_drag == DragTarget::Hue ? hueRect() : alphaRect());
    const float width = std::max(1.0f, rect.width());
    const float height = std::max(1.0f, rect.height());
    if (_drag == DragTarget::SaturationValue) {
        _saturation = clamp01((point.x - rect.minX) / width);
        _value = 1.0f - clamp01((point.y - rect.minY) / height);
    } else if (_drag == DragTarget::Hue) {
        _hue = clamp01((point.x - rect.minX) / width) * 360.0f;
    } else {
        _color.w = clamp01((point.x - rect.minX) / width);
    }
    const float alpha = _color.w;
    _color = hsvToRgb(_hue, _saturation, _value, alpha);
    updateChildText();
    markDirty();
    if (_onColorChanged) {
        _onColorChanged(_color);
    }
}

bool ColorPicker::onMouseButtonDown(const UIMouseEvent& event)
{
    if (event.mouseButton == 0) {
        if (saturationValueRect().contains(event.mousePos)) {
            _drag = DragTarget::SaturationValue;
        } else if (hueRect().contains(event.mousePos)) {
            _drag = DragTarget::Hue;
        } else if (alphaRect().contains(event.mousePos)) {
            _drag = DragTarget::Alpha;
        }
        if (_drag != DragTarget::None) {
            if (_onColorInteractionStarted) {
                _onColorInteractionStarted();
            }
            updateFromPointer(event.mousePos);
            return true;
        }
    }
    for (size_t i = 0; i < visibleSwatchCount(); ++i) {
        if (!swatchRect(i).contains(event.mousePos)) {
            continue;
        }
        if (event.mouseButton == 1) {
            rememberColor(i);
        } else if (event.mouseButton == 0) {
            setColor(_banks[_activeBank].colors[i], true);
            if (_onColorCommitted) {
                _onColorCommitted(_color);
            }
        }
        return true;
    }
    return false;
}

bool ColorPicker::onMouseMove(const UIMouseEvent& event)
{
    if (_drag == DragTarget::None) {
        return false;
    }
    updateFromPointer(event.mousePos);
    return true;
}

bool ColorPicker::onMouseButtonUp(const UIMouseEvent& event)
{
    if (event.mouseButton != 0 || _drag == DragTarget::None) {
        return false;
    }
    updateFromPointer(event.mousePos);
    _drag = DragTarget::None;
    if (_onColorCommitted) {
        _onColorCommitted(_color);
    }
    return true;
}

UiCursorHint ColorPicker::getCursorHint() const
{
    return _drag == DragTarget::None ? UiCursorHint::Default
                                     : UiCursorHint::Hand;
}

void ColorPicker::onRender(IRenderBackend& renderer)
{
    const math::FRectangle bounds = getWorldBounds();
    renderer.drawRoundedRect(bounds, {0.055f, 0.060f, 0.072f, 1.0f}, 4.0f);
    renderer.drawBorderRect(bounds, {0.20f, 0.23f, 0.28f, 1.0f}, 1.0f, 4.0f);

    const math::FRectangle sv = saturationValueRect();
    constexpr int xSteps = 24;
    constexpr int ySteps = 12;
    for (int y = 0; y < ySteps; ++y) {
        for (int x = 0; x < xSteps; ++x) {
            const float s = static_cast<float>(x) / (xSteps - 1);
            const float v = 1.0f - static_cast<float>(y) / (ySteps - 1);
            const float x0 = sv.minX + sv.width() * x / xSteps;
            const float x1 = sv.minX + sv.width() * (x + 1) / xSteps;
            const float y0 = sv.minY + sv.height() * y / ySteps;
            const float y1 = sv.minY + sv.height() * (y + 1) / ySteps;
            renderer.drawRect({x0, y0, x1 + 0.5f, y1 + 0.5f},
                              hsvToRgb(_hue, s, v));
        }
    }
    const float markerX = sv.minX + _saturation * sv.width();
    const float markerY = sv.minY + (1.0f - _value) * sv.height();
    renderer.drawBorderRect({markerX - 4.0f, markerY - 4.0f,
                             markerX + 4.0f, markerY + 4.0f},
                            {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f, 4.0f);

    const math::FRectangle hue = hueRect();
    constexpr int hueSteps = 24;
    for (int i = 0; i < hueSteps; ++i) {
        const float x0 = hue.minX + hue.width() * i / hueSteps;
        const float x1 = hue.minX + hue.width() * (i + 1) / hueSteps;
        renderer.drawRect({x0, hue.minY, x1 + 0.5f, hue.maxY},
                          hsvToRgb(360.0f * i / (hueSteps - 1), 1.0f, 1.0f));
    }
    const float hueX = hue.minX + (_hue / 360.0f) * hue.width();
    renderer.drawBorderRect({hueX - 2.0f, hue.minY - 2.0f,
                             hueX + 2.0f, hue.maxY + 2.0f},
                            {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);

    const math::FRectangle alpha = alphaRect();
    constexpr int alphaSteps = 12;
    for (int i = 0; i < alphaSteps; ++i) {
        const float x0 = alpha.minX + alpha.width() * i / alphaSteps;
        const float x1 = alpha.minX + alpha.width() * (i + 1) / alphaSteps;
        renderer.drawRect({x0, alpha.minY, x1 + 0.5f, alpha.maxY},
                          checkerColor(i, 0));
        math::FVector4 overlay = hsvToRgb(_hue, _saturation, _value,
            static_cast<float>(i) / (alphaSteps - 1));
        renderer.drawRect({x0, alpha.minY, x1 + 0.5f, alpha.maxY}, overlay);
    }
    const float alphaX = alpha.minX + _color.w * alpha.width();
    renderer.drawBorderRect({alphaX - 2.0f, alpha.minY - 2.0f,
                             alphaX + 2.0f, alpha.maxY + 2.0f},
                            {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);

    for (size_t i = 0; i < visibleSwatchCount(); ++i) {
        const math::FRectangle rect = swatchRect(i);
        renderer.drawRoundedRect(rect, _banks[_activeBank].colors[i], 3.0f);
        renderer.drawBorderRect(rect, {0.45f, 0.48f, 0.54f, 1.0f},
                                1.0f, 3.0f);
    }
}

Widget* createColorPickerWidget()
{
    return new ColorPicker();
}

} // namespace ayt::ui
