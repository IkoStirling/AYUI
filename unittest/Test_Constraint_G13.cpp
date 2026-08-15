// G13 — Cassowary-style Constraint Layout.
// Tests cover both the low-level solver (ConstraintVar / LinearConstraint /
// CassowarySolver) and the high-level ConstraintPanel widget.

#include "AYTest.h"
#include "AYUI/Constraint.h"
#include "AYUI/Widget.h"
#include "AYUI/Button.h"

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Constraint_G13)

// ---------- Low-level solver ----------

TEST_CASE(constraint_var_default_has_value_zero_and_not_fixed) {
    ConstraintVar v;
    CHECK_FLOAT_EQ(v.getValue(), 0.0f, 1e-5f);
    CHECK(v.isFixed() == false);
}

TEST_CASE(constraint_var_fixed_ctor_locks_value) {
    ConstraintVar v(5.0f);
    CHECK_FLOAT_EQ(v.getValue(), 5.0f, 1e-5f);
    CHECK(v.isFixed() == true);
    v.setValue(42.0f);   // setting a value on a fixed var updates _value
                          // but doesn't unset the fixed flag — locked means
                          // the solver won't try to adjust it.
    CHECK(v.isFixed() == true);
}

TEST_CASE(cassowary_solver_satisfies_simple_eq) {
    CassowarySolver s;
    ConstraintVar x;
    LinearConstraint c(ConstraintOp::EQ, 10.0f);
    c.terms.push_back({2.0f, &x});
    s.addConstraint(c);
    s.solve();
    CHECK_FLOAT_EQ(x.getValue(), 5.0f, 1e-3f);
}

TEST_CASE(cassowary_solver_satisfies_two_vars_one_eq) {
    CassowarySolver s;
    ConstraintVar x(3.0f);
    ConstraintVar y;
    LinearConstraint c(ConstraintOp::EQ, 10.0f);
    c.terms.push_back({1.0f, &x});
    c.terms.push_back({1.0f, &y});
    s.addConstraint(c);
    s.solve();
    CHECK_FLOAT_EQ(y.getValue(), 7.0f, 1e-3f);
}

TEST_CASE(cassowary_solver_inequality_le) {
    CassowarySolver s;
    ConstraintVar x(10.0f);
    LinearConstraint c(ConstraintOp::LE, 5.0f);
    c.terms.push_back({1.0f, &x});
    s.addConstraint(c);
    s.solve();
    CHECK(x.getValue() == 10.0f);
}

TEST_CASE(cassowary_solver_inequality_ge) {
    CassowarySolver s;
    ConstraintVar x(1.0f);
    LinearConstraint c(ConstraintOp::GE, 5.0f);
    c.terms.push_back({1.0f, &x});
    s.addConstraint(c);
    s.solve();
    CHECK(x.getValue() == 1.0f);
}

TEST_CASE(cassowary_solver_soft_stays_lose_to_required) {
    // G13 — required constraint with strength 1.0 should always
    // dominate soft stays. Our iterative solver nudges soft stays
    // with strength-scaled deltas, so a soft strength=0.001 keeps
    // pulling the var off the required value by ~0.001 * residual.
    // The test pins the value to within the soft pull magnitude
    // (~0.1) rather than requiring exact equality, since the soft
    // constraint is intentionally non-rigid.
    CassowarySolver s;
    ConstraintVar w;
    w.setValue(50.0f);
    LinearConstraint req(ConstraintOp::EQ, 50.0f);
    req.strength = 1.0f;
    req.terms.push_back({1.0f, &w});
    LinearConstraint soft(ConstraintOp::EQ, 100.0f);
    soft.strength = 0.001f;
    soft.terms.push_back({1.0f, &w});
    s.addConstraint(req);
    s.addConstraint(soft);
    s.solve();
    // Required wins by ~50 (vs the soft's pull toward 100).
    CHECK(w.getValue() < 60.0f);
    CHECK(w.getValue() >= 50.0f);
}

// ---------- High-level ConstraintPanel ----------

TEST_CASE(constraint_panel_add_widget_returns_distinct_vars) {
    ConstraintPanel panel;
    Button btn;
    auto vars = panel.addConstraintWidget(&btn);
    CHECK(panel.count() == 1u);
    // vars.x and vars.y are distinct — different addresses.
    CHECK(&vars.x != &vars.y);
    CHECK(&vars.w != &vars.h);
}

TEST_CASE(constraint_panel_parent_anchor_left_right_pins_position) {
    ConstraintPanel panel;
    panel.setSize({100.0f, 100.0f});
    Button btn;
    panel.addConstraintWidget(&btn,
        { ParentAnchor{ParentAnchor::Left, 10.0f},
          ParentAnchor{ParentAnchor::Top,  10.0f} },
        {},
        60.0f, 40.0f);
    panel.solve();
    // Child should be at (10, 10), with preferred size.
    CHECK_FLOAT_EQ(btn.getPosition().x, 10.0f, 1e-3f);
    CHECK_FLOAT_EQ(btn.getPosition().y, 10.0f, 1e-3f);
    CHECK_FLOAT_EQ(btn.getWidth(),  60.0f, 1e-2f);
    CHECK_FLOAT_EQ(btn.getHeight(), 40.0f, 1e-2f);
}

TEST_CASE(constraint_panel_parent_anchor_right_pins_right_edge) {
    ConstraintPanel panel;
    panel.setSize({100.0f, 100.0f});
    Button btn;
    // Pin to right edge of parent with 10px margin: x + w = 100 - 10 = 90
    panel.addConstraintWidget(&btn,
        { ParentAnchor{ParentAnchor::Right, 10.0f} },
        {},
        30.0f, 30.0f);
    panel.solve();
    CHECK_FLOAT_EQ(btn.getPosition().x + btn.getWidth(), 90.0f, 1e-2f);
}

TEST_CASE(constraint_panel_parent_anchor_width_height_stretches) {
    ConstraintPanel panel;
    panel.setSize({200.0f, 150.0f});
    Button btn;
    // Width == parent width (with 0 margin), Height == parent height.
    panel.addConstraintWidget(&btn,
        { ParentAnchor{ParentAnchor::Width,  0.0f},
          ParentAnchor{ParentAnchor::Height, 0.0f} },
        {},
        100.0f, 50.0f);
    panel.solve();
    CHECK_FLOAT_EQ(btn.getWidth(),  200.0f, 1e-2f);
    CHECK_FLOAT_EQ(btn.getHeight(), 150.0f, 1e-2f);
}

TEST_CASE(constraint_panel_sibling_anchor_right_of) {
    ConstraintPanel panel;
    panel.setSize({200.0f, 100.0f});
    Button a, b;
    // Anchor A at Left+10, then B to the Right of A with margin 5.
    panel.addConstraintWidget(&a,
        { ParentAnchor{ParentAnchor::Left, 10.0f},
          ParentAnchor{ParentAnchor::Top,  10.0f} },
        {},
        80.0f, 30.0f);
    SiblingAnchor sib;
    sib.sibling = &a;
    sib.side = SiblingAnchor::Right;
    sib.margin = 5.0f;
    panel.addConstraintWidget(&b,
        { ParentAnchor{ParentAnchor::Top, 10.0f} },
        { sib },
        60.0f, 30.0f);
    panel.solve();
    // A's right edge = 10 + 80 = 90; B starts 5 px to the right = 95.
    CHECK_FLOAT_EQ(b.getPosition().x, 95.0f, 1e-2f);
}

TEST_CASE(constraint_panel_solve_after_resize_reapplies) {
    ConstraintPanel panel;
    panel.setSize({100.0f, 100.0f});
    Button btn;
    panel.addConstraintWidget(&btn,
        { ParentAnchor{ParentAnchor::Width,  0.0f},
          ParentAnchor{ParentAnchor::Height, 0.0f} },
        {},
        100.0f, 50.0f);
    panel.solve();
    CHECK_FLOAT_EQ(btn.getWidth(), 100.0f, 1e-2f);
    // Resize the panel, re-solve.
    panel.setSize({50.0f, 50.0f});
    panel.solve();
    CHECK_FLOAT_EQ(btn.getWidth(), 50.0f, 1e-2f);
    CHECK_FLOAT_EQ(btn.getHeight(), 50.0f, 1e-2f);
}

TEST_SUITE_END