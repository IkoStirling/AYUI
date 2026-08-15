#include "AYTest.h"
#include "AYUI/GridPanel.h"
#include "AYUI/Panel.h"
#include "AYUI/TextLabel.h"
#include "AYUI/Button.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Style.h"
#include <iostream>

// =============================================================================
// Known-not-covered scenarios for C-8 GridPanel v1
// =============================================================================
// See Layout/AYGridPanel.h top-of-file "v1 design decisions" block for
// design rationale + upgrade paths. Pinned here as entry points.
//
// G1. Auto-grow on addCell(row = current_row_count).
//     v1 setCell bounds-checks row < rowCount and silently drops cells
//     outside the grid. v1.1 fix: addCellAuto appends a new row when the
//     caller passes row == rowCount. Useful for dynamic content panels.
//
// G2. SizePolicy::Auto (measure pass over widget preferredSize).
//     v1 deliberately does NOT auto-measure (DECISION 2 in header) — every
//     cell fills its allocated space. v1.1: add Widget::getPreferredSize
//     + a third policy that picks the column's max preferred width.
//
// G3. Lossy round-trip for spanned cells.
//     v1 serializer does NOT preserve rowSpan/colSpan (DECISION 5 +
//     serializer note in AYWidgetSerializer.cpp). Deserialized cells are
//     1×1 Fill. v1.1 fix: emit `cells[] { row, col, rowSpan, colSpan,
//     hAlign, vAlign, content }` and rebuild on load.
//
// G4. GridSplitter (resizable row/col borders).
//     v1 has none. v1.1: introduce GridSplitter widget bound to (row|col)
//     index. Mirrors SplitterHandle's role in HBox.
//
// G5. z-order / overlap rules.
//     v1 widgets paint in tree order (same as VBox/HBox). v1.1:
//     GridPanel::setCellZOrder / bringToFront.
// =============================================================================

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_GridPanel)

// C-8: default state — no rows, no cols, no cells.
TEST_CASE(gridpanel_initial_state) {
    GridPanel gp;
    CHECK(gp.getRowCount() == 0);
    CHECK(gp.getColumnCount() == 0);
    CHECK(gp.getCell(0, 0) == nullptr);
    CHECK(gp.getHorizontalSpacing() == 4.0f);
    CHECK(gp.getVerticalSpacing() == 4.0f);
}

// C-8: setRowCount / setColumnCount + getRowDef / getColumnDef.
TEST_CASE(gridpanel_sizing) {
    GridPanel gp;
    gp.setRowCount(3);
    gp.setColumnCount(2);
    CHECK(gp.getRowCount() == 3);
    CHECK(gp.getColumnCount() == 2);

    // Defaults are Stretch, weight 1.0.
    CHECK(gp.getRowDef(0).policy == GridPanel::SizePolicy::Stretch);
    CHECK_FLOAT_EQ(gp.getRowDef(0).value, 1.0f, 1e-5f);
    CHECK(gp.getColumnDef(1).policy == GridPanel::SizePolicy::Stretch);

    // Override one.
    gp.setRowDef(0, GridPanel::RowDef{GridPanel::SizePolicy::Fixed, 24.0f});
    CHECK(gp.getRowDef(0).policy == GridPanel::SizePolicy::Fixed);
    CHECK_FLOAT_EQ(gp.getRowDef(0).value, 24.0f, 1e-5f);
}

// C-8: setCell re-parents the widget + stores CellInfo.
TEST_CASE(gridpanel_set_cell_reparents) {
    GridPanel gp;
    gp.setRowCount(2);
    gp.setColumnCount(2);

    Panel* p = new Panel();
    gp.setCell(0, 0, p);
    CHECK(gp.getCell(0, 0) == p);
    CHECK(p->getParent() == &gp);
    CHECK(gp.getChildren().size() == 1u);

    gp.setCell(1, 1, p);   // move to a different cell
    CHECK(gp.getCell(1, 1) == p);
    CHECK(gp.getCell(0, 0) == nullptr);
    CHECK(gp.getChildren().size() == 1u);
}

// C-8: clearCell detaches the widget.
TEST_CASE(gridpanel_clear_cell) {
    GridPanel gp;
    gp.setRowCount(2);
    gp.setColumnCount(2);

    Panel* p = new Panel();
    gp.setCell(0, 0, p);
    CHECK(gp.getCell(0, 0) == p);

    gp.clearCell(0, 0);
    CHECK(gp.getCell(0, 0) == nullptr);
    // Parentage is dropped.
    CHECK(p->getParent() != &gp);
    // Caller owns the widget — destroy separately.
    destroyWidgetTree(p);
}

// C-8: layout — 2×2 stretch grid, all 4 cells filled.
TEST_CASE(gridpanel_layout_2x2_stretch) {
    GridPanel gp;
    gp.setRowCount(2);
    gp.setColumnCount(2);
    gp.setSize(FVector2(200.0f, 100.0f));
    gp.setPosition(FVector2(0.0f, 0.0f));
    // Default padding (4,4,4,4) + spacing (4,4).
    // content area = (192, 92); minus spacing budget (col-1=1, row-1=1)
    //   = (188, 88). Each stretch col gets 94; each stretch row gets 44.
    Panel* a = new Panel(); gp.setCell(0, 0, a);
    Panel* b = new Panel(); gp.setCell(0, 1, b);
    Panel* c = new Panel(); gp.setCell(1, 0, c);
    Panel* d = new Panel(); gp.setCell(1, 1, d);

    gp.performLayout();

    // (0, 0): x = padding.x = 4; y = padding.y = 4.
    CHECK_FLOAT_EQ(a->getPosition().x, 4.0f, 0.5f);
    CHECK_FLOAT_EQ(a->getPosition().y, 4.0f, 0.5f);
    CHECK_FLOAT_EQ(a->getSize().x, 94.0f, 0.5f);
    CHECK_FLOAT_EQ(a->getSize().y, 44.0f, 0.5f);

    // (0, 1): x = 4 + 94 + 4(spacing) = 102; y = 4.
    CHECK_FLOAT_EQ(b->getPosition().x, 102.0f, 0.5f);
    CHECK_FLOAT_EQ(b->getPosition().y, 4.0f, 0.5f);

    // (1, 0): x = 4; y = 4 + 44 + 4 = 52.
    CHECK_FLOAT_EQ(c->getPosition().x, 4.0f, 0.5f);
    CHECK_FLOAT_EQ(c->getPosition().y, 52.0f, 0.5f);

    // (1, 1): x = 102; y = 52.
    CHECK_FLOAT_EQ(d->getPosition().x, 102.0f, 0.5f);
    CHECK_FLOAT_EQ(d->getPosition().y, 52.0f, 0.5f);
}

// C-8: Fixed row + Stretch row + mixed weight Stretch.
TEST_CASE(gridpanel_layout_fixed_and_stretch) {
    GridPanel gp;
    gp.setRowCount(2);
    gp.setColumnCount(2);
    gp.setSize(FVector2(200.0f, 100.0f));
    gp.setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    gp.setSpacing(0.0f, 0.0f);

    // Row 0 fixed at 30; Row 1 stretches into 70 leftover.
    gp.setRowDef(0, GridPanel::RowDef{GridPanel::SizePolicy::Fixed, 30.0f});
    // Col 0 fixed at 60; Col 1 stretches into 140 leftover.
    gp.setColumnDef(0, GridPanel::ColDef{GridPanel::SizePolicy::Fixed, 60.0f});

    Panel* a = new Panel(); gp.setCell(0, 0, a);
    Panel* b = new Panel(); gp.setCell(0, 1, b);
    Panel* c = new Panel(); gp.setCell(1, 0, c);
    Panel* d = new Panel(); gp.setCell(1, 1, d);

    gp.performLayout();

    CHECK_FLOAT_EQ(a->getSize().x, 60.0f, 0.5f);
    CHECK_FLOAT_EQ(a->getSize().y, 30.0f, 0.5f);
    CHECK_FLOAT_EQ(b->getSize().x, 140.0f, 0.5f);
    CHECK_FLOAT_EQ(b->getSize().y, 30.0f, 0.5f);
    CHECK_FLOAT_EQ(c->getSize().x, 60.0f, 0.5f);
    CHECK_FLOAT_EQ(c->getSize().y, 70.0f, 0.5f);
    CHECK_FLOAT_EQ(d->getSize().x, 140.0f, 0.5f);
    CHECK_FLOAT_EQ(d->getSize().y, 70.0f, 0.5f);

    // Y offset for row 1 = 30.
    CHECK_FLOAT_EQ(c->getPosition().y, 30.0f, 0.5f);
    // X offset for col 1 = 60.
    CHECK_FLOAT_EQ(b->getPosition().x, 60.0f, 0.5f);
}

// C-8: rowSpan / colSpan merge cells.
TEST_CASE(gridpanel_layout_spans_merge_cells) {
    GridPanel gp;
    gp.setRowCount(2);
    gp.setColumnCount(2);
    gp.setSize(FVector2(200.0f, 100.0f));
    gp.setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    gp.setSpacing(0.0f, 0.0f);

    Panel* wide = new Panel();
    gp.setCell(0, 0, wide, /*rowSpan=*/1, /*colSpan=*/2);   // spans 2 cols

    gp.performLayout();
    CHECK_FLOAT_EQ(wide->getPosition().x, 0.0f, 0.5f);
    CHECK_FLOAT_EQ(wide->getPosition().y, 0.0f, 0.5f);
    CHECK_FLOAT_EQ(wide->getSize().x, 200.0f, 0.5f);
    CHECK_FLOAT_EQ(wide->getSize().y, 50.0f, 0.5f);
}

// C-8: factory + serializer round-trip — v1 loses rowSpan/colSpan (G3 pin)
// but preserves rowCount/columnCount + children.
TEST_CASE(gridpanel_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("GridPanel"));

    Widget* widget = factory.create("GridPanel");
    CHECK_NOT_NULL(widget);
    GridPanel* original = dynamic_cast<GridPanel*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("gp_form");
    original->setRowCount(2);
    original->setColumnCount(2);
    Panel* a = new Panel(); a->setId("cell_a");
    Panel* b = new Panel(); b->setId("cell_b");
    original->setCell(0, 0, a);
    original->setCell(0, 1, b);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"GridPanel\"") != std::string::npos);
    CHECK(json.find("\"rowCount\": 2") != std::string::npos);
    CHECK(json.find("\"columnCount\": 2") != std::string::npos);
    CHECK(json.find("cell_a") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    GridPanel* restoredGp = dynamic_cast<GridPanel*>(restored);
    CHECK_NOT_NULL(restoredGp);
    CHECK(restoredGp->getRowCount() == 2);
    CHECK(restoredGp->getColumnCount() == 2);
    // Children are placed left-to-right, top-to-bottom by the linear attach
    // path in the deserializer.
    CHECK_NOT_NULL(restoredGp->getCell(0, 0));
    CHECK_NOT_NULL(restoredGp->getCell(0, 1));

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// C-8: render emits cell panels' rects (each Panel draws bg + border).
TEST_CASE(gridpanel_render_emits_cells) {
    GridPanel gp;
    gp.setRowCount(2);
    gp.setColumnCount(2);
    gp.setSize(FVector2(160.0f, 80.0f));
    gp.setPosition(FVector2(0.0f, 0.0f));
    gp.setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    gp.setSpacing(0.0f, 0.0f);

    Panel* a = new Panel(); gp.setCell(0, 0, a);
    Panel* b = new Panel(); gp.setCell(0, 1, b);
    Panel* c = new Panel(); gp.setCell(1, 0, c);
    Panel* d = new Panel(); gp.setCell(1, 1, d);
    gp.performLayout();

    MockRenderer renderer;
    gp.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    // 4 panels × 1 bg = 4; GridPanel itself draws 0 (CompoundWidget base
    // class default). Allow ≥ 4 to leave room for borders if rendered as
    // Rects.
    CHECK(rectCount >= 4);
}

TEST_SUITE_END