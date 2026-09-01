#include "AYUI/TileView.h"

#include "AYUI/IRenderBackend.h"
#include "AYUI/ScrollBarSync.h"
#include "AYUI/Style.h"
#include "AYUI/TextMeasure.h"
#include "AYUI/UIKeyCode.h"
#include "AYUI/UIManager.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ayt::ui {

namespace {

constexpr float kCellInset = 6.0f;
constexpr float kAutoScrollEdge = 32.0f;
constexpr float kAutoScrollMaxSpeed = 560.0f;

const math::FVector4& defaultCornerMarkerColor() {
    static const math::FVector4 color(0.92f, 0.68f, 0.20f, 1.0f);
    return color;
}

bool modifierDown(uint32_t modifiers, UIKeyCode key) {
    return (modifiers & (1u << (key - UIKey_Shift))) != 0;
}

float rectangleWidth(const math::FRectangle& r) {
    return std::max(0.0f, r.maxX - r.minX);
}

float rectangleHeight(const math::FRectangle& r) {
    return std::max(0.0f, r.maxY - r.minY);
}

std::wstring ellipsizeTileText(const std::wstring& text,
                               float availableWidth,
                               IRenderBackend& renderer,
                               int fontSize) {
    if (text.empty() || availableWidth <= 0.0f) return {};
    if (measurePrefixWidth(text, text.size(), &renderer, fontSize)
        <= availableWidth) {
        return text;
    }

    const std::wstring ellipsis = L"\x2026";
    if (measurePrefixWidth(ellipsis, ellipsis.size(), &renderer, fontSize)
        > availableWidth) {
        return {};
    }

    // Keep both ends: asset labels often carry their distinguishing suffix
    // (including the extension) at the end. Binary-search the number of
    // retained characters so this cost is paid only when the cell cache is
    // invalidated, not on every frame.
    size_t low = 0;
    size_t high = text.size();
    while (low < high) {
        const size_t kept = low + (high - low + 1u) / 2u;
        const size_t prefix = (kept + 1u) / 2u;
        const size_t suffix = kept - prefix;
        const std::wstring candidate = text.substr(0, prefix) + ellipsis
            + text.substr(text.size() - suffix, suffix);
        if (measurePrefixWidth(candidate, candidate.size(), &renderer,
                               fontSize) <= availableWidth) {
            low = kept;
        } else {
            high = kept - 1u;
        }
    }

    const size_t prefix = (low + 1u) / 2u;
    const size_t suffix = low - prefix;
    return text.substr(0, prefix) + ellipsis
        + text.substr(text.size() - suffix, suffix);
}

} // namespace

// =============================================================================
// TileCell
// =============================================================================

TileCell::TileCell() {
    setSize(math::FVector2(104.0f, 112.0f));
    setLayoutPositionManaged(false);
    setLayoutSizeManaged(false);
    setAccessibilityRole(AccessibilityRole::ListItem);
    setAccessibilityActionHandler([this](AccessibilityAction action) {
        return _owner != nullptr
            && _owner->performAccessibilityAction(this, action);
    });
}

TileCell::~TileCell() = default;

void TileCell::setText(const std::wstring& text) {
    if (_text == text) return;
    _text = text;
    _displayTextWidth = -1.0f;
    setAccessibilityLabel(text);
    markDirty();
}

void TileCell::setSecondaryText(const std::wstring& text) {
    if (_secondaryText == text) return;
    _secondaryText = text;
    _displayTextWidth = -1.0f;
    markDirty();
}

void TileCell::setBadgeText(const std::wstring& text) {
    if (_badgeText == text) return;
    _badgeText = text;
    markDirty();
}

void TileCell::setThumbnail(const ImageTextureHandle& texture) {
    if (_thumbnail.handle == texture.handle
        && _thumbnail.width == texture.width
        && _thumbnail.height == texture.height
        && _thumbnail.format == texture.format
        && _thumbnail.name == texture.name
        && _thumbnail.generation == texture.generation) {
        return;
    }
    _thumbnail = texture;
    markDirty();
}

void TileCell::clearThumbnail() {
    if (!_thumbnail.isValid() && _thumbnail.name.empty()) return;
    _thumbnail = ImageTextureHandle{};
    markDirty();
}

void TileCell::setThumbnailUV(const math::FRectangle& uv) {
    _thumbnailUv = uv;
    markDirty();
}

void TileCell::setAccentColor(const math::FVector4& color) {
    if (_accentColor == color) return;
    _accentColor = color;
    markDirty();
}

void TileCell::setInfoStrip(
    const std::wstring& text, const math::FVector4& backgroundColor,
    const math::FVector4& textColor) {
    if (_infoStripText == text && _infoStripColor == backgroundColor
        && _infoStripTextColor == textColor) {
        return;
    }
    _infoStripText = text;
    _infoStripColor = backgroundColor;
    _infoStripTextColor = textColor;
    markDirty();
}

void TileCell::clearInfoStrip() {
    if (_infoStripText.empty()) return;
    _infoStripText.clear();
    markDirty();
}

void TileCell::setCornerMarkerVisible(bool visible) {
    if (_cornerMarkerVisible == visible) return;
    _cornerMarkerVisible = visible;
    markDirty();
}

void TileCell::setCornerMarkerColor(const math::FVector4& color) {
    if (_cornerMarkerColor == color) return;
    _cornerMarkerColor = color;
    markDirty();
}

math::FRectangle TileCell::getLabelRect() const {
    const math::FRectangle b = getWorldBounds();
    const float labelTop = std::max(b.minY,
        b.maxY - std::max(0.0f, _labelHeight));
    return math::FRectangle(
        b.minX + kCellInset,
        labelTop,
        b.maxX - kCellInset,
        b.maxY - 2.0f);
}

math::FRectangle TileCell::getInfoStripRect() const {
    const math::FRectangle b = getWorldBounds();
    const math::FRectangle label = getLabelRect();
    const float bottom = std::max(b.minY + kCellInset, label.minY);
    const float top = std::max(b.minY + kCellInset,
        bottom - std::max(0.0f, _infoStripHeight));
    return math::FRectangle(
        b.minX + kCellInset, top, b.maxX - kCellInset, bottom);
}

math::FRectangle TileCell::getThumbnailRect() const {
    const math::FRectangle b = getWorldBounds();
    const math::FRectangle strip = getInfoStripRect();
    const float slotMinX = b.minX + kCellInset;
    const float slotMinY = b.minY + kCellInset;
    const float slotMaxX = std::max(slotMinX, b.maxX - kCellInset);
    const float slotMaxY = std::max(slotMinY, strip.minY);
    const float slotWidth = std::max(0.0f, slotMaxX - slotMinX);
    const float slotHeight = std::max(0.0f, slotMaxY - slotMinY);
    if (slotWidth <= 0.0f || slotHeight <= 0.0f) {
        return math::FRectangle(slotMinX, slotMinY, slotMinX, slotMinY);
    }

    const float ratio = std::max(0.01f, _thumbnailAspectRatio);
    float width = slotWidth;
    float height = width / ratio;
    if (height > slotHeight) {
        height = slotHeight;
        width = height * ratio;
    }
    const float left = slotMinX + (slotWidth - width) * 0.5f;
    const float top = slotMinY + (slotHeight - height) * 0.5f;
    return math::FRectangle(left, top, left + width, top + height);
}

math::FRectangle TileCell::getCornerMarkerBounds() const {
    const math::FRectangle thumbnail = getThumbnailRect();
    const float size = std::min({
        std::max(0.0f, _cornerMarkerSize),
        rectangleWidth(thumbnail), rectangleHeight(thumbnail)});
    return math::FRectangle(
        thumbnail.maxX - size, thumbnail.minY,
        thumbnail.maxX, thumbnail.minY + size);
}

TileCell::HitRegion TileCell::hitRegion(
    const math::FVector2& worldPos) const {
    if (!isVisible() || !getWorldBounds().contains(worldPos)) {
        return HitRegion::None;
    }
    if (getLabelRect().contains(worldPos)) return HitRegion::Label;
    if (_cornerMarkerVisible) {
        const math::FRectangle marker = getCornerMarkerBounds();
        if (marker.contains(worldPos)) {
            // Only the painted half of the marker bounds is decoration; the
            // transparent half keeps the thumbnail's normal hit semantics.
            const float localX = worldPos.x - marker.minX;
            const float localY = worldPos.y - marker.minY;
            if (localX + 0.001f >= localY) return HitRegion::Body;
        }
    }
    if (getThumbnailRect().contains(worldPos)) return HitRegion::Thumbnail;
    return HitRegion::Body;
}

void TileCell::bindOwner(TileView* owner, int index, float labelHeight,
                         float infoStripHeight, float cornerMarkerSize,
                         float thumbnailAspectRatio) {
    const bool changed = _owner != owner || _index != index
        || std::fabs(_labelHeight - labelHeight) > 0.01f
        || std::fabs(_infoStripHeight - infoStripHeight) > 0.01f
        || std::fabs(_cornerMarkerSize - cornerMarkerSize) > 0.01f
        || std::fabs(_thumbnailAspectRatio - thumbnailAspectRatio) > 0.001f;
    _owner = owner;
    _index = index;
    _labelHeight = labelHeight;
    _infoStripHeight = infoStripHeight;
    _cornerMarkerSize = cornerMarkerSize;
    _thumbnailAspectRatio = thumbnailAspectRatio;
    if (changed) markDirty();
}

void TileCell::resetPresentation(const std::wstring& text) {
    setText(text);
    setSecondaryText({});
    setBadgeText({});
    clearThumbnail();
    setThumbnailUV(math::FRectangle(0.0f, 0.0f, 1.0f, 1.0f));
    setAccentColor(math::FVector4(0.32f, 0.58f, 0.92f, 1.0f));
    clearInfoStrip();
    setCornerMarkerVisible(false);
    setCornerMarkerColor(defaultCornerMarkerColor());
}

void TileCell::setCurrent(bool current) {
    if (_current == current) return;
    _current = current;
    markDirty();
}

void TileCell::updateDisplayText(IRenderBackend& renderer,
                                 float availableWidth) {
    const float width = std::max(0.0f, availableWidth);
    if (std::fabs(_displayTextWidth - width) <= 0.01f) return;
    _displayTextWidth = width;
    _displayText = ellipsizeTileText(_text, width, renderer, 13);
    _displaySecondaryText = ellipsizeTileText(
        _secondaryText, width, renderer, 11);
}

bool TileCell::onMouseMove(const UIMouseEvent& e) {
    const bool over = SelectableWidget::onMouseMove(e);
    if (_owner != nullptr) _owner->handleCellMove(this, e);
    return over || (_owner != nullptr && _owner->_pressedCell == this);
}

bool TileCell::onMouseButtonDown(const UIMouseEvent& e) {
    const bool handled = SelectableWidget::onMouseButtonDown(e);
    if (handled && _owner != nullptr) {
        _owner->handleCellPress(this, e);
    }
    return handled;
}

bool TileCell::onMouseButtonUp(const UIMouseEvent& e) {
    const bool ownerPressed = _owner != nullptr
        && _owner->_pressedCell == this;
    const bool handled = SelectableWidget::onMouseButtonUp(e);
    if (ownerPressed) {
        _owner->handleCellRelease(this, e);
    }
    return handled || ownerPressed;
}

void TileCell::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (rectangleWidth(b) <= 0.0f || rectangleHeight(b) <= 0.0f) return;

    math::FVector4 fill(0.16f, 0.17f, 0.19f, 1.0f);
    if (isSelected()) {
        fill = math::FVector4(0.18f, 0.34f, 0.55f, 1.0f);
    } else if (getState() == ButtonState::Pressed) {
        fill = math::FVector4(0.22f, 0.24f, 0.28f, 1.0f);
    } else if (getState() == ButtonState::Hovered) {
        fill = math::FVector4(0.19f, 0.21f, 0.24f, 1.0f);
    }
    fill = resolveTransitionColor(fill);

    renderer.drawRoundedRect(b, fill, 3.0f);

    const math::FRectangle thumbnail = getThumbnailRect();
    if (rectangleWidth(thumbnail) > 0.0f
        && rectangleHeight(thumbnail) > 0.0f) {
        if (_thumbnail.isValid()) {
            renderer.drawRect(thumbnail, _thumbnail.handle, _thumbnailUv);
        } else {
            renderer.drawRoundedRect(
                thumbnail,
                math::FVector4(0.10f, 0.11f, 0.13f, 1.0f),
                2.0f);
        }
    }

    const math::FRectangle infoStrip = getInfoStripRect();
    if (!_infoStripText.empty() && rectangleWidth(infoStrip) > 0.0f
        && rectangleHeight(infoStrip) > 0.0f) {
        renderer.drawRect(infoStrip, _infoStripColor);
        IRenderBackend::TextStyle stripTextStyle;
        stripTextStyle.color = _infoStripTextColor;
        stripTextStyle.align = IRenderBackend::TextStyle::Align::Center;
        stripTextStyle.valign = IRenderBackend::TextStyle::VAlign::Middle;
        renderer.drawText(infoStrip, _infoStripText, 10, stripTextStyle);
    }

    const math::FRectangle label = getLabelRect();
    if (!_text.empty() && rectangleHeight(label) > 0.0f) {
        updateDisplayText(renderer, rectangleWidth(label));
        // Text bounds are layout hints, not a backend scissor. A cell-local
        // clip is still required to contain glyph bearings and any fallback
        // renderer that cannot provide exact measurements.
        renderer.pushClip(label);
        if (_secondaryText.empty()) {
            renderer.drawText(label, _displayText, 13,
                math::FVector4(0.94f, 0.95f, 0.97f, 1.0f));
        } else {
            const float split = std::min(label.maxY, label.minY + 17.0f);
            renderer.drawText(
                math::FRectangle(label.minX, label.minY, label.maxX, split),
                _displayText, 13,
                math::FVector4(0.94f, 0.95f, 0.97f, 1.0f));
            renderer.drawText(
                math::FRectangle(label.minX, split, label.maxX, label.maxY),
                _displaySecondaryText, 11,
                math::FVector4(0.63f, 0.66f, 0.72f, 1.0f));
        }
        renderer.popClip();
    }

    // A narrow category/accent strip is useful even without a custom painter.
    renderer.drawRect(
        math::FRectangle(b.minX + 3.0f, b.maxY - 3.0f,
                         b.maxX - 3.0f, b.maxY - 1.0f),
        _accentColor);

    if (!_badgeText.empty()) {
        const float badgeWidth = std::min(46.0f,
            std::max(22.0f, rectangleWidth(b) * 0.42f));
        const math::FRectangle badge(
            b.maxX - badgeWidth - 5.0f,
            b.minY + 5.0f,
            b.maxX - 5.0f,
            b.minY + 21.0f);
        renderer.drawRoundedRect(badge,
            math::FVector4(0.08f, 0.09f, 0.11f, 0.90f), 3.0f);
        renderer.drawText(badge, _badgeText, 10,
            math::FVector4(0.94f, 0.95f, 0.97f, 1.0f));
    }


    if (_cornerMarkerVisible) {
        const math::FRectangle marker = getCornerMarkerBounds();
        if (rectangleWidth(marker) > 0.0f
            && rectangleHeight(marker) > 0.0f) {
            const math::FVector2 points[3] = {
                math::FVector2(marker.minX, marker.minY),
                math::FVector2(marker.maxX, marker.minY),
                math::FVector2(marker.maxX, marker.maxY),
            };
            const IRenderBackend::PathHandle path = renderer.createPath();
            if (path.id >= 0) {
                renderer.addPathPolygon(path, points, 3);
                renderer.setPathFillColor(path, _cornerMarkerColor);
                renderer.drawPath(path, IRenderBackend::PathFillMode::Fill);
                renderer.releasePath(path);
            }
        }
    }

    // Focus/selection chrome is the final cell primitive so thumbnail,
    // strip, badge and corner marker can never cover it.
    renderer.drawBorderRect(
        b,
        _current
            ? math::FVector4(0.48f, 0.70f, 1.0f, 1.0f)
            : math::FVector4(0.32f, 0.34f, 0.38f, 1.0f),
        _current ? 2.0f : 1.0f,
        3.0f);
}

// =============================================================================
// TileView
// =============================================================================

TileView::TileView() {
    setSize(math::FVector2(360.0f, 260.0f));
    setAccessibilityRole(AccessibilityRole::List);
    ensureBarCreated();
}

TileView::~TileView() {
    if (UIManager* ui = UIManager::tryGet()) {
        ui->clearHoverNoDispatch(this);
        ui->clearCaptureNoDispatch(this);
        ui->clearFocusNoDispatch(this);
        if (_vbar != nullptr) {
            ui->clearHoverNoDispatch(_vbar);
            ui->clearCaptureNoDispatch(_vbar);
            ui->clearFocusNoDispatch(_vbar);
        }
    }
    destroyCellPool();
    if (_vbar != nullptr) {
        if (_vbar->getParent() == this) removeChild(_vbar);
        delete _vbar;
        _vbar = nullptr;
    }
}

void TileView::setItems(const std::vector<std::wstring>& items) {
    _items = items;

    std::vector<int> valid;
    valid.reserve(_selectedIndices.size());
    for (int index : _selectedIndices) {
        if (index >= 0 && index < static_cast<int>(_items.size())) {
            valid.push_back(index);
        }
    }
    setSelectedIndices(valid);

    if (_focusedIndex >= static_cast<int>(_items.size())) {
        _focusedIndex = _selectedIndex;
    }
    if (_anchorIndex >= static_cast<int>(_items.size())) {
        _anchorIndex = _selectedIndex;
    }
    performLayout();
}

void TileView::addItem(const std::wstring& item) {
    _items.push_back(item);
    performLayout();
}

void TileView::clearItems() {
    _items.clear();
    clearSelection();
    _focusedIndex = -1;
    _anchorIndex = -1;
    _pressedCell = nullptr;
    _pressedIndex = -1;
    _scrollState.clearMomentum();
    _scrollState.setScrollOffset(math::FVector2(0.0f, 0.0f));
    performLayout();
}

const std::wstring& TileView::getItem(size_t index) const {
    static const std::wstring empty;
    return index < _items.size() ? _items[index] : empty;
}

void TileView::setCellBinder(CellBinder binder) {
    _cellBinder = std::move(binder);
    rebindCellPool();
}

void TileView::setTileSize(const math::FVector2& size) {
    const math::FVector2 clamped(
        std::max(24.0f, size.x), std::max(24.0f, size.y));
    if (_tileSize == clamped) return;
    _tileSize = clamped;
    _labelHeight = std::min(_labelHeight, _tileSize.y);
    _infoStripHeight = std::min(_infoStripHeight, _tileSize.y);
    _cornerMarkerSize = std::min(
        _cornerMarkerSize, std::min(_tileSize.x, _tileSize.y));
    performLayout();
}

void TileView::setTileSpacing(float spacing) {
    spacing = std::max(0.0f, spacing);
    if (std::fabs(_tileSpacing - spacing) < 0.01f) return;
    _tileSpacing = spacing;
    performLayout();
}

void TileView::setContentPadding(float padding) {
    padding = std::max(0.0f, padding);
    if (std::fabs(_contentPadding - padding) < 0.01f) return;
    _contentPadding = padding;
    performLayout();
}

void TileView::setLabelHeight(float height) {
    height = std::clamp(height, 0.0f, _tileSize.y);
    if (std::fabs(_labelHeight - height) < 0.01f) return;
    _labelHeight = height;
    rebindCellPool();
}

void TileView::setInfoStripHeight(float height) {
    height = std::clamp(height, 0.0f, _tileSize.y);
    if (std::fabs(_infoStripHeight - height) < 0.01f) return;
    _infoStripHeight = height;
    rebindCellPool();
}

void TileView::setCornerMarkerSize(float size) {
    size = std::clamp(size, 0.0f, std::min(_tileSize.x, _tileSize.y));
    if (std::fabs(_cornerMarkerSize - size) < 0.01f) return;
    _cornerMarkerSize = size;
    rebindCellPool();
}

void TileView::setThumbnailAspectRatio(float ratio) {
    ratio = std::max(0.01f, ratio);
    if (std::fabs(_thumbnailAspectRatio - ratio) < 0.001f) return;
    _thumbnailAspectRatio = ratio;
    rebindCellPool();
}

void TileView::setOverscanRows(int rows) {
    rows = std::max(0, rows);
    if (_overscanRows == rows) return;
    _overscanRows = rows;
    performLayout();
}

void TileView::setSelectionMode(SelectionMode mode) {
    if (_selectionMode == mode) return;
    _selectionMode = mode;
    if (mode == SelectionMode::Single && _selectedIndices.size() > 1) {
        const int keep = (_focusedIndex >= 0 && isSelected(_focusedIndex))
            ? _focusedIndex : _selectedIndex;
        setSelectedIndices(keep >= 0 ? std::vector<int>{keep}
                                     : std::vector<int>{});
    }
}

void TileView::setSelectedIndex(int index) {
    if (index < 0 || index >= static_cast<int>(_items.size())) {
        clearSelection();
        return;
    }
    setSelectedIndices({index});
    _anchorIndex = index;
    setFocusedIndex(index, true);
}

void TileView::setSelectedIndices(const std::vector<int>& indices) {
    std::vector<int> next;
    next.reserve(indices.size());
    for (int index : indices) {
        if (index >= 0 && index < static_cast<int>(_items.size())) {
            next.push_back(index);
        }
    }
    std::sort(next.begin(), next.end());
    next.erase(std::unique(next.begin(), next.end()), next.end());
    if (_selectionMode == SelectionMode::Single && next.size() > 1) {
        next = {next.back()};
    }
    if (next == _selectedIndices) return;

    _selectedIndices = std::move(next);
    _selectedIndexSet.clear();
    _selectedIndexSet.reserve(_selectedIndices.size());
    for (int index : _selectedIndices) _selectedIndexSet.insert(index);
    _selectedIndex = _selectedIndices.empty() ? -1 : _selectedIndices.back();
    if (_focusedIndex < 0 && _selectedIndex >= 0) {
        _focusedIndex = _selectedIndex;
    }
    updateCellStates();
    if (_onSelectionChanged) _onSelectionChanged(_selectedIndex);
    if (_onSelectionIndicesChanged) {
        _onSelectionIndicesChanged(_selectedIndices);
    }
    markDirty();
}

bool TileView::isSelected(int index) const {
    return _selectedIndexSet.find(index) != _selectedIndexSet.end();
}

void TileView::clearSelection() {
    setSelectedIndices({});
}

void TileView::setFocusedIndex(int index, bool scrollIntoView) {
    if (index < 0 || index >= static_cast<int>(_items.size())) index = -1;
    if (_focusedIndex == index) {
        if (scrollIntoView && index >= 0) scrollToIndex(index);
        return;
    }
    _focusedIndex = index;
    updateCellStates();
    if (scrollIntoView && index >= 0) scrollToIndex(index);
    markDirty();
}

void TileView::setDragThreshold(float pixels) {
    _dragThreshold = std::max(0.0f, pixels);
}

void TileView::setDragEnabled(bool enabled) {
    if (_dragEnabled == enabled) return;
    _dragEnabled = enabled;
    for (TileCell* cell : _cellPool) {
        if (cell != nullptr) cell->setDraggable(enabled);
    }
    if (!enabled && !_dragInProgress) {
        _pressedCell = nullptr;
        _pressedIndex = -1;
        _deferPlainCollapse = false;
    }
}

int TileView::getCellPoolLogicalIndex(size_t slot) const {
    if (slot >= _cellPool.size() || _cellPool[slot] == nullptr) return -1;
    return _cellPool[slot]->getIndex();
}

TileCell* TileView::cellForLogicalIndex(int index) const {
    if (index < _firstPooledIndex) return nullptr;
    const size_t slot = static_cast<size_t>(index - _firstPooledIndex);
    if (slot >= _cellPool.size()) return nullptr;
    TileCell* cell = _cellPool[slot];
    return cell != nullptr && cell->isVisible() && cell->getIndex() == index
        ? cell : nullptr;
}

int TileView::indexAt(const math::FVector2& worldPos) const {
    if (!getClientRect().contains(worldPos)) return -1;
    for (auto it = _cellPool.rbegin(); it != _cellPool.rend(); ++it) {
        TileCell* cell = *it;
        if (cell != nullptr && cell->isVisible()
            && cell->getWorldBounds().contains(worldPos)) {
            return cell->getIndex();
        }
    }
    return -1;
}

TileCell::HitRegion TileView::regionAt(
    const math::FVector2& worldPos) const {
    const int index = indexAt(worldPos);
    TileCell* cell = cellForLogicalIndex(index);
    return cell != nullptr ? cell->hitRegion(worldPos)
                           : TileCell::HitRegion::None;
}

TileView::GridMetrics TileView::computeGridMetrics() const {
    GridMetrics result;
    const float pitchX = std::max(1.0f, _tileSize.x + _tileSpacing);

    auto fillForWidth = [&](float clientWidth, GridMetrics& out) {
        const float usable = std::max(0.0f,
            clientWidth - _contentPadding * 2.0f);
        out.columns = std::max(1, static_cast<int>(
            std::floor((usable + _tileSpacing) / pitchX)));
        out.rows = _items.empty() ? 0
            : (static_cast<int>(_items.size()) + out.columns - 1)
                / out.columns;
        const float gridWidth = out.columns * _tileSize.x
            + std::max(0, out.columns - 1) * _tileSpacing;
        out.contentWidth = std::max(clientWidth,
            _contentPadding * 2.0f + gridWidth);
        out.contentHeight = out.rows <= 0 ? 0.0f
            : _contentPadding * 2.0f
                + out.rows * _tileSize.y
                + std::max(0, out.rows - 1) * _tileSpacing;
    };

    fillForWidth(std::max(0.0f, getWidth()), result);
    result.needsVerticalBar = result.contentHeight > getHeight() + 0.01f;
    if (result.needsVerticalBar) {
        fillForWidth(std::max(0.0f,
            getWidth() - ScrollBar::kDefaultBarWidth), result);
        result.needsVerticalBar = true;
    }
    return result;
}

int TileView::computeVisibleRows() const {
    const float pitchY = std::max(1.0f, _tileSize.y + _tileSpacing);
    return std::max(1,
        static_cast<int>(std::ceil(std::max(0.0f, getHeight()) / pitchY)));
}

int TileView::computePoolSize() const {
    if (_items.empty() || _logicalRowCount <= 0) return 0;
    const int poolRows = std::min(_logicalRowCount,
        computeVisibleRows() + _overscanRows * 2);
    const int slots = poolRows * std::max(1, _columnCount);
    return std::min(static_cast<int>(_items.size()), slots);
}

void TileView::ensureBarCreated() {
    if (_vbar != nullptr) return;
    _vbar = new ScrollBar();
    _vbar->setOrientation(ScrollBar::Orientation::Vertical);
    _vbar->setOnValueChanged([this](float value) {
        _scrollState.clearMomentum();
        setScrollOffset(math::FVector2(0.0f, value));
    });
    addChildExternal(_vbar);
}

void TileView::syncBarToOffset() {
    syncVerticalBar(_vbar, _contentSize.y, getHeight(),
                    _scrollState.getScrollOffset().y);
}

void TileView::destroyCellPool() {
    if (UIManager* ui = UIManager::tryGet()) {
        for (TileCell* cell : _cellPool) {
            if (cell == nullptr) continue;
            ui->clearHoverNoDispatch(cell);
            ui->clearCaptureNoDispatch(cell);
            ui->clearFocusNoDispatch(cell);
            ui->clearDragStateNoDispatch(cell);
        }
    }
    for (TileCell* cell : _cellPool) {
        if (cell == nullptr) continue;
        if (cell->getParent() == this) removeChild(cell);
        delete cell;
    }
    _cellPool.clear();
    _pressedCell = nullptr;
    _pressedIndex = -1;
    _dragInProgress = false;
    _deferPlainCollapse = false;
    _activeDragIndices.clear();
}

void TileView::rebuildCellPool() {
    destroyCellPool();
    const int count = computePoolSize();
    _cellPool.reserve(static_cast<size_t>(count));
    for (int slot = 0; slot < count; ++slot) {
        TileCell* cell = new TileCell();
        cell->setDraggable(_dragEnabled);
        cell->setOnDragStart([this, cell]() {
            handleDragStarted(cell);
        });
        cell->setOnDragEnd([this, cell](bool accepted) {
            handleDragFinished(cell, accepted);
        });
        addChildExternal(cell);
        _cellPool.push_back(cell);
    }
    rebindCellPool();
}

void TileView::rebindCellPool() {
    if (_cellPool.empty() || _items.empty()) {
        for (TileCell* cell : _cellPool) {
            if (cell != nullptr) cell->setVisible(false);
        }
        _firstPooledIndex = 0;
        return;
    }

    const float pitchY = std::max(1.0f, _tileSize.y + _tileSpacing);
    const float offset = _scrollState.getScrollOffset().y;
    int rawVisibleRow = std::max(0, static_cast<int>(
        std::floor(std::max(0.0f, offset - _contentPadding) / pitchY)));
    // When the viewport starts in the inter-row spacing, the preceding tile
    // is already completely clipped. Advance to the first row that can
    // contribute pixels; overscan is applied after this exact visible row is
    // known, so the pool never needs a second implicit buffer row.
    const float candidateBottom = _contentPadding
        + rawVisibleRow * pitchY + _tileSize.y;
    if (candidateBottom <= offset + 0.01f
        && rawVisibleRow + 1 < _logicalRowCount) {
        ++rawVisibleRow;
    }
    const int poolRows = std::max(1,
        static_cast<int>(_cellPool.size() + _columnCount - 1)
            / std::max(1, _columnCount));
    const int maxFirstRow = std::max(0, _logicalRowCount - poolRows);
    const int firstRow = std::clamp(rawVisibleRow - _overscanRows,
                                    0, maxFirstRow);
    _firstPooledIndex = firstRow * _columnCount;

    const float pitchX = _tileSize.x + _tileSpacing;
    for (size_t slot = 0; slot < _cellPool.size(); ++slot) {
        TileCell* cell = _cellPool[slot];
        if (cell == nullptr) continue;
        const int logical = _firstPooledIndex + static_cast<int>(slot);
        if (logical < 0 || logical >= static_cast<int>(_items.size())) {
            cell->setVisible(false);
            cell->bindOwner(this, -1, _labelHeight, _infoStripHeight,
                            _cornerMarkerSize, _thumbnailAspectRatio);
            continue;
        }

        const int row = logical / _columnCount;
        const int column = logical % _columnCount;
        cell->setVisible(true);
        cell->setSize(_tileSize);
        cell->setPosition(math::FVector2(
            _contentPadding + column * pitchX,
            _contentPadding + row * pitchY - offset));
        cell->bindOwner(this, logical, _labelHeight, _infoStripHeight,
                        _cornerMarkerSize, _thumbnailAspectRatio);
        cell->resetPresentation(_items[logical]);
        cell->setSelected(isSelected(logical));
        cell->setCurrent(logical == _focusedIndex);
        cell->setDraggable(_dragEnabled);
        if (_cellBinder) _cellBinder(*cell, logical, _items[logical]);
    }
    markDirty();
}

void TileView::updateCellStates() {
    for (TileCell* cell : _cellPool) {
        if (cell == nullptr || !cell->isVisible()) continue;
        cell->setSelected(isSelected(cell->getIndex()));
        cell->setCurrent(cell->getIndex() == _focusedIndex);
    }
}

void TileView::performLayout() {
    layoutChildren();
}

void TileView::layoutChildren() {
    ensureBarCreated();
    const GridMetrics metrics = computeGridMetrics();
    const bool gridChanged = _columnCount != metrics.columns
        || _logicalRowCount != metrics.rows;
    _columnCount = metrics.columns;
    _logicalRowCount = metrics.rows;

    if (_vbar != nullptr) {
        _vbar->setVisible(metrics.needsVerticalBar);
        _vbar->setPosition(math::FVector2(
            std::max(0.0f, getWidth() - ScrollBar::kDefaultBarWidth), 0.0f));
        _vbar->setSize(math::FVector2(
            ScrollBar::kDefaultBarWidth, std::max(0.0f, getHeight())));
    }

    const float clientWidth = std::max(0.0f, getWidth()
        - (metrics.needsVerticalBar ? ScrollBar::kDefaultBarWidth : 0.0f));
    _contentSize = math::FVector2(clientWidth, metrics.contentHeight);
    _scrollState.setContentSize(_contentSize);
    const math::FVector2 clamped = ScrollableWidget::clampScrollOffset(
        _scrollState.getScrollOffset(),
        math::FVector2(clientWidth, getHeight()), _contentSize);
    _scrollState.setScrollOffset(math::FVector2(0.0f, clamped.y));
    syncBarToOffset();

    const int needed = computePoolSize();
    if (gridChanged || needed != static_cast<int>(_cellPool.size())) {
        rebuildCellPool();
    } else {
        rebindCellPool();
    }
    markDirty();
}

math::FRectangle TileView::getClientRect() const {
    const math::FRectangle b = getWorldBounds();
    const float barWidth = (_vbar != nullptr && _vbar->isVisible())
        ? ScrollBar::kDefaultBarWidth : 0.0f;
    return math::FRectangle(b.minX, b.minY,
                            std::max(b.minX, b.maxX - barWidth), b.maxY);
}

void TileView::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (rectangleWidth(b) <= 0.0f || rectangleHeight(b) <= 0.0f) return;

    const ResolvedStyle style = resolveStyle(getStyleId(), this);
    const math::FVector4 background = style.hasStyle
        ? style.backgroundColor : math::FVector4(0.105f, 0.11f, 0.125f, 1.0f);
    const math::FVector4 border = style.hasStyle
        ? style.borderColor : math::FVector4(0.34f, 0.36f, 0.40f, 1.0f);
    const float borderWidth = style.hasStyle ? style.borderWidth : 1.0f;

    renderer.drawRoundedRect(getClientRect(), background, 2.0f);
    renderer.drawBorderRect(b, border, borderWidth, 2.0f);
    renderer.pushClip(getClientRect());
    for (TileCell* cell : _cellPool) {
        if (cell != nullptr && cell->isVisible()) cell->render(renderer);
    }
    renderer.popClip();
    if (_vbar != nullptr && _vbar->isVisible()) _vbar->render(renderer);
}

void TileView::renderChildren(IRenderBackend& renderer) {
    // TileCell and ScrollBar are internal external-owned children and are
    // painted exactly once in onRender to preserve content/chrome ordering.
    (void)renderer;
}

Widget* TileView::hitTest(const math::FVector2& worldPos) {
    if (!isVisible() || !getWorldBounds().contains(worldPos)) return nullptr;
    if (_vbar != nullptr && _vbar->isVisible()) {
        if (Widget* hit = _vbar->hitTest(worldPos)) return hit;
    }
    if (!getClientRect().contains(worldPos)) return this;
    for (auto it = _cellPool.rbegin(); it != _cellPool.rend(); ++it) {
        TileCell* cell = *it;
        if (cell == nullptr || !cell->isVisible()) continue;
        if (Widget* hit = cell->hitTest(worldPos)) return hit;
    }
    return this;
}

void TileView::setScrollOffset(const math::FVector2& offset) {
    const math::FRectangle client = getClientRect();
    const math::FVector2 viewport(rectangleWidth(client), getHeight());
    const math::FVector2 clamped = ScrollableWidget::clampScrollOffset(
        math::FVector2(0.0f, offset.y), viewport, _contentSize);
    const math::FVector2 previous = _scrollState.getScrollOffset();
    if (std::fabs(previous.y - clamped.y) < 0.01f) return;
    _scrollState.setScrollOffset(clamped);
    syncBarToOffset();
    rebindCellPool();
    if (_onScroll) _onScroll(_scrollState.getScrollOffset());
}

bool TileView::scrollBy(float deltaY) {
    const float before = _scrollState.getScrollOffset().y;
    setScrollOffset(math::FVector2(0.0f, before + deltaY));
    return std::fabs(_scrollState.getScrollOffset().y - before) > 0.01f;
}

void TileView::scrollToIndex(int index) {
    if (index < 0 || index >= static_cast<int>(_items.size())) return;
    const int row = index / std::max(1, _columnCount);
    const float pitchY = _tileSize.y + _tileSpacing;
    const float top = _contentPadding + row * pitchY;
    const float bottom = top + _tileSize.y;
    const float viewTop = _scrollState.getScrollOffset().y;
    const float viewBottom = viewTop + getHeight();
    if (top < viewTop) {
        setScrollOffset(math::FVector2(0.0f, top));
    } else if (bottom > viewBottom) {
        setScrollOffset(math::FVector2(0.0f, bottom - getHeight()));
    }
}

bool TileView::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0 || !getClientRect().contains(e.mousePos)) {
        return false;
    }
    if (UIManager* ui = UIManager::tryGet()) ui->setFocus(this);

    // This handler is reached for empty grid space; cells route through
    // handleCellPress instead. Plain background click clears selection.
    uint32_t modifiers = 0;
    if (UIManager* ui = UIManager::tryGet()) modifiers = ui->getModifiers();
    const bool shift = modifierDown(modifiers, UIKey_Shift);
    const bool ctrl = modifierDown(modifiers, UIKey_Control);
    if (!shift && !ctrl) clearSelection();
    return true;
}

bool TileView::onMouseButtonUp(const UIMouseEvent& e) {
    return e.mouseButton == 0 && getWorldBounds().contains(e.mousePos);
}

bool TileView::onMouseWheel(const UIMouseWheelEvent& e) {
    const math::FRectangle client = getClientRect();
    const math::FVector2 previous = _scrollState.getScrollOffset();
    const bool changed = _scrollState.applyWheel(
        math::FVector2(0.0f, e.deltaY),
        math::FVector2(rectangleWidth(client), getHeight()));
    if (changed) {
        syncBarToOffset();
        rebindCellPool();
        if (_onScroll) _onScroll(_scrollState.getScrollOffset());
    }
    if (!changed && previous == _scrollState.getScrollOffset()) {
        _scrollState.clearMomentum();
    }
    return changed;
}

void TileView::applyPointerSelection(int index, uint32_t modifiers) {
    if (index < 0 || index >= static_cast<int>(_items.size())) return;
    const bool shift = modifierDown(modifiers, UIKey_Shift);
    const bool ctrl = modifierDown(modifiers, UIKey_Control);
    setFocusedIndex(index, false);

    if (_selectionMode == SelectionMode::Extended && shift) {
        const int anchor = _anchorIndex >= 0 ? _anchorIndex
                                             : (_selectedIndex >= 0
                                                ? _selectedIndex : index);
        const int lo = std::min(anchor, index);
        const int hi = std::max(anchor, index);
        std::vector<int> range(static_cast<size_t>(hi - lo + 1));
        std::iota(range.begin(), range.end(), lo);
        if (ctrl) {
            range.insert(range.end(), _selectedIndices.begin(),
                         _selectedIndices.end());
        }
        _anchorIndex = anchor;
        setSelectedIndices(range);
    } else if (_selectionMode == SelectionMode::Extended && ctrl) {
        std::vector<int> next = _selectedIndices;
        const auto found = std::find(next.begin(), next.end(), index);
        if (found == next.end()) next.push_back(index);
        else next.erase(found);
        _anchorIndex = index;
        setSelectedIndices(next);
    } else {
        _anchorIndex = index;
        setSelectedIndices({index});
    }
}

void TileView::handleCellPress(TileCell* cell, const UIMouseEvent& e) {
    if (cell == nullptr || e.mouseButton != 0 || cell->getIndex() < 0) return;
    if (UIManager* ui = UIManager::tryGet()) ui->setFocus(this);
    _scrollState.clearMomentum();
    _pressedCell = cell;
    _pressedIndex = cell->getIndex();
    _pressPosition = e.mousePos;
    _dragInProgress = false;

    uint32_t modifiers = 0;
    if (UIManager* ui = UIManager::tryGet()) modifiers = ui->getModifiers();
    const bool plain = !modifierDown(modifiers, UIKey_Shift)
        && !modifierDown(modifiers, UIKey_Control);
    _deferPlainCollapse = plain
        && _selectionMode == SelectionMode::Extended
        && _selectedIndices.size() > 1
        && isSelected(_pressedIndex);
    if (_deferPlainCollapse) {
        setFocusedIndex(_pressedIndex, false);
    } else {
        applyPointerSelection(_pressedIndex, modifiers);
    }
}

void TileView::handleCellMove(TileCell* cell, const UIMouseEvent& e) {
    if (!_dragEnabled || _dragInProgress || cell == nullptr
        || cell != _pressedCell || _pressedIndex < 0) {
        return;
    }
    const math::FVector2 delta = e.mousePos - _pressPosition;
    if (delta.x * delta.x + delta.y * delta.y
        < _dragThreshold * _dragThreshold) {
        return;
    }

    if (!isSelected(_pressedIndex)) {
        setSelectedIndex(_pressedIndex);
    }
    _activeDragIndices = _selectedIndices.empty()
        ? std::vector<int>{_pressedIndex} : _selectedIndices;
    cell->setDragPayload(buildDragPayload(_pressedIndex, _activeDragIndices));
    cell->setDraggable(true);
    if (UIManager* ui = UIManager::tryGet()) {
        if (!ui->beginDrag(cell)) _activeDragIndices.clear();
    }
}

void TileView::handleCellRelease(TileCell* cell, const UIMouseEvent& e) {
    const int pressedIndex = _pressedIndex;
    const bool wasDragging = _dragInProgress;
    const bool sameCell = cell != nullptr && cell == _pressedCell
        && cell->getIndex() == pressedIndex;
    _pressedCell = nullptr;
    _pressedIndex = -1;

    if (wasDragging || !sameCell || e.mouseButton != 0
        || !getClientRect().contains(e.mousePos)) {
        _deferPlainCollapse = false;
        return;
    }
    if (_deferPlainCollapse) setSelectedIndex(pressedIndex);
    _deferPlainCollapse = false;

    const TileCell::HitRegion region = cell->hitRegion(e.mousePos);
    if (region == TileCell::HitRegion::None) return;
    const math::FVector2 clickDelta = e.mousePos - _lastClickPosition;
    const bool isDouble = _doubleClickElapsed >= 0.0f
        && _doubleClickElapsed <= kDoubleClickSeconds
        && _lastClickIndex == pressedIndex
        && _lastClickRegion == region
        && clickDelta.x * clickDelta.x + clickDelta.y * clickDelta.y
            <= kDoubleClickDistance * kDoubleClickDistance;
    if (isDouble) {
        _doubleClickElapsed = -1.0f;
        _lastClickIndex = -1;
        _lastClickRegion = TileCell::HitRegion::None;
        // One semantic callback per gesture: the region-aware callback is
        // authoritative when installed; otherwise preserve the conventional
        // onItemActivated-on-double-click shortcut. This also avoids reading
        // TileView again after a host callback that may destroy it.
        if (_onItemDoubleClicked) {
            _onItemDoubleClicked(pressedIndex, region);
        } else {
            activateIndex(pressedIndex);
        }
    } else {
        _doubleClickElapsed = 0.0f;
        _lastClickIndex = pressedIndex;
        _lastClickRegion = region;
        _lastClickPosition = e.mousePos;
    }
}

DragPayload TileView::buildDragPayload(
    int pressedIndex, const std::vector<int>& indices) const {
    if (_dragPayloadBuilder) return _dragPayloadBuilder(pressedIndex, indices);
    DragPayload payload;
    payload.kind = "AYUI.TileItems";
    payload.data = const_cast<TileView*>(this);
    payload.userData = pressedIndex;
    if (indices.size() == 1 && pressedIndex >= 0
        && pressedIndex < static_cast<int>(_items.size())) {
        payload.text = _items[pressedIndex];
    } else {
        payload.text = std::to_wstring(indices.size()) + L" items";
    }
    return payload;
}

void TileView::handleDragStarted(TileCell* cell) {
    _dragInProgress = true;
    _deferPlainCollapse = false;
    if (cell != nullptr) cell->onMouseLeave();
    if (_onItemDragStarted) _onItemDragStarted(_activeDragIndices);
}

void TileView::handleDragFinished(TileCell* cell, bool accepted) {
    if (cell != nullptr) cell->onMouseLeave();
    const bool wasOurDrag = _dragInProgress;
    _dragInProgress = false;
    _pressedCell = nullptr;
    _pressedIndex = -1;
    _activeDragIndices.clear();
    if (wasOurDrag && _onItemDragFinished) {
        _onItemDragFinished(accepted);
    }
}

bool TileView::performAccessibilityAction(
    TileCell* cell, AccessibilityAction action) {
    if (cell == nullptr || cell->getIndex() < 0) return false;
    switch (action) {
    case AccessibilityAction::Focus:
        if (UIManager* ui = UIManager::tryGet()) ui->setFocus(this);
        setFocusedIndex(cell->getIndex(), true);
        return true;
    case AccessibilityAction::Select:
        setSelectedIndex(cell->getIndex());
        return true;
    case AccessibilityAction::Press:
        setSelectedIndex(cell->getIndex());
        activateIndex(cell->getIndex());
        return true;
    default:
        return false;
    }
}

void TileView::activateIndex(int index) {
    if (index >= 0 && index < static_cast<int>(_items.size())
        && _onItemActivated) {
        _onItemActivated(index);
    }
}

void TileView::applyKeyboardMove(int next, bool shift, bool ctrl) {
    if (next < 0 || next >= static_cast<int>(_items.size())) return;
    const int oldFocus = _focusedIndex;
    const int anchor = _anchorIndex >= 0 ? _anchorIndex
        : (oldFocus >= 0 ? oldFocus : next);
    setFocusedIndex(next, true);
    if (ctrl && !shift) return;
    if (shift && _selectionMode == SelectionMode::Extended) {
        const int lo = std::min(anchor, next);
        const int hi = std::max(anchor, next);
        std::vector<int> range(static_cast<size_t>(hi - lo + 1));
        std::iota(range.begin(), range.end(), lo);
        if (ctrl) {
            range.insert(range.end(), _selectedIndices.begin(),
                         _selectedIndices.end());
        }
        _anchorIndex = anchor;
        setSelectedIndices(range);
    } else {
        _anchorIndex = next;
        setSelectedIndices({next});
    }
}

bool TileView::onKeyDown(int keyCode) {
    if (_items.empty()) return false;
    uint32_t modifiers = 0;
    if (UIManager* ui = UIManager::tryGet()) modifiers = ui->getModifiers();
    const bool shift = modifierDown(modifiers, UIKey_Shift);
    const bool ctrl = modifierDown(modifiers, UIKey_Control);
    const int count = static_cast<int>(_items.size());
    const int current = _focusedIndex >= 0 ? _focusedIndex
        : (_selectedIndex >= 0 ? _selectedIndex : 0);

    if (keyCode == UIKey_F2) {
        if (_focusedIndex >= 0 && _onRenameRequested) {
            _onRenameRequested(_focusedIndex);
            return true;
        }
        return false;
    }
    if (keyCode == UIKey_Enter) {
        if (_focusedIndex >= 0) {
            activateIndex(_focusedIndex);
            return true;
        }
        return false;
    }
    if (keyCode == UIKey_Escape) {
        if (_selectedIndices.empty()) return false;
        clearSelection();
        return true;
    }
    if (ctrl && keyCode == UIKey_A) {
        if (_selectionMode == SelectionMode::Extended) {
            std::vector<int> all(static_cast<size_t>(count));
            std::iota(all.begin(), all.end(), 0);
            setSelectedIndices(all);
            if (_focusedIndex < 0) setFocusedIndex(0, true);
        } else {
            setSelectedIndex(0);
        }
        return true;
    }
    if (keyCode == UIKey_Space) {
        if (_selectionMode == SelectionMode::Extended && ctrl) {
            std::vector<int> next = _selectedIndices;
            const auto found = std::find(next.begin(), next.end(), current);
            if (found == next.end()) next.push_back(current);
            else next.erase(found);
            _anchorIndex = current;
            setSelectedIndices(next);
        } else {
            setSelectedIndex(current);
        }
        return true;
    }

    int next = current;
    const int columns = std::max(1, _columnCount);
    const int pageRows = std::max(1, computeVisibleRows() - 1);
    switch (keyCode) {
    case UIKey_Left:     next = std::max(0, current - 1); break;
    case UIKey_Right:    next = std::min(count - 1, current + 1); break;
    case UIKey_Up:       next = std::max(0, current - columns); break;
    case UIKey_Down:     next = std::min(count - 1, current + columns); break;
    case UIKey_Home:     next = 0; break;
    case UIKey_End:      next = count - 1; break;
    case UIKey_PageUp:
        next = std::max(0, current - columns * pageRows);
        break;
    case UIKey_PageDown:
        next = std::min(count - 1, current + columns * pageRows);
        break;
    default:
        return false;
    }
    applyKeyboardMove(next, shift, ctrl);
    return true;
}

void TileView::updateEdgeAutoScroll(float dt) {
    if (!_dragInProgress || dt <= 0.0f) return;
    UIManager* ui = UIManager::tryGet();
    if (ui == nullptr || !ui->isDragActive()) return;
    const math::FRectangle client = getClientRect();
    const math::FVector2 pointer = ui->getDragLastMousePos();
    if (pointer.x < client.minX || pointer.x > client.maxX
        || pointer.y < client.minY || pointer.y > client.maxY) {
        return;
    }

    const float edge = std::min(kAutoScrollEdge,
        std::max(1.0f, rectangleHeight(client) * 0.25f));
    float direction = 0.0f;
    if (pointer.y < client.minY + edge) {
        direction = -(client.minY + edge - pointer.y) / edge;
    } else if (pointer.y > client.maxY - edge) {
        direction = (pointer.y - (client.maxY - edge)) / edge;
    }
    if (std::fabs(direction) > 0.001f) {
        _scrollState.clearMomentum();
        scrollBy(direction * kAutoScrollMaxSpeed * dt);
    }
}

void TileView::tick(float dt) {
    CompoundFocusableWidget::tick(dt);
    if (_doubleClickElapsed >= 0.0f) {
        _doubleClickElapsed += std::max(0.0f, dt);
        if (_doubleClickElapsed > kDoubleClickSeconds) {
            _doubleClickElapsed = -1.0f;
            _lastClickIndex = -1;
            _lastClickRegion = TileCell::HitRegion::None;
        }
    }

    updateEdgeAutoScroll(dt);
    if (!_dragInProgress) {
        const math::FRectangle client = getClientRect();
        math::FVector2 delta;
        if (_scrollState.advanceMomentum(
                dt, math::FVector2(rectangleWidth(client), getHeight()), delta)) {
            scrollBy(delta.y);
        }
    }
}

Widget* createTileViewWidget() {
    return new TileView();
}

} // namespace ayt::ui
