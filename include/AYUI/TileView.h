#pragma once

#include "AYUI/CompoundFocusableWidget.h"
#include "AYUI/ImageTexture.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/ScrollableWidget.h"
#include "AYUI/SelectableWidget.h"

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace ayt::ui {

class TileView;

// A recycled visual slot owned by TileView. TileCell is deliberately a small
// presentation object, not the item model: scrolling only rebinds these cells
// to different logical indices.
class TileCell : public SelectableWidget {
public:
    enum class HitRegion {
        None,
        Body,
        Thumbnail,
        Label,
    };

    TileCell();
    ~TileCell() override;

    int getIndex() const { return _index; }
    const std::wstring& getText() const { return _text; }
    const std::wstring& getSecondaryText() const { return _secondaryText; }
    const std::wstring& getBadgeText() const { return _badgeText; }

    void setText(const std::wstring& text);
    void setSecondaryText(const std::wstring& text);
    void setBadgeText(const std::wstring& text);

    // The cell does not own the texture. A named or anonymous handle remains
    // managed by the host/TextureRegistry; the visible cell only submits it.
    void setThumbnail(const ImageTextureHandle& texture);
    const ImageTextureHandle& getThumbnail() const { return _thumbnail; }
    void clearThumbnail();
    void setThumbnailUV(const math::FRectangle& uv);
    const math::FRectangle& getThumbnailUV() const { return _thumbnailUv; }

    void setAccentColor(const math::FVector4& color);
    const math::FVector4& getAccentColor() const { return _accentColor; }

    // Generic, host-authored presentation. TileCell deliberately does not
    // interpret the text or colors as a resource/category type.
    void setInfoStrip(const std::wstring& text,
                      const math::FVector4& backgroundColor,
                      const math::FVector4& textColor);
    void clearInfoStrip();
    const std::wstring& getInfoStripText() const { return _infoStripText; }
    const math::FVector4& getInfoStripColor() const {
        return _infoStripColor;
    }
    const math::FVector4& getInfoStripTextColor() const {
        return _infoStripTextColor;
    }
    math::FRectangle getInfoStripRect() const;

    void setCornerMarkerVisible(bool visible);
    bool isCornerMarkerVisible() const { return _cornerMarkerVisible; }
    void setCornerMarkerColor(const math::FVector4& color);
    const math::FVector4& getCornerMarkerColor() const {
        return _cornerMarkerColor;
    }
    math::FRectangle getCornerMarkerBounds() const;

    bool isCurrent() const { return _current; }
    HitRegion hitRegion(const math::FVector2& worldPos) const;
    math::FRectangle getThumbnailRect() const;
    math::FRectangle getLabelRect() const;

    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onRender(IRenderBackend& renderer) override;

private:
    void bindOwner(TileView* owner, int index, float labelHeight,
                   float infoStripHeight, float cornerMarkerSize,
                   float thumbnailAspectRatio);
    void resetPresentation(const std::wstring& text);
    void setCurrent(bool current);
    void updateDisplayText(IRenderBackend& renderer, float availableWidth);

    TileView* _owner = nullptr;
    int _index = -1;
    float _labelHeight = 34.0f;
    float _infoStripHeight = 16.0f;
    float _cornerMarkerSize = 12.0f;
    float _thumbnailAspectRatio = 1.0f;
    bool _current = false;

    std::wstring _text;
    std::wstring _secondaryText;
    // Render-only elision cache. Accessibility, activation and rename paths
    // continue to expose _text/_secondaryText in full.
    std::wstring _displayText;
    std::wstring _displaySecondaryText;
    float _displayTextWidth = -1.0f;
    std::wstring _badgeText;
    ImageTextureHandle _thumbnail;
    math::FRectangle _thumbnailUv{0.0f, 0.0f, 1.0f, 1.0f};
    math::FVector4 _accentColor{0.32f, 0.58f, 0.92f, 1.0f};
    std::wstring _infoStripText;
    math::FVector4 _infoStripColor{0.32f, 0.58f, 0.92f, 1.0f};
    math::FVector4 _infoStripTextColor{1.0f, 1.0f, 1.0f, 1.0f};
    bool _cornerMarkerVisible = false;
    math::FVector4 _cornerMarkerColor{0.92f, 0.68f, 0.20f, 1.0f};

    friend class TileView;
};

class TileView : public CompoundFocusableWidget {
public:
    enum class SelectionMode {
        Single,
        Extended,
    };

    using CellBinder =
        std::function<void(TileCell&, int, const std::wstring&)>;
    using DragPayloadBuilder =
        std::function<DragPayload(int, const std::vector<int>&)>;

    TileView();
    ~TileView() override;

    // Data. The string vector is the stable built-in model; richer hosts use
    // CellBinder to project their model onto the few currently pooled cells.
    void setItems(const std::vector<std::wstring>& items);
    void addItem(const std::wstring& item);
    void clearItems();
    size_t getItemCount() const { return _items.size(); }
    const std::wstring& getItem(size_t index) const;
    const std::vector<std::wstring>& getItemsRef() const { return _items; }

    void setCellBinder(CellBinder binder);

    // Grid metrics, expressed in logical UI units (DIP).
    void setTileSize(const math::FVector2& size);
    const math::FVector2& getTileSize() const { return _tileSize; }
    void setTileSpacing(float spacing);
    float getTileSpacing() const { return _tileSpacing; }
    void setContentPadding(float padding);
    float getContentPadding() const { return _contentPadding; }
    void setLabelHeight(float height);
    float getLabelHeight() const { return _labelHeight; }
    void setInfoStripHeight(float height);
    float getInfoStripHeight() const { return _infoStripHeight; }
    void setCornerMarkerSize(float size);
    float getCornerMarkerSize() const { return _cornerMarkerSize; }
    void setThumbnailAspectRatio(float ratio);
    float getThumbnailAspectRatio() const { return _thumbnailAspectRatio; }
    void setOverscanRows(int rows);
    int getOverscanRows() const { return _overscanRows; }

    int getColumnCount() const { return _columnCount; }
    int getLogicalRowCount() const { return _logicalRowCount; }

    // Selection and keyboard-focus cursor are separate. In Extended mode,
    // Ctrl+arrow moves only focusedIndex while Shift+arrow grows a range.
    void setSelectionMode(SelectionMode mode);
    SelectionMode getSelectionMode() const { return _selectionMode; }
    void setSelectedIndex(int index);
    int getSelectedIndex() const { return _selectedIndex; }
    void setSelectedIndices(const std::vector<int>& indices);
    const std::vector<int>& getSelectedIndices() const {
        return _selectedIndices;
    }
    bool isSelected(int index) const;
    void clearSelection();

    void setFocusedIndex(int index, bool scrollIntoView = true);
    int getFocusedIndex() const { return _focusedIndex; }
    int getAnchorIndex() const { return _anchorIndex; }

    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }
    void setOnSelectionIndicesChanged(
        std::function<void(const std::vector<int>&)> cb) {
        _onSelectionIndicesChanged = std::move(cb);
    }
    void setOnItemActivated(std::function<void(int)> cb) {
        _onItemActivated = std::move(cb);
    }
    void setOnRenameRequested(std::function<void(int)> cb) {
        _onRenameRequested = std::move(cb);
    }
    void setOnItemDoubleClicked(
        std::function<void(int, TileCell::HitRegion)> cb) {
        _onItemDoubleClicked = std::move(cb);
    }

    // Drag source integration. The default payload kind is AYUI.TileItems,
    // data points at this TileView, and userData is the pressed index. A host
    // can replace the payload builder (for example with a FileList payload).
    void setDragEnabled(bool enabled);
    bool isDragEnabled() const { return _dragEnabled; }
    void setDragThreshold(float pixels);
    float getDragThreshold() const { return _dragThreshold; }
    void setDragPayloadBuilder(DragPayloadBuilder builder) {
        _dragPayloadBuilder = std::move(builder);
    }
    void setOnItemDragStarted(
        std::function<void(const std::vector<int>&)> cb) {
        _onItemDragStarted = std::move(cb);
    }
    void setOnItemDragFinished(std::function<void(bool)> cb) {
        _onItemDragFinished = std::move(cb);
    }
    const std::vector<int>& getActiveDragIndices() const {
        return _activeDragIndices;
    }

    // Virtualization inspection and lookup.
    size_t getCellPoolSize() const { return _cellPool.size(); }
    int getCellPoolLogicalIndex(size_t slot) const;
    int getFirstPooledIndex() const { return _firstPooledIndex; }
    TileCell* cellForLogicalIndex(int index) const;
    int indexAt(const math::FVector2& worldPos) const;
    TileCell::HitRegion regionAt(const math::FVector2& worldPos) const;

    const math::FVector2& getScrollOffset() const {
        return _scrollState.getScrollOffset();
    }
    void setScrollOffset(const math::FVector2& offset);
    bool scrollBy(float deltaY);
    void scrollToIndex(int index);
    ScrollBar* getVerticalScrollBar() const { return _vbar; }
    void setOnScroll(std::function<void(const math::FVector2&)> cb) {
        _onScroll = std::move(cb);
    }

    void performLayout() override;
    void layoutChildren() override;
    math::FRectangle getClientRect() const override;
    void onRender(IRenderBackend& renderer) override;
    void renderChildren(IRenderBackend& renderer) override;
    Widget* hitTest(const math::FVector2& worldPos) override;

    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    bool onMouseWheel(const UIMouseWheelEvent& e) override;
    bool onKeyDown(int keyCode) override;
    void tick(float dt) override;

private:
    struct GridMetrics {
        int columns = 1;
        int rows = 0;
        float contentWidth = 0.0f;
        float contentHeight = 0.0f;
        bool needsVerticalBar = false;
    };

    GridMetrics computeGridMetrics() const;
    int computePoolSize() const;
    int computeVisibleRows() const;
    void ensureBarCreated();
    void syncBarToOffset();
    void rebuildCellPool();
    void rebindCellPool();
    void updateCellStates();
    void destroyCellPool();

    void handleCellPress(TileCell* cell, const UIMouseEvent& e);
    void handleCellMove(TileCell* cell, const UIMouseEvent& e);
    void handleCellRelease(TileCell* cell, const UIMouseEvent& e);
    void handleDragStarted(TileCell* cell);
    void handleDragFinished(TileCell* cell, bool accepted);
    bool performAccessibilityAction(TileCell* cell, AccessibilityAction action);
    void applyPointerSelection(int index, uint32_t modifiers);
    void applyKeyboardMove(int next, bool shift, bool ctrl);
    void activateIndex(int index);
    void updateEdgeAutoScroll(float dt);
    DragPayload buildDragPayload(int pressedIndex,
                                 const std::vector<int>& indices) const;

    std::vector<std::wstring> _items;
    CellBinder _cellBinder;

    math::FVector2 _tileSize{104.0f, 112.0f};
    float _tileSpacing = 8.0f;
    float _contentPadding = 4.0f;
    float _labelHeight = 34.0f;
    float _infoStripHeight = 16.0f;
    float _cornerMarkerSize = 12.0f;
    float _thumbnailAspectRatio = 1.0f;
    int _overscanRows = 1;

    int _columnCount = 1;
    int _logicalRowCount = 0;
    int _firstPooledIndex = 0;
    std::vector<TileCell*> _cellPool;

    SelectionMode _selectionMode = SelectionMode::Single;
    std::vector<int> _selectedIndices;
    std::unordered_set<int> _selectedIndexSet;
    int _selectedIndex = -1;
    int _focusedIndex = -1;
    int _anchorIndex = -1;

    ScrollBar* _vbar = nullptr;
    ScrollableWidget _scrollState;
    math::FVector2 _contentSize{0.0f, 0.0f};

    bool _dragEnabled = true;
    float _dragThreshold = 5.0f;
    TileCell* _pressedCell = nullptr;
    int _pressedIndex = -1;
    math::FVector2 _pressPosition{0.0f, 0.0f};
    bool _dragInProgress = false;
    bool _deferPlainCollapse = false;
    std::vector<int> _activeDragIndices;
    DragPayloadBuilder _dragPayloadBuilder;

    static constexpr float kDoubleClickSeconds = 0.4f;
    static constexpr float kDoubleClickDistance = 4.0f;
    float _doubleClickElapsed = -1.0f;
    int _lastClickIndex = -1;
    TileCell::HitRegion _lastClickRegion = TileCell::HitRegion::None;
    math::FVector2 _lastClickPosition{0.0f, 0.0f};

    std::function<void(int)> _onSelectionChanged;
    std::function<void(const std::vector<int>&)> _onSelectionIndicesChanged;
    std::function<void(int)> _onItemActivated;
    std::function<void(int)> _onRenameRequested;
    std::function<void(int, TileCell::HitRegion)> _onItemDoubleClicked;
    std::function<void(const std::vector<int>&)> _onItemDragStarted;
    std::function<void(bool)> _onItemDragFinished;
    std::function<void(const math::FVector2&)> _onScroll;

    friend class TileCell;
};

Widget* createTileViewWidget();

} // namespace ayt::ui
