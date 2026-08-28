#pragma once

#include "AYUI/Accessibility.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ayt::ui {

class UIManager;

enum class AccessibilityChangeKind : uint8_t {
    Tree,
    Name,
    Description,
    Value,
    Bounds,
    State,
    Focus,
};

struct AccessibilityChange {
    AccessibilityChangeKind kind = AccessibilityChangeKind::Tree;
    uint64_t nodeId = 0;
};

// Host bridge for the platform-neutral AYUI semantic tree. update() must be
// called on the UI thread after layout. On Windows, handleNativeMessage()
// serves WM_GETOBJECT and marshals UIA actions back to that same thread.
class AccessibilityAdapter {
public:
    virtual ~AccessibilityAdapter() = default;

    virtual void update() = 0;
    virtual const AccessibilityNode& snapshot() const = 0;
    virtual const std::vector<AccessibilityChange>& changes() const = 0;

    virtual bool handleNativeMessage(uint32_t message,
                                     uintptr_t wParam,
                                     intptr_t lParam,
                                     intptr_t& result) = 0;
};

// nativeWindow is HWND on Windows and the host window object on other ports.
// A null window still creates the snapshot/diff adapter, which is useful for
// headless hosts and tests; native platform exposure is simply disabled.
std::unique_ptr<AccessibilityAdapter> createNativeAccessibilityAdapter(
    UIManager& manager, void* nativeWindow = nullptr);

} // namespace ayt::ui
