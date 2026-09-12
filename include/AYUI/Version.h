#pragma once

#include <cstdint>

#ifndef AYUI_SOURCE_ABI_VERSION
#define AYUI_SOURCE_ABI_VERSION 123
#endif

static_assert(AYUI_SOURCE_ABI_VERSION == 123,
              "AYUI headers and target disagree; perform a full rebuild.");

#define AYUI_STRINGIZE_IMPL(value) #value
#define AYUI_STRINGIZE(value) AYUI_STRINGIZE_IMPL(value)
#if defined(_MSC_VER)
#pragma detect_mismatch("AYUI.SourceABI", AYUI_STRINGIZE(AYUI_SOURCE_ABI_VERSION))
#endif

namespace ayt::ui {

inline constexpr std::uint32_t kUiSourceAbiVersion = AYUI_SOURCE_ABI_VERSION;
inline constexpr const char* kUiVersion = "1.1.0";

} // namespace ayt::ui

#undef AYUI_STRINGIZE
#undef AYUI_STRINGIZE_IMPL
