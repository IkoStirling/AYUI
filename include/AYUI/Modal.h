#pragma once

#include "AYUI/CompoundFocusableWidget.h"
#include "AYUI/Dimmer.h"

namespace ayt::ui {

// =============================================================================
// Phase D (D2) — Modal
// =============================================================================
//
// A modal dialog with focus trap + dimmer + Esc-to-close. Lives as a child of
// `_overlayRoot` when open, so it sits on top of the main UI canvas.
//
// Lifecycle:
//   1) Host: `Modal* m = new Modal(); m->setContent(myContent); m->setOnClose(cb);`
//   2) Host: `m->openModal();` — reparent m onto _overlayRoot (UIManager's openModal
//      handles this; Modal just delegates), save the previously-focused widget,
//      force focus into the modal subtree, mount a Dimmer if not attached.
//   3) User interaction: clicks on the Dimmer → Modal::onDimmerClicked (gated by
//      _dismissOnDimmerClick → closeModal). Esc → UIManager.onKeyDown intercepts
//      and fires Modal::closeModal. Tab / focus stays inside the subtree via
//      collectFocusablesDFS(this) (the UIManager Tab handler picks the right
//      DFS root when focus is inside _activeModal).
//   4) closeModal: detach from overlay, restore previously-focused widget, fire
//      _onClose if set.
//
// DECISION (Q5): two widgets (Dimmer + Modal). The dimmer is a separate
// widget so reuse tests + a future "Modal without dimmer" use case can
// swap it out independently. Hosts may call setDimmer / setDimmerOwned;
// openModal also calls ensureDimmer() so a default owned scrim appears
// when none was attached (industrial modal blocking without boilerplate).
// Hosts that truly want no dimmer must setDimmer(nullptr) after open —
// or we can add a policy flag later.
//
// DECISION (Q6): focus-trap start root = `this`. UIManager.onKeyDown's Tab
// branch reads `_activeModal` and chooses `_activeModal` as the DFS root
// when `_focusedWidget` is inside it. We reuse `collectFocusablesDFS(root)`
// exactly as written.
//
// DECISION (Q7): UIManager.onKeyDown swallows Esc when `_activeModal != nullptr`.
// The Modal widget itself does NOT receive Esc on its `onKeyDown` (since the
// focused widget is INSIDE the modal — typically a Button — and the modal
// has no focus ownership of its own; the UIManager-level interception is
// what fires `_onClose`. If a future host wants "host sees Esc first", we
// will add a policy flag — for now the policy is "UIManager wins".
//
// DECISION (Q8): `_dismissOnDimmerClick` field defaulting to true. Hosts
// needing forced-confirm flows (delete-this-resource, etc.) set false so
// the dimmer becomes a true blocker.
//
// Caller-owned (DECISION 2 — mirror ComboBox popup / TreeNode row):
// the Modal heap-allocates itself (or a host allocator does); destroyWidgetTree
// is the canonical teardown, called by `~Modal` if `_isOpen` to first
// deregister from UIManager, and by external host code when retiring.
// =============================================================================
class Modal : public CompoundFocusableWidget {
public:
    Modal();
    ~Modal() override;

    // Sets the dimmer. Two ownership modes — pick the one matching the
    // call site's allocation:
    //
    //   setDimmer(d)            — non-owning attach. Host owns the
    //                             Dimmer's lifetime; ~Modal just
    //                             detaches the callback. Matches the
    //                             stack-allocated test pattern
    //                             `Modal m; Dimmer d; m.setDimmer(&d);`
    //                             and any host that already owns the
    //                             Dimmer as a member / persistent.
    //
    //   setDimmerOwned(new D)   — owning attach. Modal deletes the
    //                             Dimmer in ~Modal(). Use for the
    //                             common case `m.setDimmerOwned(new Dimmer())`
    //                             which previously leaked because no
    //                             caller-side delete path existed.
    //
    // Code-review 2026-08-02 #12: prior contract was ambiguous ("host
    // owns lifetime, but no one calls delete"), so `new Dimmer()` calls
    // leaked and pre-dtor host frees caused UAF. The two-mode split
    // keeps the existing test fixtures valid AND gives heap-allocating
    // hosts a clear ownership path.
    //
    // The previously-attached dimmer (if any) is detached in both modes.
    // In setDimmerOwned mode, the previously-attached dimmer is also
    // deleted (Modal owns it for the new one's lifetime too).
    void setDimmer(Dimmer* dimmer);
    void setDimmerOwned(Dimmer* dimmer);
    Dimmer* getDimmer() const { return _dimmer; }

    // Lazily attach an owned default Dimmer (50% black scrim) when the
    // host never called setDimmer / setDimmerOwned. openModal invokes
    // this so Gallery / editor hosts get industrial modal blocking
    // without boilerplate. No-op when a dimmer is already attached.
    void ensureDimmer();

    // Sets the content widget. Reparents onto `this`. Host owns the content
    // lifetime; Modal does NOT destroy content on close (mirror DECISION 2
    // for ComboBox popup).
    void setContent(Widget* content);
    // Owning counterpart used by loaders/serializers that allocate the
    // content subtree. Replacing or destroying the Modal also destroys this
    // content unless destroyWidgetTree already visited it first.
    void setContentOwned(Widget* content);
    Widget* getContent() const { return _content; }

    // Lifecycle. openModal reparents the Modal onto _overlayRoot (delegates
    // to UIManager::openModal), saves the previously focused widget,
    // sets focus into the modal subtree, mounts the dimmer if attached
    // (also on the overlay), and sets _isOpen=true.
    //
    // closeModal detaches the Modal from the overlay, restores focus to
    // the previously-focused widget (or clears focus if it was nullptr),
    // and fires _onClose if set. After closeModal the host may re-open
    // by calling openModal again. Idempotent.
    void openModal();
    void closeModal();

    // Clamp the modal to the available viewport and place it at the visual
    // centre.  Call this before openModal() so the opening animation returns
    // to the centred position instead of animating from the viewport origin.
    // A non-zero outerMargin keeps the plate away from screen edges when the
    // preferred size is larger than the available client area.
    void fitAndCenterInViewport(const math::FVector2& viewportSize,
                                float outerMargin = 0.0f);

    // Invoked by UIManager::openModal when this modal is the SINGLE-ACTIVE
    // victim of a new modal mounting (Q14). Mirrors ComboBox's
    // onPopupDismissedByManager (Phase A landmine #2). Clears the open
    // state, detaches dimmer from the overlay, restores any previously
    // saved focus, and resets bookkeeping — WITHOUT firing the host's
    // _onClose (the host did not ask to dismiss this modal; another modal
    // landed on top). `priorRoot` is the overlay-rooted widget pointer that
    // UIManager tracked; it's typically `this` but we accept it as a
    // parameter so the path is durable against future refactors that
    // split a Modal between an "owner" widget and a "root" widget.
    void onForceClosedByManager(Widget* priorRoot);

    bool isOpen() const { return _isOpen; }

    // Q8 — if true, clicking the dimmer dismisses the modal (default).
    // If false, the dimmer is a true blocker (forced-confirm flows).
    void setDismissOnDimmerClick(bool enabled) { _dismissOnDimmerClick = enabled; }
    bool isDismissOnDimmerClick() const { return _dismissOnDimmerClick; }

    // Q4-style notify-only callback. Called once on close. Empty by default.
    void setOnClose(std::function<void()> cb) { _onClose = std::move(cb); }
    const std::function<void()>& getOnClose() const { return _onClose; }

    // Dimension override — Modal size is treated as max-content-size for
    // layout. Hosts typically set size explicitly via setSize. NOTE: Widget::
    // setSize is NOT virtual, so we wrap with a re-layout hook AFTER calling
    // the base setter (instead of overriding it). openModal/closeModal also
    // trigger performLayout for the same reason.
    //
    // layoutChildren — dimmer fills the viewport, content gets the rest.
    void layoutChildren() override;

private:
    friend class UIManager;

    void onDimmerClicked();   // sink bound to _dimmer->_onDismiss
    void setContentImpl(Widget* content, bool owned);
    void updateViewportLayout(const math::FVector2& viewportSize);

    Dimmer* _dimmer = nullptr;
    // Code-review 2026-08-02 #12: tracks whether ~Modal should delete
    // _dimmer (true only when ownership was transferred via
    // setDimmerOwned). Defaults false so stack-allocated Dimmer& passed
    // to setDimmer() is NOT freed on modal destruction.
    bool    _dimmerOwned = false;
    Widget* _content = nullptr;
    Widget* _focusedBefore = nullptr;
    bool    _isOpen = false;
    bool    _dismissOnDimmerClick = true;
    bool    _fitAndCenterWithViewport = false;
    float   _viewportOuterMargin = 0.0f;
    math::FVector2 _viewportPreferredSize{0.0f, 0.0f};
    std::function<void()> _onClose;
};

} // namespace ayt::ui
