#pragma once
#include "AYUI/Thickness.h"
#include "AYMath/MathTypes.h"
#include <string>

namespace ayt::ui {

// Shared text rendering payload for Button / TextLabel / CheckBox / TextInput /
// TabItem / MenuItem etc. Adopted incrementally — R-1..R-3 keep existing
// widgets on their discrete fields to minimize diff; C-2/C-3 widgets will use
// this struct directly.
struct TextContent {
    std::wstring text;
    std::wstring fontFamily;
    int fontSize = 14;
    math::FVector4 color{1.0f, 1.0f, 1.0f, 1.0f};

    enum class HAlign { Left, Center, Right };
    enum class VAlign { Top, Center, Bottom };

    HAlign hAlign = HAlign::Left;
    VAlign vAlign = VAlign::Top;
    Thickness padding;
    bool wordWrap = false;
    float wrapWidth = 0.0f;
};

} // namespace ayt::ui