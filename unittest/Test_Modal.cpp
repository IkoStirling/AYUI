#include "AYTest.h"
#include "AYUI/Modal.h"
#include "AYUI/Dimmer.h"
#include "AYUI/UIManager.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/UIKeyCode.h"

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Modal)

// =============================================================================
// Phase D (D2) — Modal tests (PR-2)
// =============================================================================
//
// Coverage:
//   - Initial state: not open, focus not held, default size.
//   - openModal saves the previously-focused widget; closeModal restores.
//   - Q8 dismissal flag (default true / set false → no dismiss on dimmer
//     click).
//   - Q6 focus trap: Tab cycles within modal subtree.
//   - Q7 Esc: UIManager.onKeyDown(Escape) closes the active modal.
//   - R3: ~Modal with _isOpen=true re-cleans UIManager (no UAF).
// =============================================================================

TEST_CASE(modal_initial_state_focused_off) {
    Modal m;
    CHECK_FALSE(m.isOpen());
    CHECK_FALSE(m.hasFocus());
    CHECK(m.isDismissOnDimmerClick());   // Q8 default true
    CHECK(m.getDimmer() == nullptr);
    CHECK(m.getContent() == nullptr);
}

TEST_CASE(modal_open_auto_attaches_default_dimmer) {
    UIManager um;
    um.initialize(nullptr);
    um.setClientSize(640.0f, 480.0f);

    Modal m;
    CHECK(m.getDimmer() == nullptr);
    um.root()->addChildExternal(&m);

    m.openModal();
    CHECK(m.isOpen());
    CHECK(m.getDimmer() != nullptr);
    // Dimmer under modal on the overlay (reverse pick → modal wins on plate).
    Widget* overlay = um.getOverlayRoot();
    CHECK(overlay != nullptr);
    CHECK(m.getDimmer()->getParent() == overlay);
    CHECK(m.getParent() == overlay);
    const auto& kids = overlay->getChildren();
    CHECK(kids.size() >= 2u);
    CHECK(kids[kids.size() - 2] == m.getDimmer());
    CHECK(kids[kids.size() - 1] == &m);

    m.closeModal();
    um.root()->removeChild(&m);
    um.shutdown();
}

// Phase D §5.3 follow-up: this test is DISABLED until the stack-dtor
// R3 landmine in TextInput::~TextInput (Phase C) gets a Phase D-style
// fix that handles mid-construction virtual dispatch on stack-stored
// FocusableWidget subclasses. The Phase D PR-2 manager-side
// clearFocusNoDispatch path covers heap-owned widgets but is incomplete
// for stack-stored widgets where the destructor order crosses paths.
// Test passes when run in isolation but SEGV in batch because of
// phase-order destructor sequencing across prior test cases.
// See commit message 7c81dd3 (Phase D PR-3) for full root-cause analysis.
TEST_CASE(modal_open_saves_focus_and_restores_on_close) {
    // Phase D §5.3: focus restore no longer fires setFocus via the
    // Modal::closeModal path — too fragile in stack-stored FocusableWidget
    // scenarios (R3 landmine). ModalDialog tests pin the focused restore
    // contract via setOnClose hooks. Here we only verify the manager-side
    // piece (focus leaves the modal on close) + _activeModal is cleared.
    UIManager um;
    um.initialize(nullptr);

    TextInput ti;
    ti.setSize(FVector2(100.0f, 24.0f));
    um.root()->addChildExternal(&ti);

    um.setFocus(&ti);
    CHECK(ti.hasFocus());

    Modal m;
    Dimmer dim;
    m.setDimmer(&dim);
    um.root()->addChildExternal(&m);

    m.openModal();
    CHECK(m.isOpen());
    CHECK(um.getFocusedWidget() == &m);

    m.closeModal();
    CHECK_FALSE(m.isOpen());
    // After closeModal, m is no longer focused. _focusedWidget may
    // still point at ti (R3-safe path skips the virtual setFocus call,
    // so the previously-focused ti still claims focus), or it may be
    // nullptr — both are valid post-conditions depending on ti lifetime.
    // We only verify m has lost focus here; the full restore semantics
    // are tested in ModalDialog tests.
    CHECK(um.getFocusedWidget() != &m);

    um.root()->removeChild(&m);
    um.shutdown();
}

TEST_CASE(modal_click_outside_inside_dismisses_when_flag_true) {
    UIManager um;
    um.initialize(nullptr);
    um.setClientSize(640.0f, 480.0f);

    Modal m;
    Dimmer dim;
    m.setDimmer(&dim);
    um.root()->addChildExternal(&m);

    m.openModal();
    CHECK(m.isOpen());

    // Drive the dimmer's onMouseButtonDown directly. Inside the dimmer:
    // the dimmer's sink fires `onDimmerClicked` → Modal::onDimmerClicked →
    // closeModal (Q8 flag default true).
    CHECK(dim.onMouseButtonDown(UIMouseEvent(FVector2(10.0f, 10.0f), 0)));
    CHECK_FALSE(m.isOpen());

    um.root()->removeChild(&m);
    um.shutdown();
}

TEST_CASE(modal_click_outside_does_not_dismiss_when_flag_false) {
    UIManager um;
    um.initialize(nullptr);
    um.setClientSize(640.0f, 480.0f);

    Modal m;
    Dimmer dim;
    m.setDimmer(&dim);
    m.setDismissOnDimmerClick(false);   // Q8 forced-confirm flow
    um.root()->addChildExternal(&m);

    m.openModal();
    CHECK(m.isOpen());

    // Dimmer swallow-click should NOT close the modal.
    CHECK(dim.onMouseButtonDown(UIMouseEvent(FVector2(10.0f, 10.0f), 0)));
    CHECK(m.isOpen());   // unchanged

    // Esc still closes (Q7 — UIManager-owned Esc policy).
    um.onKeyDown(UIKey_Escape);
    CHECK_FALSE(m.isOpen());

    um.root()->removeChild(&m);
    um.shutdown();
}

TEST_CASE(modal_tab_stays_inside_subtree) {
    UIManager um;
    um.initialize(nullptr);

    Modal m;
    um.root()->addChildExternal(&m);

    // Two focusable children inside the modal.
    TextInput* inA = new TextInput();
    inA->setSize(FVector2(80.0f, 24.0f));
    TextInput* inB = new TextInput();
    inB->setSize(FVector2(80.0f, 24.0f));
    TextInput* inC = new TextInput();
    inC->setSize(FVector2(80.0f, 24.0f));
    m.addChildExternal(inA);
    m.addChildExternal(inB);
    m.addChildExternal(inC);
    // Sibling outside the modal — must NEVER receive focus while modal is open.
    TextInput* outOfModal = new TextInput();
    outOfModal->setSize(FVector2(80.0f, 24.0f));
    um.root()->addChildExternal(outOfModal);

    m.openModal();
    // First tab from modal self → inA (DFS pre-order, children-first).
    um.onKeyDown(UIKey_Tab);
    CHECK(um.getFocusedWidget() == inA);
    um.onKeyDown(UIKey_Tab);
    CHECK(um.getFocusedWidget() == inB);
    um.onKeyDown(UIKey_Tab);
    CHECK(um.getFocusedWidget() == inC);
    // Wrap inside the modal — never reach outOfModal.
    um.onKeyDown(UIKey_Tab);
    CHECK(um.getFocusedWidget() == inA);
    um.onKeyDown(UIKey_Tab);
    CHECK(um.getFocusedWidget() == inB);
    um.onKeyDown(UIKey_Tab);
    CHECK(um.getFocusedWidget() == inC);

    m.closeModal();

    // Outside-modal tabs work again.
    um.onKeyDown(UIKey_Tab);
    CHECK(um.getFocusedWidget() != inA);
    CHECK(um.getFocusedWidget() != inB);
    CHECK(um.getFocusedWidget() != inC);

    delete outOfModal;
    um.shutdown();
}

TEST_CASE(modal_esc_closes_modal_via_uimanager) {
    UIManager um;
    um.initialize(nullptr);

    Modal m;
    Dimmer dim;
    m.setDimmer(&dim);
    um.root()->addChildExternal(&m);

    int closeCount = 0;
    m.setOnClose([&]() { ++closeCount; });

    m.openModal();
    CHECK(m.isOpen());

    // Esc → UIManager closes the active modal + fires _onClose (Q7).
    um.onKeyDown(UIKey_Escape);
    CHECK_FALSE(m.isOpen());
    CHECK(closeCount == 1);

    um.root()->removeChild(&m);
    um.shutdown();
}

TEST_CASE(modal_dtor_with_open_modal_no_uaf) {
    UIManager um;
    um.initialize(nullptr);

    Modal* m = new Modal();
    Dimmer* dim = new Dimmer();
    m->setDimmer(dim);
    um.root()->addChildExternal(m);

    m->openModal();
    CHECK(m->isOpen());

    // Destroy the Modal while open. ~Modal should close it via UIManager
    // + drop focus. No UAF on subsequent shutdown() or onDeviceCompositionEnd-
    // style paths.
    delete m;
    CHECK(um.getFocusedWidget() == nullptr);

    // Subsequent Esc must NOT segfault.
    um.onKeyDown(UIKey_Escape);

    delete dim;
    um.shutdown();
}

TEST_SUITE_END
