#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>

namespace ayt::ui {

// Shared viewport-to-pool mapping for fixed-extent virtualized collections.
// It deliberately owns no widgets or data model: ListView, TreeView and
// future Repeater-style controls retain their own binding and interaction
// semantics while using identical range/clamping math.
struct VirtualListWindow {
    int firstIndex = 0;
    int poolSize = 0;
    float leadingOffset = 0.0f;
};

inline VirtualListWindow computeVirtualListWindow(
    std::size_t itemCount,
    float viewportExtent,
    float itemExtent,
    float scrollOffset,
    int overscanRows = 2) noexcept
{
    VirtualListWindow result;
    if (itemCount == 0u || itemExtent <= 0.0f) return result;

    const std::size_t boundedCount = std::min(
        itemCount,
        static_cast<std::size_t>(std::numeric_limits<int>::max()));
    const int count = static_cast<int>(boundedCount);
    const float viewport = std::max(0.0f, viewportExtent);
    const int visibleRows = std::max(
        1, static_cast<int>(viewport / itemExtent) + 1);
    result.poolSize = std::min(
        count, visibleRows + std::max(0, overscanRows));

    const float clampedOffset = std::max(0.0f, scrollOffset);
    const int rawFirst = static_cast<int>(clampedOffset / itemExtent);
    const int maxFirst = std::max(0, count - result.poolSize);
    result.firstIndex = std::clamp(rawFirst, 0, maxFirst);
    result.leadingOffset = clampedOffset
        - static_cast<float>(result.firstIndex) * itemExtent;
    return result;
}

} // namespace ayt::ui
