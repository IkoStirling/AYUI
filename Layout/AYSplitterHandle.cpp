#include "AYSplitterHandle.h"
#include "AYBox.h"
#include "IAYRenderBackend.h"

#include <cstdio>
#include <cstdlib>

namespace ayt::ui {

namespace {

bool splitterDebugEnabled()
{
    static int cached = -1;
    if (cached < 0) {
        const char* env = std::getenv("AY_UI_SPLITTER_DEBUG");
        cached = (env != nullptr && env[0] != '\0' && env[0] != '0') ? 1 : 0;
        if (cached) {
            std::fprintf(stderr,
                "[SplitterDebug] enabled (set AY_UI_SPLITTER_DEBUG=0 to disable)\n");
        }
    }
    return cached != 0;
}

const char* widgetLabel(const Widget* w)
{
    if (w == nullptr) {
        return "null";
    }
    if (!w->getId().empty()) {
        return w->getId().c_str();
    }
    if (w->isSplitterHandle()) {
        return "<SplitterHandle>";
    }
    return "<Widget>";
}

} // namespace

SplitterHandle::SplitterHandle() {
    setSize(math::FVector2(kDefaultWidth, 100.0f));
}

void SplitterHandle::bindPanels(BoxBase* owner, int beforePanelSlot, int afterPanelSlot) {
    _owner = owner;
    _beforePanelSlot = beforePanelSlot;
    _afterPanelSlot = afterPanelSlot;
}

math::FRectangle SplitterHandle::interactionBand() const {
    math::FRectangle band = getWorldBounds();
    // If layout accidentally stretched us to a fill slot, only the leading
    // kDefaultWidth strip is the real handle — otherwise "move away" still
    // lands inside the fat band and leave never fires. The thin axis is x
    // for Horizontal (HBox, vertical strip) and y for Vertical (VBox,
    // horizontal strip).
    if (_orientation == Orientation::Horizontal) {
        if (band.maxX - band.minX > kDefaultWidth + 0.5f) {
            band.maxX = band.minX + kDefaultWidth;
        }
    } else {
        if (band.maxY - band.minY > kDefaultWidth + 0.5f) {
            band.maxY = band.minY + kDefaultWidth;
        }
    }
    return band;
}

Widget* SplitterHandle::hitTest(const math::FVector2& worldPos) {
    if (!_visible) {
        return nullptr;
    }
    return interactionBand().contains(worldPos) ? this : nullptr;
}

bool SplitterHandle::isRevealed() const {
    if (_dragging) {
        return true;
    }
    // Both flags required — a stale _hoverElapsed after leave must never
    // keep the accent lit on its own.
    if (_hover && _hoverElapsed >= kHoverRevealDelay) {
        return true;
    }
    return false;
}

bool SplitterHandle::onMouseMove(const UIMouseEvent& e) {
    // Capture / drag delivers moves even when the cursor is outside the
    // band. Only arm hover when the pointer is actually inside.
    const math::FRectangle band = interactionBand();
    const math::FRectangle world = getWorldBounds();
    const bool inside = band.contains(e.mousePos);
    const bool wasHover = _hover;

    if (!inside) {
        clearHoverReveal();
        if (splitterDebugEnabled() && wasHover) {
            std::fprintf(stderr,
                "[SplitterDebug] %s onMouseMove OUTSIDE -> clearHover "
                "mouse=(%.1f,%.1f) band=[%.1f,%.1f)x[%.1f,%.1f) "
                "worldW=%.1f (clamped=%d)\n",
                widgetLabel(this), e.mousePos.x, e.mousePos.y,
                band.minX, band.maxX, band.minY, band.maxY,
                world.maxX - world.minX,
                (world.maxX - world.minX > kDefaultWidth + 0.5f) ? 1 : 0);
        }
        if (_dragging) {
            applyDrag(cursorAxisPos(e.mousePos));
            return true;
        }
        return false;
    }

    if (!_hover) {
        // Re-entering: restart the delay so quick in/out doesn't accumulate.
        _hoverElapsed = 0.0f;
        if (splitterDebugEnabled()) {
            std::fprintf(stderr,
                "[SplitterDebug] %s HOVER ENTER mouse=(%.1f,%.1f) "
                "bandW=%.1f worldW=%.1f\n",
                widgetLabel(this), e.mousePos.x, e.mousePos.y,
                band.maxX - band.minX, world.maxX - world.minX);
        }
    }
    _hover = true;
    if (_dragging) {
        applyDrag(cursorAxisPos(e.mousePos));
    }
    return true;
}

bool SplitterHandle::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }

    if (_owner == nullptr || _beforePanelSlot < 0 || _afterPanelSlot < 0) {
        if (BoxBase* box = dynamic_cast<BoxBase*>(getParent())) {
            box->rebindSplitters();
        }
    }

    if (_owner == nullptr || _beforePanelSlot < 0 || _afterPanelSlot < 0) {
        return false;
    }

    _dragging = true;
    _hover = true;
    _dragStartMouseAxisPos = cursorAxisPos(e.mousePos);
    _adjustBefore = _owner->slotSize(_beforePanelSlot) > 0.0f;
    const int targetSlot = _adjustBefore ? _beforePanelSlot : _afterPanelSlot;
    _dragStartPrimarySize = _owner->slotSize(targetSlot);
    if (splitterDebugEnabled()) {
        std::fprintf(stderr,
            "[SplitterDebug] %s BUTTON DOWN -> dragging=1 mouse=(%.1f,%.1f)\n",
            widgetLabel(this), e.mousePos.x, e.mousePos.y);
    }
    return true;
}

bool SplitterHandle::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }

    if (_dragging) {
        if (splitterDebugEnabled()) {
            std::fprintf(stderr,
                "[SplitterDebug] %s BUTTON UP -> endDrag mouse=(%.1f,%.1f)\n",
                widgetLabel(this), e.mousePos.x, e.mousePos.y);
        }
        endDrag();
        return true;
    }

    return false;
}

void SplitterHandle::clearHoverReveal() {
    const bool had = _hover || _hoverElapsed >= 0.0f;
    _hover = false;
    _hoverElapsed = -1.0f;
    if (splitterDebugEnabled() && had) {
        // If revealed stays 1 here, `_dragging` is still true — that is
        // NOT a hover bug; isRevealed() treats drag as always-on.
        std::fprintf(stderr,
            "[SplitterDebug] %s clearHoverReveal hover=0 elapsed=-1 "
            "dragging=%d revealed=%d\n",
            widgetLabel(this), _dragging ? 1 : 0, isRevealed() ? 1 : 0);
    }
}

void SplitterHandle::endDrag() {
    if (!_dragging && !_hover && _hoverElapsed < 0.0f) {
        return;
    }
    if (splitterDebugEnabled()) {
        std::fprintf(stderr,
            "[SplitterDebug] %s endDrag (was dragging=%d hover=%d)\n",
            widgetLabel(this), _dragging ? 1 : 0, _hover ? 1 : 0);
    }
    _dragging = false;
    clearHoverReveal();
}

void SplitterHandle::onMouseLeave() {
    if (splitterDebugEnabled()) {
        std::fprintf(stderr,
            "[SplitterDebug] %s onMouseLeave (was hover=%d elapsed=%.3f "
            "dragging=%d revealed=%d)\n",
            widgetLabel(this), _hover ? 1 : 0, _hoverElapsed,
            _dragging ? 1 : 0, isRevealed() ? 1 : 0);
    }
    clearHoverReveal();
}

UiCursorHint SplitterHandle::getCursorHint() const {
    if (_dragging || _hover) {
        return (_orientation == Orientation::Horizontal)
            ? UiCursorHint::SizeHorizontal
            : UiCursorHint::SizeVertical;
    }
    return UiCursorHint::Default;
}

void SplitterHandle::applyDrag(float mouseAxisPos) {
    if (_owner == nullptr) {
        return;
    }

    _owner->applySplitterDrag(_beforePanelSlot, _afterPanelSlot, mouseAxisPos,
                              _dragStartMouseAxisPos, _dragStartPrimarySize, _adjustBefore);
}

void SplitterHandle::tick(float dt) {
    // Self-heal: if hover was cleared, never keep a positive elapsed that
    // could be misread by a stale isRevealed caller.
    if (!_hover) {
        _hoverElapsed = -1.0f;
        return;
    }
    if (!_dragging && _hoverElapsed >= 0.0f) {
        const bool wasRevealed = isRevealed();
        _hoverElapsed += dt;
        if (splitterDebugEnabled() && !wasRevealed && isRevealed()) {
            std::fprintf(stderr,
                "[SplitterDebug] %s REVEAL threshold crossed elapsed=%.3f\n",
                widgetLabel(this), _hoverElapsed);
        }
    }
}

void SplitterHandle::onRender(IRenderBackend& renderer) {
    const math::FRectangle bounds = interactionBand();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    if (!isRevealed()) {
        return;
    }

    renderer.drawRect(bounds, math::FVector4(0.40f, 0.48f, 0.62f, 1.0f));

    constexpr float kGrabHalfSize = 1.0f;
    constexpr float kGrabInset = 4.0f;
    if (_orientation == Orientation::Horizontal) {
        // Vertical strip: grab bar runs down the middle, inset top/bottom.
        const float cx = (bounds.minX + bounds.maxX) * 0.5f;
        if (bounds.maxY - bounds.minY > kGrabInset + kGrabInset) {
            renderer.drawRect(
                math::FRectangle(cx - kGrabHalfSize,
                                 bounds.minY + kGrabInset,
                                 cx + kGrabHalfSize,
                                 bounds.maxY - kGrabInset),
                math::FVector4(0.85f, 0.88f, 0.92f, 0.9f));
        }
    } else {
        // Horizontal strip: grab bar runs across the middle, inset left/right.
        const float cy = (bounds.minY + bounds.maxY) * 0.5f;
        if (bounds.maxX - bounds.minX > kGrabInset + kGrabInset) {
            renderer.drawRect(
                math::FRectangle(bounds.minX + kGrabInset,
                                 cy - kGrabHalfSize,
                                 bounds.maxX - kGrabInset,
                                 cy + kGrabHalfSize),
                math::FVector4(0.85f, 0.88f, 0.92f, 0.9f));
        }
    }
}

} // namespace ayt::ui
