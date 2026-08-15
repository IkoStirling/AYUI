#pragma once

// G13 — Constraint Layout: Cassowary-style solver. The solver operates
// on linear constraints over scalar variables (each ConstraintVar holds
// one numeric value). Constraints are of the form
//     sum(coeff_i * var_i) op rhs      (op ∈ {EQ, LE, GE})
// and may carry a strength in (0, 1] (1.0 = required, <1 = soft/preferred).
//
// A typical use case is "child.x + child.w <= parent.right - 5" — this
// anchors the right edge of a child to the parent's right edge with a
// 5-pixel margin. The solver assigns concrete numeric values to every
// var such that all constraints are satisfied; soft stay constraints
// push vars toward their preferred values but never violate required
// constraints.
//
// Implementation note: this is a SIMPLIFIED solver, not a full Cassowary
// implementation. The full simplex method handles incremental re-solve
// + suggestion propagation; we use a straightforward approach that
// works for the common case of (a) all-EQ constraints with optional
// bounds, and (b) non-overconstrained LE/GE mixes. For correctness on
// large constraint sets we recommend porting kiwi-solver-js, which is
// the canonical MIT-licensed Cassowary reference.

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <initializer_list>
#include <AYUI/Widget.h>

namespace ayt::ui {

class Widget;

class CassowarySolver;

// A constraint variable. Has an internal ID + optional initial value.
// Pre-solve: getValue() returns the value at the time of construction
// (or setValue()). Post-solve: getValue() returns the solved value.
class ConstraintVar {
public:
    ConstraintVar();
    ConstraintVar(float fixed);                 // locked value, strength=1
    ~ConstraintVar();

    float getValue() const;
    void  setValue(float v);                    // pre-solve hint
    bool  isFixed() const;
    void  setFixed(float v);                    // lock at v

    int   id() const { return _id; }

private:
    int   _id;
    bool  _fixed;
    float _value;
};

enum class ConstraintOp { LE, GE, EQ };

// A single linear constraint. `terms` are summed, then compared to `rhs`
// with `op`. Strength is in (0, 1]; 1.0 (the default) is "required".
struct LinearConstraint {
    struct Term { float coeff; ConstraintVar* var; };
    std::vector<Term> terms;
    ConstraintOp      op = ConstraintOp::EQ;
    float             rhs = 0.0f;
    float             strength = 1.0f;

    LinearConstraint() = default;
    LinearConstraint(ConstraintOp o, float r) : op(o), rhs(r) {}
};

// Convenience: build a LinearConstraint from terms + op + rhs + strength.
// Provided as a free function so callers don't need to construct a
// LinearConstraint manually when they have a list of (coeff, var) pairs.
LinearConstraint makeConstraint(
    const std::vector<std::pair<float, ConstraintVar*>>& terms,
    ConstraintOp op,
    float rhs,
    float strength = 1.0f);

// Parent-side anchor: a child's edge or center pinned to the parent's
// matching edge or center with a margin. Used at the ConstraintPanel
// public API level; the panel expands these into LinearConstraints
// before calling the solver.
struct ParentAnchor {
    enum Side {
        Left, Right, Top, Bottom,
        CenterX, CenterY,
        Width, Height
    };
    Side  side = Left;
    float margin = 0.0f;
};

// Sibling-side anchor: a child's edge pinned to a sibling's edge with
// a margin.
struct SiblingAnchor {
    Widget* sibling = nullptr;
    enum Side { Left, Right, Top, Bottom, CenterX, CenterY };
    Side    side = Left;
    float   margin = 0.0f;
};

// ---------------------------------------------------------------------------
// The solver. Hosts create one CassowarySolver per ConstraintPanel, add
// constraints via addConstraint(), then call solve() to assign values.
// ---------------------------------------------------------------------------
class CassowarySolver {
public:
    CassowarySolver();
    ~CassowarySolver();

    // Add a constraint. The solver copies the constraint's structure
    // (it does not retain the LinearConstraint by reference). Returned
    // id is for removeConstraint.
    int addConstraint(const LinearConstraint& c);

    // Remove a previously-added constraint. No-op if id is unknown.
    void removeConstraint(int id);

    // Run the solver. After this call, every ConstraintVar referenced by
    // any added constraint has its post-solve value (via getValue()).
    void solve();

    // Reset the solver to empty state. All constraints are dropped.
    void clear();

    size_t constraintCount() const { return _constraints.size(); }

private:
    struct StoredConstraint {
        int  id;
        LinearConstraint c;
    };
    std::vector<StoredConstraint> _constraints;
    int _nextId = 1;
};

// ---------------------------------------------------------------------------
// ConstraintPanel — a CompoundWidget whose children are positioned by
// the Cassowary solver instead of by a hand-written layout pass. Hosts
// call addConstraintWidget() with parent/sibling anchors, and the panel
// keeps a per-child set of ConstraintVars (x, y, w, h) wired to the
// widget's actual position/size.
//
// performLayout() (called by UIManager / parents) re-expands anchors
// into LinearConstraints, runs the solver, and pushes the solved
// values back into each child widget via setPosition / setSize.
// ---------------------------------------------------------------------------
class ConstraintPanel : public CompoundWidget {
public:
    ConstraintPanel();
    ~ConstraintPanel() override;

    // The four solver-managed vars for one child. Returned by addConstraintWidget
    // so hosts can wire more constraints between variables (e.g. w == h).
    struct Vars {
        ConstraintVar x;
        ConstraintVar y;
        ConstraintVar w;
        ConstraintVar h;
    };

    // Add a child widget with constraint anchors. preferredW/H are
    // soft EQ stays (strength=1.0, weak priority) — solver prefers
    // these sizes but won't violate required anchors.
    Vars addConstraintWidget(
        Widget* w,
        const std::vector<ParentAnchor>& parents = {},
        const std::vector<SiblingAnchor>& siblings = {},
        float preferredW = 100.0f,
        float preferredH = 50.0f);

    // Number of constraint-managed children.
    size_t count() const;

    // Re-run the solver and push values to children. Called by
    // performLayout() automatically; exposed for hosts that want to
    // re-solve after mutating anchors at runtime.
    void solve();

    void performLayout() override;

private:
    struct Entry {
        Widget* widget = nullptr;
        Vars vars;
        std::vector<ParentAnchor> parents;
        std::vector<SiblingAnchor> siblings;
        float preferredW = 100.0f;
        float preferredH = 50.0f;
    };
    std::vector<Entry> _entries;
    CassowarySolver _solver;
};

} // namespace ayt::ui