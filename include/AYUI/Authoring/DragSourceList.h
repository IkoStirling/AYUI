#pragma once
#include "AYUI/ListView.h"
#include "AYUI/DragDrop.h"
#include <cstdint>
#include <functional>

namespace ayt::ui::authoring {
/** @brief Single-selection authoring list with host-defined opaque drag payloads.
 * Keeps ListView rendering/keyboard/scrollbars. Payload is re-resolved when the
 * pointer crosses the threshold, not retained across owner mutations. Hosts must
 * cancelPendingDrag before replacing rows through a base ListView reference.
 * Normal typed setItems and provider changes cancel the old gesture/session.
 */
class DragSourceList : public ListView {
public:
    using PayloadProvider = std::function<DragPayload(int)>;
    DragSourceList();
    void setPayloadProvider(PayloadProvider provider);
    void setDragThreshold(float pixels);
    void setItems(const std::vector<std::wstring>& items);
    void cancelPendingDrag();
    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseButtonDown(const UIMouseEvent& event) override;
    bool onMouseMove(const UIMouseEvent& event) override;
    bool onMouseButtonUp(const UIMouseEvent& event) override;
    void onCaptureCancelled() override;
private:
    void clearGesture();
    PayloadProvider _payloadProvider;
    math::FVector2 _pressPoint{};
    int _pressedRow = -1;
    float _threshold = 5;
    bool _dragging = false;
    std::uint64_t _gestureEpoch = 0;
};
} // namespace ayt::ui::authoring
