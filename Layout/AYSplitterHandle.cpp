#include "AYSplitterHandle.h"
#include "AYBox.h"
#include "AYIRenderBackend.h"

namespace ayt::ui {

SplitterHandle::SplitterHandle() {
    setSize(math::FVector2(kDefaultWidth, 100.0f));
}

void SplitterHandle::bindPanels(HBox* owner, int leftPanelSlot, int rightPanelSlot) {
    _owner = owner;
    _leftPanelSlot = leftPanelSlot;
    _rightPanelSlot = rightPanelSlot;
}

Widget* SplitterHandle::hitTest(const math::FVector2& worldPos) {
    if (!_visible) {
        return nullptr;
    }
    return getWorldBounds().contains(worldPos) ? this : nullptr;
}

bool SplitterHandle::isRevealed() const {
    if (_dragging) {
        return true;
    }
    if (_hover && _hoverElapsed >= kHoverRevealDelay) {
        return true;
    }
    return false;
}

bool SplitterHandle::onMouseMove(const UIMouseEvent& e) {
    if (!_hover) {
        // Re-entering: restart the delay counter so a quick in/out doesn't
        // accumulate across separate hover sessions.
        _hoverElapsed = 0.0f;
    }
    _hover = true;
    if (_dragging) {
        applyDrag(e.mousePos.x);
    }
    return true;
}

bool SplitterHandle::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }

    if (_owner == nullptr || _leftPanelSlot < 0 || _rightPanelSlot < 0) {
        if (HBox* box = dynamic_cast<HBox*>(getParent())) {
            box->rebindSplitters();
        }
    }

    if (_owner == nullptr || _leftPanelSlot < 0 || _rightPanelSlot < 0) {
        return false;
    }

    _dragging = true;
    _hover = true;
    _dragStartMouseX = e.mousePos.x;
    _adjustLeft = _owner->slotWidth(_leftPanelSlot) > 0.0f;
    const int targetSlot = _adjustLeft ? _leftPanelSlot : _rightPanelSlot;
    _dragStartPrimaryWidth = _owner->slotWidth(targetSlot);
    return true;
}

bool SplitterHandle::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }

    if (_dragging) {
        _dragging = false;
        // Drag-end: the splitter just followed the cursor via
        // applySplitterDrag — its world bounds may now sit squarely
        // under the release position, so a plain onMouseLeave will
        // never fire. Reset _hover + _hoverElapsed unconditionally so
        // the splitter returns to invisible the moment the user lets
        // go. The cursor entering the splitter band again later will
        // re-arm hover via onMouseMove (which restarts the delay
        // counter from 0).
        _hover = false;
        _hoverElapsed = -1.0f;
        return true;
    }

    return false;
}

void SplitterHandle::onMouseLeave() {
    if (!_dragging) {
        _hover = false;
        // Reset the delay counter immediately so the next hover starts
        // fresh (no carryover from the previous session).
        _hoverElapsed = -1.0f;
    }
}

UiCursorHint SplitterHandle::getCursorHint() const {
    if (_dragging || _hover) {
        return UiCursorHint::SizeHorizontal;
    }
    return UiCursorHint::Default;
}

void SplitterHandle::applyDrag(float mouseWorldX) {
    if (_owner == nullptr) {
        return;
    }

    _owner->applySplitterDrag(_leftPanelSlot, _rightPanelSlot, mouseWorldX,
                              _dragStartMouseX, _dragStartPrimaryWidth, _adjustLeft);
}

void SplitterHandle::tick(float dt) {
    if (_hover && !_dragging && _hoverElapsed >= 0.0f) {
        _hoverElapsed += dt;
    }
}

void SplitterHandle::onRender(IRenderBackend& renderer) {
    const math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    // Visual states:
    //   default + drag: invisible (zero draws)
    //   hover: invisible until the cursor has lingered for kHoverRevealDelay
    //   hover + delay elapsed: full accent fill + 2px grab handle
    //   drag: bypasses delay (drag is strong intent)
    if (!isRevealed()) {
        return;
    }

    // Revealed: fill the full splitter width with the accent color so the
    // user sees the hit zone light up, then draw a 2px grab handle down the
    // center to communicate "drag me". Inset the grab handle by 4px on each
    // end so it doesn't touch the splitter edge.
    renderer.drawRect(bounds, math::FVector4(0.40f, 0.48f, 0.62f, 1.0f));

    const float cx = (bounds.minX + bounds.maxX) * 0.5f;
    const float grabHalfWidth = 1.0f;
    const float grabInsetTop = 4.0f;
    const float grabInsetBottom = 4.0f;
    if (bounds.maxY - bounds.minY > grabInsetTop + grabInsetBottom) {
        renderer.drawRect(
            math::FRectangle(cx - grabHalfWidth,
                             bounds.minY + grabInsetTop,
                             cx + grabHalfWidth,
                             bounds.maxY - grabInsetBottom),
            math::FVector4(0.85f, 0.88f, 0.92f, 0.9f));
    }
}

} // namespace ayt::ui
