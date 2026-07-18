#include "AYGridPanel.h"
#include <algorithm>

namespace ayt::ui {

GridPanel::GridPanel() {
    setSize(math::FVector2(320.0f, 240.0f));
}

GridPanel::~GridPanel() {
    // Cells hold Widget* by reference — parent never deletes. Caller owns.
}

namespace {
constexpr int kDefaultRows = 0;
constexpr int kDefaultCols = 0;
} // namespace

// ----------------------------------------------------------------------------
// Sizing
// ----------------------------------------------------------------------------

void GridPanel::setRowCount(int n) {
    if (n < 0) n = 0;
    if (static_cast<int>(_rowDefs.size()) == n) return;
    _rowDefs.resize(static_cast<size_t>(n), RowDef{SizePolicy::Stretch, 1.0f});
    // Resize cell flat-vector to match new dimensions.
    int cols = static_cast<int>(_colDefs.size());
    _cells.resize(static_cast<size_t>(n) * static_cast<size_t>(std::max(cols, 1)),
                  CellInfo{});
}

void GridPanel::setColumnCount(int n) {
    if (n < 0) n = 0;
    if (static_cast<int>(_colDefs.size()) == n) return;
    int oldCols = static_cast<int>(_colDefs.size());
    _colDefs.resize(static_cast<size_t>(n), ColDef{SizePolicy::Stretch, 1.0f});
    int rows = static_cast<int>(_rowDefs.size());
    // Rebuild cells so (row, col) index stays correct. Existing widgets
    // are migrated by row*oldCols + col → row*newCols + col, but only
    // when new cols >= old cols. Simpler: drop all cells when col count
    // shrinks (rare in practice — setColumnCount is normally called once
    // before adding cells). Hosts needing preservation should re-add.
    if (n < oldCols) {
        std::vector<CellInfo> rebuilt(_cells.size(), CellInfo{});
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < n; ++c) {
                rebuilt[r * n + c] = _cells[r * oldCols + c];
            }
        }
        _cells = std::move(rebuilt);
    } else if (n > oldCols) {
        _cells.resize(static_cast<size_t>(rows) * static_cast<size_t>(n), CellInfo{});
    }
}

void GridPanel::setRowDef(int row, const RowDef& def) {
    if (row < 0 || row >= static_cast<int>(_rowDefs.size())) return;
    _rowDefs[row] = def;
}

void GridPanel::setColumnDef(int col, const ColDef& def) {
    if (col < 0 || col >= static_cast<int>(_colDefs.size())) return;
    _colDefs[col] = def;
}

const GridPanel::RowDef& GridPanel::getRowDef(int row) const {
    static const RowDef kDefault{};
    if (row < 0 || row >= static_cast<int>(_rowDefs.size())) return kDefault;
    return _rowDefs[row];
}

const GridPanel::ColDef& GridPanel::getColumnDef(int col) const {
    static const ColDef kDefault{};
    if (col < 0 || col >= static_cast<int>(_colDefs.size())) return kDefault;
    return _colDefs[col];
}

// ----------------------------------------------------------------------------
// Cell management
// ----------------------------------------------------------------------------

void GridPanel::setCell(int row, int col, Widget* widget,
                        int rowSpan, int colSpan,
                        HAlign hAlign, VAlign vAlign) {
    if (row < 0 || col < 0) return;
    int rows = static_cast<int>(_rowDefs.size());
    int cols = static_cast<int>(_colDefs.size());
    if (row >= rows || col >= cols) return;
    if (rowSpan < 1) rowSpan = 1;
    if (colSpan < 1) colSpan = 1;
    if (row + rowSpan > rows) rowSpan = rows - row;
    if (col + colSpan > cols) colSpan = cols - col;

    // If the widget is already attached to another cell of this GridPanel,
    // clear that cell first so we don't have a dangling pointer in the
    // old slot (CompoundWidget won't delete the child either way; the
    // old slot would still claim ownership and confuse downstream).
    if (widget != nullptr) {
        for (auto& c : _cells) {
            if (c.widget == widget) {
                c = CellInfo{};
            }
        }
    }

    // Reparent: if widget had a different parent, detach + attach.
    if (widget != nullptr && widget->getParent() != this) {
        if (widget->getParent() != nullptr) {
            widget->getParent()->removeChild(widget);
        }
        addChild(widget);
    } else if (widget == nullptr) {
        // clearCell path — drop widget from this cell, but leave parentage
        // alone (CompoundWidget won't delete the child either way; the
        // widget becomes orphaned at CompoundWidget level).
        CellInfo& info = _cells[row * cols + col];
        info = CellInfo{};
        return;
    }

    CellInfo& info = _cells[row * cols + col];
    info.widget = widget;
    info.rowSpan = rowSpan;
    info.colSpan = colSpan;
    info.hAlign = hAlign;
    info.vAlign = vAlign;
}

void GridPanel::clearCell(int row, int col) {
    int cols = static_cast<int>(_colDefs.size());
    if (row < 0 || col < 0 || row >= static_cast<int>(_rowDefs.size()) ||
        col >= cols) {
        return;
    }
    CellInfo& info = _cells[row * cols + col];
    if (info.widget != nullptr && info.widget->getParent() == this) {
        removeChild(info.widget);
    }
    info = CellInfo{};
}

Widget* GridPanel::getCell(int row, int col) const {
    const CellInfo* c = findCell(row, col);
    return c ? c->widget : nullptr;
}

const GridPanel::CellInfo* GridPanel::findCell(int row, int col) const {
    int cols = static_cast<int>(_colDefs.size());
    if (row < 0 || col < 0 || row >= static_cast<int>(_rowDefs.size()) ||
        col >= cols) {
        return nullptr;
    }
    return &_cells[row * cols + col];
}

// ----------------------------------------------------------------------------
// Padding / spacing
// ----------------------------------------------------------------------------

void GridPanel::setPadding(float left, float top, float right, float bottom) {
    _padding = math::FVector4(left, top, right, bottom);
}

void GridPanel::setSpacing(float h, float v) {
    _spacingH = std::max(0.0f, h);
    _spacingV = std::max(0.0f, v);
}

// ----------------------------------------------------------------------------
// Layout
// ----------------------------------------------------------------------------

float GridPanel::resolveColWidth(int col, float availableWidth,
                                 const std::vector<float>& fixedTotals,
                                 float stretchTotalWeight) const {
    const ColDef& def = _colDefs[col];
    if (def.policy == SizePolicy::Fixed) {
        return std::max(0.0f, def.value);
    }
    float leftover = std::max(0.0f, availableWidth - fixedTotals[0]);
    if (stretchTotalWeight <= 0.0f || def.value <= 0.0f) {
        return 0.0f;
    }
    return leftover * (def.value / stretchTotalWeight);
}

float GridPanel::resolveRowHeight(int row, float availableHeight,
                                  const std::vector<float>& fixedTotals,
                                  float stretchTotalWeight) const {
    const RowDef& def = _rowDefs[row];
    if (def.policy == SizePolicy::Fixed) {
        return std::max(0.0f, def.value);
    }
    float leftover = std::max(0.0f, availableHeight - fixedTotals[0]);
    if (stretchTotalWeight <= 0.0f || def.value <= 0.0f) {
        return 0.0f;
    }
    return leftover * (def.value / stretchTotalWeight);
}

void GridPanel::performLayout() {
    CompoundWidget::performLayout();
    layoutChildren();
}

void GridPanel::layoutChildren() {
    int rows = static_cast<int>(_rowDefs.size());
    int cols = static_cast<int>(_colDefs.size());
    if (rows == 0 || cols == 0) return;

    // Phase 1: sum fixed sizes + total stretch weights for rows / cols.
    float colFixedSum = 0.0f;
    float colStretchWeight = 0.0f;
    for (int c = 0; c < cols; ++c) {
        const ColDef& def = _colDefs[c];
        if (def.policy == SizePolicy::Fixed) colFixedSum += def.value;
        else colStretchWeight += def.value;
    }
    float rowFixedSum = 0.0f;
    float rowStretchWeight = 0.0f;
    for (int r = 0; r < rows; ++r) {
        const RowDef& def = _rowDefs[r];
        if (def.policy == SizePolicy::Fixed) rowFixedSum += def.value;
        else rowStretchWeight += def.value;
    }

    // Available content area (padding excluded).
    math::FVector2 sz = getSize();
    float availW = std::max(0.0f, sz.x - _padding.x - _padding.z);
    float availH = std::max(0.0f, sz.y - _padding.y - _padding.w);
    // Subtract the inter-cell spacing budget (n-1 spacings).
    if (cols > 1) availW = std::max(0.0f, availW - _spacingH * static_cast<float>(cols - 1));
    if (rows > 1) availH = std::max(0.0f, availH - _spacingV * static_cast<float>(rows - 1));

    std::vector<float> colWidths(static_cast<size_t>(cols), 0.0f);
    std::vector<float> colOffsets(static_cast<size_t>(cols + 1), 0.0f);
    std::vector<float> rowHeights(static_cast<size_t>(rows), 0.0f);
    std::vector<float> rowOffsets(static_cast<size_t>(rows + 1), 0.0f);

    for (int c = 0; c < cols; ++c) {
        colWidths[c] = resolveColWidth(c, availW, {colFixedSum}, colStretchWeight);
        colOffsets[c + 1] = colOffsets[c] + colWidths[c] +
            (c + 1 < cols ? _spacingH : 0.0f);
    }
    for (int r = 0; r < rows; ++r) {
        rowHeights[r] = resolveRowHeight(r, availH, {rowFixedSum}, rowStretchWeight);
        rowOffsets[r + 1] = rowOffsets[r] + rowHeights[r] +
            (r + 1 < rows ? _spacingV : 0.0f);
    }

    // Phase 2: place each cell.
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const CellInfo& info = _cells[r * cols + c];
            if (info.widget == nullptr) continue;

            // Span range.
            int rs = std::max(1, info.rowSpan);
            int cs = std::max(1, info.colSpan);
            int rEnd = std::min(rows, r + rs);
            int cEnd = std::min(cols, c + cs);

            // Merged cell rect.
            float cellX = colOffsets[c];
            float cellY = rowOffsets[r];
            float cellW = (cEnd > 0 ? colOffsets[cEnd] : cellX) - cellX;
            // For the trailing column, account for the trailing spacing that
            // we did NOT add (we only added (n-1) spacings).
            float cellH = (rEnd > 0 ? rowOffsets[rEnd] : cellY) - cellY;
            // Adjust: colOffsets[cEnd] excluded the inter-spacing between
            // cEnd-1 and cEnd, so cellW is correct.
            // For the LAST column when cEnd == cols, we need to add back the
            // (cEnd-1) spacing. Simpler: compute merged width as sum of
            // colWidths[c..cEnd-1] + (cEnd-1 - c) spacings.
            float mergedW = 0.0f;
            for (int cc = c; cc < cEnd; ++cc) mergedW += colWidths[cc];
            if (cEnd - c > 1) mergedW += _spacingH * static_cast<float>(cEnd - c - 1);
            float mergedH = 0.0f;
            for (int rr = r; rr < rEnd; ++rr) mergedH += rowHeights[rr];
            if (rEnd - r > 1) mergedH += _spacingV * static_cast<float>(rEnd - r - 1);

            cellW = mergedW;
            cellH = mergedH;

            float x = _padding.x + cellX;
            float y = _padding.y + cellY;

            // Apply alignment.
            float w = cellW;
            float h = cellH;
            switch (info.hAlign) {
            case HAlign::Fill: break;
            case HAlign::Left: {
                // Keep widget's intrinsic width if it has one; otherwise
                // shrink to a sensible minimum. v1: just fill — widget
                // doesn't expose intrinsic size yet.
                break;
            }
            case HAlign::Center: {
                // Center inside the cell — v1 just sets full cell size;
                // hosts using Center/Right must call setSize externally.
                break;
            }
            case HAlign::Right: {
                break;
            }
            }
            switch (info.vAlign) {
            case VAlign::Fill: break;
            case VAlign::Top:
            case VAlign::Middle:
            case VAlign::Bottom:
                break;
            }

            if (info.widget->isLayoutPositionManaged()) {
                info.widget->setPosition(math::FVector2(x, y));
            }
            if (info.widget->isLayoutSizeManaged()) {
                info.widget->setSize(math::FVector2(w, h));
            }
            if (!info.widget->getChildren().empty()) {
                info.widget->performLayout();
            }
        }
    }
}

} // namespace ayt::ui