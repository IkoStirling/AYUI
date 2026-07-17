#include "AYTest.h"
#include "AYIRenderBackend.h"
#include "AYMockRenderer.h"

using namespace ayt::ui;

// R-9 (R12 fix): BlendMode / PathFillMode / AnimationCurve (and friends)
// used to be nested inside IRenderBackend, forcing call sites to spell out
// `IRenderBackend::BlendMode::Normal`. R-9 promotes them to the
// `ayt::ui` namespace so the common call site reads `BlendMode::Normal`
// directly. Old call sites still work via `using` aliases inside the
// class. This test pins both forms at compile time so a future move
// won't silently break either path.
TEST_SUITE(AYUI_R9_NamespaceEnum)

// R-9: namespace-scope enums compile and resolve.
TEST_CASE(enum_namespace_scope_resolves) {
    BlendMode bm = BlendMode::Normal;
    PathFillMode pfm = PathFillMode::Fill;
    AnimationCurve ac = AnimationCurve::EaseOut;

    CHECK(bm == BlendMode::Normal);
    CHECK(pfm == PathFillMode::Fill);
    CHECK(ac == AnimationCurve::EaseOut);

    // The class-scope using aliases re-export the namespace-scope enums
    // verbatim, so `IRenderBackend::BlendMode` IS the same type as
    // `ayt::ui::BlendMode`. Verify that the alias is a real alias (not
    // a stale forward declaration) by reading values through both paths.
    static_assert(std::is_same_v<BlendMode, IRenderBackend::BlendMode>,
                  "R-9: IRenderBackend::BlendMode must alias ayt::ui::BlendMode");
    static_assert(std::is_same_v<PathFillMode, IRenderBackend::PathFillMode>,
                  "R-9: IRenderBackend::PathFillMode must alias ayt::ui::PathFillMode");
    static_assert(std::is_same_v<AnimationCurve, IRenderBackend::AnimationCurve>,
                  "R-9: IRenderBackend::AnimationCurve must alias ayt::ui::AnimationCurve");
}

// R-9: backward-compat — IRenderBackend::BlendMode still works via the
// `using` alias inside the class. If anyone forgot the alias, the
// IRenderBackend::BlendMode::Normal spelling would fail to compile.
TEST_CASE(enum_backward_compat_via_using_alias) {
    IRenderBackend::BlendMode bm = IRenderBackend::BlendMode::Additive;
    IRenderBackend::PathFillMode pfm = IRenderBackend::PathFillMode::Stroke;
    IRenderBackend::AnimationCurve ac = IRenderBackend::AnimationCurve::Spring;

    CHECK(bm == IRenderBackend::BlendMode::Additive);
    CHECK(pfm == IRenderBackend::PathFillMode::Stroke);
    CHECK(ac == IRenderBackend::AnimationCurve::Spring);
}

// R-9: a renderer override that names an enum parameter in the old
// (class-scope) form must still resolve. MockRenderer::createAnimation
// does this — and the override must still match IRenderBackend's
// virtual signature exactly.
TEST_CASE(mockrenderer_animation_override_uses_class_scope_enum) {
    MockRenderer renderer;
    auto handle = renderer.createAnimation(0.0f, 1.0f, 0.5f,
                                           IRenderBackend::AnimationCurve::EaseInOut);
    // MockRenderer returns a fresh id (incremented per call). The
    // important thing is that the call resolves at all — if R-9 had
    // dropped the using alias, IRenderBackend::AnimationCurve wouldn't
    // name the same type as the override parameter and the override
    // wouldn't match the virtual.
    CHECK(handle.id >= 0);
}

// R-9: namespace-scope enum can be passed to a virtual that takes the
// class-scope alias. They are the same type via the alias so this is
// a no-op conversion — but it verifies the alias is real, not just
// a forward declaration that decayed.
TEST_CASE(namespace_enum_assignable_to_class_scope_alias) {
    BlendMode nsBm = BlendMode::Screen;
    IRenderBackend::BlendMode classBm = nsBm;  // implicit via using alias
    CHECK(classBm == BlendMode::Screen);
    CHECK(static_cast<int>(classBm) == static_cast<int>(BlendMode::Screen));
}

TEST_SUITE_END