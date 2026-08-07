#include "AYWindow.h"
#include "AYScrollableWidget.h"
#include "AYScrollBarSync.h"
#include "IAYRenderBackend.h"
#include "aymath/MathUtils.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

namespace {

float clampAxis(float value, float parentSize, float windowSize, float minKeepVisible)
{
    const float keepVisible = std::min(windowSize, std::max(minKeepVisible, 1.0f));

    if (windowSize <= parentSize) {
        const float maxValue = parentSize - windowSize;
        return std::clamp(value, 0.0f, std::max(0.0f, maxValue));
    }

    const float minValue = keepVisible - windowSize;
    const float maxValue = parentSize - keepVisible;
    return std::clamp(value, minValue, maxValue);
}

// Phase D (D1) — SE-corner hit band width in pixels (matches convention from
// vscode / native OS chrome — 16px catches a mouse-drag reliably without
// claiming the entire lower-right body).
constexpr float kResizeBandPx = 16.0f;
// Top edge (title-bar overlap): only the outer few pixels resize. The rest
// of the title bar is window drag so move vs resize stays unambiguous.
constexpr float kTitleTopResizeBandPx = 4.0f;
constexpr float kResizeGripDotSize = 1.5f;

// PR-B1 — map a ResizeEdge to the OS cursor the OS expects to see. Source
// of truth for both in-drag (during a resize) and hover (idle) hints.
UiCursorHint cursorHintForEdge(ResizeEdge edge) {
    switch (edge) {
        case ResizeEdge::Top:
        case ResizeEdge::Bottom:
            return UiCursorHint::SizeNs;
        case ResizeEdge::Left:
        case ResizeEdge::Right:
            return UiCursorHint::SizeWe;
        case ResizeEdge::TopLeft:
        case ResizeEdge::BottomRight:
            return UiCursorHint::SizeNwse;
        case ResizeEdge::TopRight:
        case ResizeEdge::BottomLeft:
            return UiCursorHint::SizeNesw;
        case ResizeEdge::None:
        default:
            return UiCursorHint::Default;
    }
}

} // namespace

Window::Window()
    : _title(L"Window")
    , _movable(true)
    , _resizable(false)
    , _closable(true)
    , _modal(false)
    , _titleBarHeight(28.0f)
    , _minSize(120.0f, 80.0f)
    , _isDragging(false)
{
    setSize(math::FVector2(400.0f, 300.0f));
}

Window::~Window() {
    // _bodyVBar is an owned child via addChild(owning) — CompoundWidget's
    // dtor does not delete children (UI-OWN-1 invariant), but a host
    // using destroyWidgetTree(root) on us WILL recursively free it.
    // Code-review 2026-08-02 #18: the previous comment incorrectly said
    // addChildExternal (which would have leaked the bar — mirrors the
    // ListView / ScrollView / TabStrip pattern fixed in commit 7502ca7).
    // _bodyVBar is here only so we don't touch a freed pointer if our
    // dtor runs first (e.g. host detaches us then deletes).
    _bodyVBar = nullptr;
}

void Window::setMinSize(float width, float height) {
    _minSize = math::FVector2(std::max(width, 1.0f), std::max(height, 1.0f));
    setSize(getSize());
}

void Window::setMinSize(const math::FVector2& size) {
    setMinSize(size.x, size.y);
}

void Window::setSize(const math::FVector2& size) {
    Widget::setSize(math::FVector2(std::max(size.x, _minSize.x), std::max(size.y, _minSize.y)));
}

// PR-Container-Contract-Cut2: getClientRect = world bounds minus title
// bar. Used by both renderChildren's clip and hitTest's body-descent
// gate so they can't drift. Title-bar clicks (above the rect) fall
// through to self via the shared helper — the window IS the chrome.
math::FRectangle Window::getClientRect() const {
    const math::FRectangle bounds = getWorldBounds();
    return math::FRectangle(bounds.minX, bounds.minY + _titleBarHeight,
                            bounds.maxX, bounds.maxY);
}

Widget* Window::hitTest(const math::FVector2& worldPos) {
    if (!_visible) return nullptr;

    math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) return nullptr;

    // Prefer title-bar move over Top-edge resize so hover cursor matches
    // onMouseButtonDown (Move in title interior, Size* on corners/rim).
    if (_movable) {
        math::FRectangle titleBar(bounds.minX, bounds.minY,
                                  bounds.maxX, bounds.minY + _titleBarHeight);
        if (titleBar.contains(worldPos)) {
            if (_resizable) {
                const ResizeEdge edge = hitTestResizeEdge(worldPos);
                const bool corner =
                    edge == ResizeEdge::TopLeft ||
                    edge == ResizeEdge::TopRight;
                const bool topRim =
                    edge == ResizeEdge::Top
                    && worldPos.y <= bounds.minY + kTitleTopResizeBandPx;
                if (corner || topRim) {
                    return this;
                }
            }
            return this;
        }
    }

    if (_resizable && hitTestResizeEdge(worldPos) != ResizeEdge::None) {
        return this;
    }

    // Prefer the overflow scrollbar when present.
    if (_bodyVBar != nullptr && _bodyVBar->isVisible()) {
        if (Widget* hit = _bodyVBar->hitTest(worldPos)) {
            return hit;
        }
    }

    // PR-Container-Contract-Cut2: defer to the shared clipped helper.
    // The helper gates by getClientRect() (= body rect) so scrolled-off
    // children can't claim clicks outside the body, and vbar (a child)
    // is in the helper's descent path but already handled above.
    return compoundDescendHitTestClipped(this, worldPos);
}

ResizeEdge Window::hitTestResizeEdge(const math::FVector2& worldPos) const {
    if (!_resizable || !_visible) return ResizeEdge::None;
    const math::FRectangle bounds = getWorldBounds();

    // PR-B1 — guard against outside-window points. v1 SE-only test never
    // hit this case (it only checked inside-window band points) but PR-B1
    // tests outside-window too (50, 50 etc.). Without this guard, a point
    // like (0, 0) outside a (100, 100)-(500, 400) window matches the TL
    // corner band (x ≤ minX + kBand AND y ≤ minY + kBand).
    if (!bounds.contains(worldPos)) return ResizeEdge::None;

    // PR-B1 — 4 corners checked FIRST so a click on the intersection lands
    // on the corner rather than the adjacent edge (Windows convention).
    if (worldPos.x <= bounds.minX + kResizeBandPx &&
        worldPos.y <= bounds.minY + kResizeBandPx) {
        return ResizeEdge::TopLeft;
    }
    if (worldPos.x >= bounds.maxX - kResizeBandPx &&
        worldPos.y <= bounds.minY + kResizeBandPx) {
        return ResizeEdge::TopRight;
    }
    if (worldPos.x <= bounds.minX + kResizeBandPx &&
        worldPos.y >= bounds.maxY - kResizeBandPx) {
        return ResizeEdge::BottomLeft;
    }
    if (worldPos.x >= bounds.maxX - kResizeBandPx &&
        worldPos.y >= bounds.maxY - kResizeBandPx) {
        return ResizeEdge::BottomRight;
    }

    // PR-B1 — 4 edges as kResizeBandPx-thick bands AFTER the corner checks.
    // Top edge uses a thinner band so the title-bar interior stays Move.
    if (worldPos.y <= bounds.minY + kTitleTopResizeBandPx) {
        return ResizeEdge::Top;
    }
    if (worldPos.y >= bounds.maxY - kResizeBandPx) {
        return ResizeEdge::Bottom;
    }
    if (worldPos.x <= bounds.minX + kResizeBandPx) {
        return ResizeEdge::Left;
    }
    if (worldPos.x >= bounds.maxX - kResizeBandPx) {
        return ResizeEdge::Right;
    }

    return ResizeEdge::None;
}

math::FVector2 Window::localPositionFromMouse(const math::FVector2& mouseWorldPos) const {
    math::FVector2 parentOrigin(0.0f, 0.0f);
    if (Widget* parent = getParent()) {
        const math::FRectangle parentBounds = parent->getWorldBounds();
        parentOrigin = math::FVector2(parentBounds.minX, parentBounds.minY);
    }
    return mouseWorldPos - parentOrigin - _dragOffset;
}

void Window::clampPositionWithinParent() {
    Widget* parent = getParent();
    if (parent == nullptr) {
        return;
    }

    const float parentWidth  = parent->getWidth();
    const float parentHeight = parent->getHeight();
    const float windowWidth  = getWidth();
    const float windowHeight = getHeight();

    // PR-B1 hotfix: Window is mounted on the overlay (size 0,0 in production
    // because the overlay is a hit-test funnel, not a real layout region).
    // Clamping against a 0-sized parent forces y into [-keepVis, -keepVis]
    // which makes the window fly off-screen the first frame the user drags
    // the title bar. Skip the clamp when the parent doesn't have a real
    // bounding region — that's the convention for "free-floating" parents.
    if (parentWidth <= 0.0f || parentHeight <= 0.0f) {
        return;
    }

    const float minKeepWidth  = std::max(_minSize.x, _titleBarHeight);
    const float minKeepHeight = std::max(_minSize.y, _titleBarHeight);

    const math::FVector2 pos = getPosition();
    const float clampedX = clampAxis(pos.x, parentWidth, windowWidth, minKeepWidth);
    const float clampedY = clampAxis(pos.y, parentHeight, windowHeight, minKeepHeight);
    setPosition(math::FVector2(clampedX, clampedY));
}

bool Window::onMouseMove(const UIMouseEvent& e) {
    // PR-B1 — cache last mouse position for hover-cursor hint.
    _lastMouseWorldPos = e.mousePos;

    math::FRectangle bounds = getWorldBounds();
    math::FRectangle titleBar(bounds.minX, bounds.minY,
                               bounds.maxX, bounds.minY + _titleBarHeight);
    _titleBarHover = _movable && titleBar.contains(e.mousePos);

    // PR-B1 — full 8-edge resize drag. Edge determines which dimension(s)
    // of the rect change AND whether the position shifts (Left/Top edges
    // and corners whose component is Left/Top pull the rect's anchor
    // along with the mouse, so the opposite edge stays under the cursor).
    if (_isResizing) {
        const float dx = e.mousePos.x - _resizeStartMousePos.x;
        const float dy = e.mousePos.y - _resizeStartMousePos.y;

        float newW = _resizeStartSize.x;
        float newH = _resizeStartSize.y;
        switch (_resizeEdge) {
            case ResizeEdge::Right:
            case ResizeEdge::TopRight:
            case ResizeEdge::BottomRight:
                newW = _resizeStartSize.x + dx;
                break;
            case ResizeEdge::Left:
            case ResizeEdge::TopLeft:
            case ResizeEdge::BottomLeft:
                newW = _resizeStartSize.x - dx;
                break;
            default: break;
        }
        switch (_resizeEdge) {
            case ResizeEdge::Bottom:
            case ResizeEdge::BottomLeft:
            case ResizeEdge::BottomRight:
                newH = _resizeStartSize.y + dy;
                break;
            case ResizeEdge::Top:
            case ResizeEdge::TopLeft:
            case ResizeEdge::TopRight:
                newH = _resizeStartSize.y - dy;
                break;
            default: break;
        }
        setSize(math::FVector2(newW, newH));

        // Re-anchor from resize-down origin so Left/Top stay pinned to the
        // opposite edge without accumulating per-frame absolute deltas.
        const math::FVector2 finalSize = getSize();
        float posX = _resizeStartPos.x;
        float posY = _resizeStartPos.y;
        if (_resizeEdge == ResizeEdge::Left ||
            _resizeEdge == ResizeEdge::TopLeft ||
            _resizeEdge == ResizeEdge::BottomLeft) {
            const float actualDx = finalSize.x - _resizeStartSize.x;
            posX = _resizeStartPos.x - actualDx;
        }
        if (_resizeEdge == ResizeEdge::Top ||
            _resizeEdge == ResizeEdge::TopLeft ||
            _resizeEdge == ResizeEdge::TopRight) {
            const float actualDy = finalSize.y - _resizeStartSize.y;
            posY = _resizeStartPos.y - actualDy;
        }
        setPosition(math::FVector2(posX, posY));
        return true;
    }

    if (!_isDragging || !_movable) {
        return _titleBarHover;
    }

    setPosition(localPositionFromMouse(e.mousePos));
    clampPositionWithinParent();
    return true;
}

bool Window::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0) return false;

    math::FRectangle bounds = getWorldBounds();

    math::FRectangle titleBar(bounds.minX, bounds.minY,
                               bounds.maxX, bounds.minY + _titleBarHeight);

    // Title-bar move wins over Top-edge resize (corners still resize).
    // Matches Windows: grab the title to drag; grab the outer rim / corners
    // to resize.
    if (_movable && titleBar.contains(e.mousePos)) {
        const ResizeEdge edge = _resizable
            ? hitTestResizeEdge(e.mousePos)
            : ResizeEdge::None;
        const bool corner =
            edge == ResizeEdge::TopLeft ||
            edge == ResizeEdge::TopRight ||
            edge == ResizeEdge::BottomLeft ||
            edge == ResizeEdge::BottomRight;
        const bool topRim =
            edge == ResizeEdge::Top
            && e.mousePos.y <= bounds.minY + kTitleTopResizeBandPx;
        if (!corner && !topRim) {
            _isDragging = true;
            _dragOffset = e.mousePos - math::FVector2(bounds.minX, bounds.minY);
            setLayoutPositionManaged(false);
            bringToFront();
            return true;
        }
    }

    if (_resizable) {
        const ResizeEdge edge = hitTestResizeEdge(e.mousePos);
        if (edge != ResizeEdge::None) {
            _isResizing = true;
            _resizeEdge = edge;
            _resizeStartSize = getSize();
            _resizeStartPos = getPosition();
            _resizeStartMousePos = e.mousePos;
            bringToFront();
            return true;
        }
    }

    return false;
}

bool Window::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) {
        return false;
    }

    // Phase D (D1) — end of SE resize. Snapshot the size before resetting
    // state, then compare to the start size. Fire _onResize exactly once
    // if the size actually changed (avoids notification noise on click-without-
    // drag). Q4 — release-only callback.
    if (_isResizing) {
        const math::FVector2 newSize = getSize();
        const bool sizeChanged =
            newSize.x != _resizeStartSize.x || newSize.y != _resizeStartSize.y;
        _isResizing = false;
        _resizeEdge = ResizeEdge::None;
        if (sizeChanged && _onResize) {
            _onResize(_resizeStartSize, newSize);
        }
        return true;
    }

    if (_isDragging) {
        _isDragging = false;
        clampPositionWithinParent();
        return true;
    }

    return false;
}

UiCursorHint Window::getCursorHint() const {
    // PR-B1 — resize cursor wins over Move/Default, except title-bar
    // interior where Move wins over Top-edge SizeNs (matches click).
    if (_resizable && _isResizing && _resizeEdge != ResizeEdge::None) {
        return cursorHintForEdge(_resizeEdge);
    }
    if (_movable && (_isDragging || _titleBarHover)) {
        if (_resizable) {
            const ResizeEdge hoverEdge = hitTestResizeEdge(_lastMouseWorldPos);
            const bool corner =
                hoverEdge == ResizeEdge::TopLeft ||
                hoverEdge == ResizeEdge::TopRight;
            const math::FRectangle bounds = getWorldBounds();
            const bool topRim =
                hoverEdge == ResizeEdge::Top
                && _lastMouseWorldPos.y <= bounds.minY + kTitleTopResizeBandPx;
            if (corner || topRim) {
                return cursorHintForEdge(hoverEdge);
            }
        }
        return UiCursorHint::Move;
    }
    if (_resizable) {
        const ResizeEdge hoverEdge = hitTestResizeEdge(_lastMouseWorldPos);
        if (hoverEdge != ResizeEdge::None) {
            return cursorHintForEdge(hoverEdge);
        }
    }
    return UiCursorHint::Default;
}

void Window::onMouseLeave() {
    _titleBarHover = false;
}

void Window::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    math::FRectangle titleBar(bounds.minX, bounds.minY, bounds.maxX, bounds.minY + _titleBarHeight);
    math::FRectangle body(bounds.minX, bounds.minY + _titleBarHeight, bounds.maxX, bounds.maxY);

    renderer.drawRect(bounds, math::FVector4(0.16f, 0.16f, 0.18f, 1.0f));
    renderer.drawRect(titleBar, math::FVector4(0.22f, 0.22f, 0.25f, 1.0f));
    renderer.drawRect(body, math::FVector4(0.12f, 0.12f, 0.14f, 1.0f));
    renderer.drawBorderRect(bounds, math::FVector4(0.08f, 0.08f, 0.08f, 1.0f), 1.0f, 0.0f);

    if (!_title.empty()) {
        math::FRectangle titleBounds(titleBar.minX + 8.0f, titleBar.minY,
                                     titleBar.maxX - 8.0f, titleBar.maxY);
        renderer.drawText(titleBounds, _title, 13, math::FVector4(0.92f, 0.92f, 0.92f, 1.0f));
    }

    if (_resizable) {
        renderResizeGrip(renderer);
    }
}

void Window::renderChildren(IRenderBackend& renderer) {
    // PR-Container-Contract-Cut2: helper handles push/pop balance and
    // uses getClientRect() (= body rect) as the clip. _bodyVBar is
    // excluded so it stays painted after popClip — vbar is chrome,
    // it must not be clipped to the body.
    compoundDescendClippedRender(this, renderer, {_bodyVBar});
}

void Window::renderResizeGrip(IRenderBackend& renderer) const {
    const math::FRectangle bounds = getWorldBounds();
    const math::FVector4 gripColor(0.5f, 0.5f, 0.55f, 0.8f);

    // Inner SE corner of the body, offset 6px from the bottom-right edge.
    // Walk NW along a 3-dot diagonal at 3px spacing — the exact spacing
    // matches Win32's min/max/restore grip icon at small sizes.
    const float margin = 6.0f;
    const float spacing = 3.0f;
    for (int i = 0; i < 3; ++i) {
        const float cx = bounds.maxX - margin - static_cast<float>(i) * spacing - kResizeGripDotSize;
        const float cy = bounds.maxY - margin - static_cast<float>(i) * spacing - kResizeGripDotSize;
        renderer.drawRect(
            math::FRectangle(cx, cy, cx + kResizeGripDotSize, cy + kResizeGripDotSize),
            gripColor);
    }
}

void Window::ensureBodyScrollBar() {
    if (!_bodyScrollEnabled || _bodyVBar != nullptr) {
        return;
    }
    _bodyVBar = new ScrollBar();
    _bodyVBar->setOrientation(ScrollBar::Orientation::Vertical);
    _bodyVBar->setOnValueChanged([this](float v) {
        // PR-Container-Shared-Contract: Window manages its own _scrollY,
        // so we use the pure-function clampScrollOffset rather than the
        // mutating scrollBy. The 0.01f visual threshold is preserved.
        const math::FVector2 clamped = ScrollableWidget::clampScrollOffset(
            math::FVector2(0.0f, v),
            math::FVector2(0.0f, _bodyViewportH),
            math::FVector2(0.0f, _contentExtentY));
        if (std::fabs(clamped.y - _scrollY) < 0.01f) {
            return;
        }
        _scrollY = clamped.y;
        const float paddingLeft = 8.0f;
        const float paddingTop = 8.0f;
        const float spacing = 6.0f;
        const float contentTop = _titleBarHeight + paddingTop;
        float y = contentTop - _scrollY;
        for (Widget* child : _children) {
            if (child == nullptr || child == _bodyVBar) {
                continue;
            }
            const float h = child->getHeight();
            child->setPosition(math::FVector2(paddingLeft, y));
            y += h + spacing;
        }
    });
    addChild(_bodyVBar);
}

void Window::syncBodyScrollBar() {
    if (_bodyVBar == nullptr) {
        return;
    }
    const float maxScroll = std::max(0.0f, _contentExtentY - _bodyViewportH);
    const bool needed = maxScroll > 0.5f;
    _bodyVBar->setVisible(needed);
    if (!needed) {
        _scrollY = 0.0f;
        return;
    }
    // PR-SyncVerticalBar: helper locks range→viewport→value order.
    // Visibility toggle + scrollY reset above are intentionally NOT
    // folded into the helper — they're Window-specific epilogue.
    _scrollY = std::clamp(_scrollY, 0.0f, maxScroll);
    syncVerticalBar(_bodyVBar, _contentExtentY, _bodyViewportH, _scrollY);
}

bool Window::scrollBodyBy(float dy) {
    if (!_bodyScrollEnabled) {
        return false;
    }
    const math::FVector2 clamped = ScrollableWidget::clampScrollOffset(
        math::FVector2(0.0f, _scrollY + dy),
        math::FVector2(0.0f, _bodyViewportH),
        math::FVector2(0.0f, _contentExtentY));
    if (clamped.y <= 0.0f && _contentExtentY <= _bodyViewportH) {
        return false;
    }
    if (std::fabs(clamped.y - _scrollY) < 0.01f) {
        return false;
    }
    _scrollY = clamped.y;
    if (_bodyVBar != nullptr) {
        _bodyVBar->setValue(_scrollY);
    }
    layoutChildren();
    return true;
}

void Window::layoutChildren() {
    const float paddingLeft = 8.0f;
    const float paddingTop = 8.0f;
    const float paddingRight = 8.0f;
    const float paddingBottom = 8.0f;
    const float spacing = 6.0f;

    const float contentTop = _titleBarHeight + paddingTop;
    float contentWidth = getWidth() - paddingLeft - paddingRight;
    _bodyViewportH = getHeight() - contentTop - paddingBottom;
    if (contentWidth <= 0.0f || _bodyViewportH <= 0.0f) {
        return;
    }

    // Measure stacked body children (exclude the overflow scrollbar).
    float totalFixedHeight = 0.0f;
    size_t fillCount = 0;
    size_t bodyCount = 0;
    for (Widget* child : _children) {
        if (child == nullptr || child == _bodyVBar) {
            continue;
        }
        ++bodyCount;
        const float childHeight = child->getHeight();
        if (childHeight > 0.0f) {
            totalFixedHeight += childHeight;
        } else {
            ++fillCount;
        }
        totalFixedHeight += spacing;
    }
    if (bodyCount > 0) {
        totalFixedHeight -= spacing;
    }

    // Overflow → reserve bar width, then place children with -_scrollY.
    const float barW = ScrollBar::kDefaultBarWidth;
    const bool overflow = _bodyScrollEnabled && totalFixedHeight > _bodyViewportH + 0.5f;
    if (overflow) {
        ensureBodyScrollBar();
        contentWidth = std::max(1.0f, contentWidth - barW);
    }

    _contentExtentY = totalFixedHeight;
    if (!overflow) {
        _scrollY = 0.0f;
    } else {
        const float maxOff = std::max(0.0f, _contentExtentY - _bodyViewportH);
        _scrollY = std::clamp(_scrollY, 0.0f, maxOff);
    }

    const float fillHeight = (fillCount > 0 && !overflow)
        ? (_bodyViewportH - totalFixedHeight) / static_cast<float>(fillCount)
        : 0.0f;

    float y = contentTop - _scrollY;
    for (Widget* child : _children) {
        if (child == nullptr || child == _bodyVBar) {
            continue;
        }
        float childHeight = child->getHeight();
        if (childHeight <= 0.0f) {
            childHeight = (fillHeight > 0.0f) ? fillHeight : 22.0f;
        }

        child->setPosition(math::FVector2(paddingLeft, y));
        child->setSize(math::FVector2(contentWidth, childHeight));
        y += childHeight + spacing;
    }

    if (_bodyVBar != nullptr) {
        _bodyVBar->setPosition(math::FVector2(
            getWidth() - paddingRight - barW + 4.0f, contentTop));
        _bodyVBar->setSize(math::FVector2(barW, _bodyViewportH));
        syncBodyScrollBar();
    }
}

Widget* createWindowWidget() {
    return new Window();
}

} // namespace ayt::ui
