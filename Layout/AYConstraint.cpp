#include "AYUI/Constraint.h"
#include "AYUI/Widget.h"
// AYWidget.h provides the full CompoundWidget definition that
// ConstraintPanel inherits from. Without this include the class
// hierarchy is incomplete and methods like getWidth/getHeight/
// addChild aren't visible.

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>
#include <utility>

namespace ayt::ui {

// ---------------------------------------------------------------------------
// ConstraintVar
// ---------------------------------------------------------------------------

namespace {
int g_nextVarId = 1;
}

ConstraintVar::ConstraintVar()
    : _id(g_nextVarId++), _fixed(false), _value(0.0f) {}

ConstraintVar::ConstraintVar(float fixed)
    : _id(g_nextVarId++), _fixed(true), _value(fixed) {}

ConstraintVar::~ConstraintVar() = default;

float ConstraintVar::getValue() const { return _value; }

void ConstraintVar::setValue(float v) {
    _value = v;
}

bool ConstraintVar::isFixed() const { return _fixed; }

void ConstraintVar::setFixed(float v) {
    _fixed = true;
    _value = v;
}

// ---------------------------------------------------------------------------
// makeConstraint helper
// ---------------------------------------------------------------------------

LinearConstraint makeConstraint(
    const std::vector<std::pair<float, ConstraintVar*>>& terms,
    ConstraintOp op,
    float rhs,
    float strength)
{
    LinearConstraint c(op, rhs);
    c.strength = strength;
    c.terms.reserve(terms.size());
    for (const auto& t : terms) {
        c.terms.push_back({t.first, t.second});
    }
    return c;
}

// ---------------------------------------------------------------------------
// CassowarySolver — simplified iterative solver
// ---------------------------------------------------------------------------

CassowarySolver::CassowarySolver() = default;
CassowarySolver::~CassowarySolver() = default;

int CassowarySolver::addConstraint(const LinearConstraint& c) {
    int id = _nextId++;
    _constraints.push_back({id, c});
    return id;
}

void CassowarySolver::removeConstraint(int id) {
    _constraints.erase(
        std::remove_if(_constraints.begin(), _constraints.end(),
            [id](const StoredConstraint& sc) { return sc.id == id; }),
        _constraints.end());
}

void CassowarySolver::clear() {
    _constraints.clear();
}

void CassowarySolver::solve() {
    if (_constraints.empty()) return;

    constexpr int kMaxIterations = 256;
    constexpr float kEpsilon = 1e-6f;

    for (int iter = 0; iter < kMaxIterations; ++iter) {
        float totalError = 0.0f;
        for (const auto& sc : _constraints) {
            const LinearConstraint& c = sc.c;
            if (c.terms.empty()) continue;
            float lhs = 0.0f;
            ConstraintVar* adjustVar = nullptr;
            float adjustCoeff = 0.0f;
            for (const auto& term : c.terms) {
                lhs += term.coeff * term.var->getValue();
                if (!term.var->isFixed()) {
                    if (adjustVar == nullptr ||
                        std::abs(term.coeff) > std::abs(adjustCoeff)) {
                        adjustVar = term.var;
                        adjustCoeff = term.coeff;
                    }
                }
            }
            float residual = c.rhs - lhs;
            // Code-review 2026-08-02 #17: the convergence gate used to
            // clamp the residual to 0 for satisfied LE/GE before summing
            // into totalError. That meant an already-satisfied LE
            // contributed 0 to the error budget, so totalError could
            // fall under kEpsilon even if EQ constraints elsewhere still
            // had measurable error. Track the unsatisfied portion
            // (clamped) for the solver's per-iteration adjustment, but
            // sum the *raw* residual into totalError so EQ error is
            // always counted. LE/GE error correctly stays at 0 once
            // satisfied (the raw residual IS already 0 in that case).
            float clampedResidual = residual;
            switch (c.op) {
                case ConstraintOp::EQ: break;
                case ConstraintOp::LE:
                    if (clampedResidual >= 0.0f) clampedResidual = 0.0f;
                    break;
                case ConstraintOp::GE:
                    if (clampedResidual <= 0.0f) clampedResidual = 0.0f;
                    break;
            }
            totalError += std::abs(residual);
            if (adjustVar == nullptr || adjustCoeff == 0.0f) continue;
            float delta = clampedResidual / adjustCoeff;
            delta *= c.strength;
            adjustVar->setValue(adjustVar->getValue() + delta);
        }
        if (totalError < kEpsilon) break;
    }
}

// ---------------------------------------------------------------------------
// ConstraintPanel
// ---------------------------------------------------------------------------

ConstraintPanel::ConstraintPanel() = default;

ConstraintPanel::~ConstraintPanel() = default;

ConstraintPanel::Vars ConstraintPanel::addConstraintWidget(
    Widget* w,
    const std::vector<ParentAnchor>& parents,
    const std::vector<SiblingAnchor>& siblings,
    float preferredW,
    float preferredH)
{
    if (w == nullptr) return {};
    Vars v;
    v.w.setValue(preferredW);
    v.h.setValue(preferredH);
    Entry e;
    e.widget = w;
    e.vars = v;
    e.parents = parents;
    e.siblings = siblings;
    e.preferredW = preferredW;
    e.preferredH = preferredH;
    _entries.push_back(e);
    addChild(w);
    return v;
}

size_t ConstraintPanel::count() const {
    return _entries.size();
}

void ConstraintPanel::solve() {
    _solver.clear();

    const float pW = getWidth();
    const float pH = getHeight();
    // For "inset" sides (Right/Bottom) the value is parent's far edge
    // minus the margin — margin is the inset distance from that edge.
    // For "outset" sides (Left/Top/CenterX/CenterY) the margin is added
    // to the corresponding parent coordinate. For Width/Height the
    // margin is added directly to the parent's size.
    auto parentVarFor = [&](ParentAnchor::Side s, float margin) -> float {
        switch (s) {
            case ParentAnchor::Left:   return 0.0f + margin;
            case ParentAnchor::Right:  return pW - margin;
            case ParentAnchor::Top:    return 0.0f + margin;
            case ParentAnchor::Bottom: return pH - margin;
            case ParentAnchor::CenterX: return pW * 0.5f + margin;
            case ParentAnchor::CenterY: return pH * 0.5f + margin;
            case ParentAnchor::Width:  return pW - margin;
            case ParentAnchor::Height: return pH - margin;
        }
        return 0.0f;
    };

    for (auto& e : _entries) {
        // Soft stays on w / h (preferred size).
        {
            LinearConstraint stayW(ConstraintOp::EQ, e.preferredW);
            stayW.strength = 0.001f;
            stayW.terms.push_back({1.0f, &e.vars.w});
            _solver.addConstraint(stayW);
        }
        {
            LinearConstraint stayH(ConstraintOp::EQ, e.preferredH);
            stayH.strength = 0.001f;
            stayH.terms.push_back({1.0f, &e.vars.h});
            _solver.addConstraint(stayH);
        }

        for (const auto& pa : e.parents) {
            float parentVal = parentVarFor(pa.side, pa.margin);
            LinearConstraint lc(ConstraintOp::EQ, parentVal);
            switch (pa.side) {
                case ParentAnchor::Left:
                    lc.terms.push_back({1.0f, &e.vars.x});
                    break;
                case ParentAnchor::Right:
                    lc.terms.push_back({1.0f, &e.vars.x});
                    lc.terms.push_back({1.0f, &e.vars.w});
                    break;
                case ParentAnchor::Top:
                    lc.terms.push_back({1.0f, &e.vars.y});
                    break;
                case ParentAnchor::Bottom:
                    lc.terms.push_back({1.0f, &e.vars.y});
                    lc.terms.push_back({1.0f, &e.vars.h});
                    break;
                case ParentAnchor::CenterX:
                    lc.terms.push_back({1.0f, &e.vars.x});
                    lc.terms.push_back({0.5f, &e.vars.w});
                    break;
                case ParentAnchor::CenterY:
                    lc.terms.push_back({1.0f, &e.vars.y});
                    lc.terms.push_back({0.5f, &e.vars.h});
                    break;
                case ParentAnchor::Width:
                    lc.terms.push_back({1.0f, &e.vars.w});
                    break;
                case ParentAnchor::Height:
                    lc.terms.push_back({1.0f, &e.vars.h});
                    break;
            }
            _solver.addConstraint(lc);
        }

        for (const auto& sa : e.siblings) {
            if (sa.sibling == nullptr) continue;
            for (auto& other : _entries) {
                if (other.widget == sa.sibling) {
                    float siblingVal = sa.margin;
                    LinearConstraint lc(ConstraintOp::EQ, siblingVal);
                    switch (sa.side) {
                        case SiblingAnchor::Left:
                            lc.terms.push_back({1.0f, &e.vars.x});
                            lc.terms.push_back({-1.0f, &other.vars.x});
                            break;
                        case SiblingAnchor::Right:
                            lc.terms.push_back({1.0f, &e.vars.x});
                            lc.terms.push_back({-1.0f, &other.vars.x});
                            lc.terms.push_back({-1.0f, &other.vars.w});
                            break;
                        case SiblingAnchor::Top:
                            lc.terms.push_back({1.0f, &e.vars.y});
                            lc.terms.push_back({-1.0f, &other.vars.y});
                            break;
                        case SiblingAnchor::Bottom:
                            lc.terms.push_back({1.0f, &e.vars.y});
                            lc.terms.push_back({-1.0f, &other.vars.y});
                            lc.terms.push_back({-1.0f, &other.vars.h});
                            break;
                        case SiblingAnchor::CenterX:
                            lc.terms.push_back({1.0f, &e.vars.x});
                            lc.terms.push_back({0.5f, &e.vars.w});
                            lc.terms.push_back({-1.0f, &other.vars.x});
                            lc.terms.push_back({-0.5f, &other.vars.w});
                            break;
                        case SiblingAnchor::CenterY:
                            lc.terms.push_back({1.0f, &e.vars.y});
                            lc.terms.push_back({0.5f, &e.vars.h});
                            lc.terms.push_back({-1.0f, &other.vars.y});
                            lc.terms.push_back({-0.5f, &other.vars.h});
                            break;
                    }
                    _solver.addConstraint(lc);
                    break;
                }
            }
        }
    }

    _solver.solve();

    for (const auto& e : _entries) {
        if (e.widget == nullptr) continue;
        e.widget->setPosition(
            math::FVector2(e.vars.x.getValue(), e.vars.y.getValue()));
        e.widget->setSize(
            math::FVector2(e.vars.w.getValue(), e.vars.h.getValue()));
    }
}

void ConstraintPanel::performLayout() {
    solve();
    // Cascade layout to children — inlined because calling
    // compoundDescendLayout(this) hit an MSVC ambiguity (Widget* vs
    // ConstraintPanel* in static_cast context). The Widget base
    // class's children list is what CompoundWidget::performLayout
    // would iterate anyway.
    for (size_t i = 0; i < _children.size(); ++i) {
        _children[i]->performLayout();
    }
}

} // namespace ayt::ui