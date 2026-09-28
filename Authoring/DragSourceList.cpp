#include "AYUI/Authoring/DragSourceList.h"
#include "AYUI/UIManager.h"
#include <algorithm>
#include <cmath>

namespace ayt::ui::authoring {
DragSourceList::DragSourceList() {
    setDraggable(true);
    setOnDragEnd([this](bool) { clearGesture(); });
}
void DragSourceList::setPayloadProvider(PayloadProvider provider) {
    cancelPendingDrag();
    _payloadProvider = std::move(provider);
}
void DragSourceList::setDragThreshold(float pixels) {
    if (std::isfinite(pixels) && pixels >= 0) _threshold = pixels;
}
void DragSourceList::setItems(const std::vector<std::wstring>& items) {
    cancelPendingDrag();
    ListView::setItems(items);
}
void DragSourceList::clearGesture() {
    ++_gestureEpoch;
    _pressedRow = -1;
    _dragging = false;
    setDragPayload({});
}
void DragSourceList::cancelPendingDrag() {
    if (auto* ui = UIManager::tryGet(); ui && ui->getDragSource() == this) ui->cancelDrag();
    clearGesture();
}
Widget* DragSourceList::hitTest(const math::FVector2& worldPos) {
    Widget* hit = ListView::hitTest(worldPos);
    return !hit || hit == getVerticalScrollBar() ? hit : this;
}
bool DragSourceList::onMouseButtonDown(const UIMouseEvent& event) {
    if (event.mouseButton != 0) return false;
    cancelPendingDrag();
    const auto bounds = getWorldBounds();
    if (!getClientRect().contains(event.mousePos)) return false;
    const float localY = event.mousePos.y - bounds.minY + getScrollOffset().y;
    const int row = static_cast<int>(std::floor(localY / (std::max)(1.0f, getItemHeight())));
    if (row < 0 || static_cast<std::size_t>(row) >= getItemCount()) return false;
    const auto epoch = _gestureEpoch;
    (void)ListView::onMouseButtonDown(event);
    setSelectedIndex(row);
    if (_gestureEpoch != epoch) return true;
    _pressPoint = event.mousePos;
    _pressedRow = row;
    return true;
}
bool DragSourceList::onMouseMove(const UIMouseEvent& event) {
    if (_pressedRow < 0 || _dragging) return false;
    const auto delta = event.mousePos - _pressPoint;
    if (delta.x * delta.x + delta.y * delta.y < _threshold * _threshold) return true;
    const auto epoch = _gestureEpoch;
    const auto payload = _payloadProvider ? _payloadProvider(_pressedRow) : DragPayload{};
    if (_gestureEpoch != epoch) return false;
    if (payload.isEmpty()) { clearGesture(); return false; }
    setDragPayload(payload);
    if (auto* ui = UIManager::tryGet(); ui && ui->beginDrag(this)) {
        _dragging = true;
        return true;
    }
    clearGesture();
    return false;
}
bool DragSourceList::onMouseButtonUp(const UIMouseEvent& event) {
    if (event.mouseButton != 0) return false;
    const bool handled = _pressedRow >= 0 || _dragging;
    if (!_dragging) clearGesture();
    // UIManager owns drop completion; do not erase a live drag payload here.
    return handled;
}
void DragSourceList::onCaptureCancelled() { cancelPendingDrag(); }
} // namespace ayt::ui::authoring
