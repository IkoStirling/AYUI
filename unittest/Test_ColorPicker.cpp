#include "AYTest.h"
#include "AYUI/ColorPicker.h"
#include "AYUI/WidgetSerializer.h"

using namespace ayt::math;
using namespace ayt::ui;

TEST_SUITE(AYUI_ColorPicker)

TEST_CASE(ColorPickerHsvRoundTrip)
{
    const FVector4 color = ColorPicker::hsvToRgb(210.0f, 0.75f, 0.80f, 0.5f);
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
    ColorPicker::rgbToHsv(color, hue, saturation, value);
    CHECK_FLOAT_EQ(hue, 210.0f, 0.01f);
    CHECK_FLOAT_EQ(saturation, 0.75f, 0.001f);
    CHECK_FLOAT_EQ(value, 0.80f, 0.001f);
    CHECK_FLOAT_EQ(color.w, 0.5f, 0.001f);
}

TEST_CASE(ColorPickerAcceptsRgbAndRgbaHex)
{
    FVector4 color;
    CHECK_TRUE(ColorPicker::parseHexCode(L"#3366CC", color));
    CHECK_FLOAT_EQ(color.x, 0.2f, 0.001f);
    CHECK_FLOAT_EQ(color.y, 0.4f, 0.001f);
    CHECK_FLOAT_EQ(color.z, 0.8f, 0.001f);
    CHECK_FLOAT_EQ(color.w, 1.0f, 0.001f);
    CHECK_TRUE(ColorPicker::parseHexCode(L"3366CC80", color));
    CHECK_FLOAT_EQ(color.w, 128.0f / 255.0f, 0.001f);
    CHECK_FALSE(ColorPicker::parseHexCode(L"#12GG00", color));
}

TEST_CASE(ColorPickerNamedBanksRememberColors)
{
    ColorPicker picker;
    picker.setPaletteBanks({{L"Project", {}}, {L"Recent", {}}});
    picker.setActivePalette(1u);
    picker.setHexCode(L"#804020FF");
    picker.rememberColor();
    CHECK_INT_EQ(picker.activePalette(), 1u);
    CHECK_INT_EQ(picker.paletteBanks()[1].colors.size(), 1u);
    CHECK_TRUE(ColorPicker::formatHexCode(
        picker.paletteBanks()[1].colors[0]) == L"#804020FF");
}

TEST_CASE(ColorPickerRememberSlotReportsNewSwatch)
{
    ColorPicker picker;
    picker.setPaletteBanks({{L"Project", {}}});
    int changes = 0;
    picker.setOnPaletteChanged(
        [&](const std::vector<ColorPaletteBank>&) { ++changes; });
    picker.setHexCode(L"#2468ACFF");
    CHECK_TRUE(picker.rememberColor(3u));
    CHECK_INT_EQ(changes, 1);
    CHECK_INT_EQ(picker.paletteBanks()[0].colors.size(), 4u);
    CHECK_TRUE(ColorPicker::formatHexCode(
        picker.paletteBanks()[0].colors[3]) == L"#2468ACFF");
}

TEST_CASE(ColorPickerEyedropperHookAcceptsSample)
{
    ColorPicker picker;
    int changes = 0;
    picker.setOnColorChanged([&](const FVector4&) { ++changes; });
    picker.sampleColor({0.1f, 0.2f, 0.3f, 1.0f});
    CHECK_INT_EQ(changes, 1);
    CHECK_TRUE(picker.hexCode() == L"#1A334DFF");
}

TEST_CASE(ColorPickerSerializerKeepsTypeColorAndBanks)
{
    ColorPicker picker;
    picker.setId("picker");
    picker.setPaletteBanks({
        {L"Project", {{0.1f, 0.2f, 0.3f, 0.4f}}},
        {L"Warm", {{0.8f, 0.3f, 0.1f, 1.0f}}}});
    picker.setActivePalette(1u);
    picker.setHexCode(L"#336699CC", false);

    const std::string json = WidgetSerializer::serializeWidget(&picker);
    Widget* restoredBase = WidgetSerializer::deserialize(json);
    auto* restored = dynamic_cast<ColorPicker*>(restoredBase);
    CHECK_NOT_NULL(restored);
    if (restored != nullptr) {
        CHECK_TRUE(restored->hexCode() == L"#336699CC");
        CHECK_INT_EQ(restored->activePalette(), 1u);
        CHECK_INT_EQ(restored->paletteBanks().size(), 2u);
        CHECK_TRUE(restored->paletteBanks()[0].name == L"Project");
        CHECK_INT_EQ(restored->paletteBanks()[0].colors.size(), 1u);
    }
    destroyWidgetTree(restoredBase);
}

TEST_SUITE_END
