#include "AYModal.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"

namespace ayt::ui {

namespace {
    // Convenience — auto-built default dimmer for hosts that don't bring
    // their own. v1 keeps it as a private detail; exposed via factory
    // registration only.
    Dimmer* createDimmerWidget() { return new Dimmer(); }
}

Modal::Modal() {
    // Default size: 400×300 like Window. Hosts override per-modal.
    setSize(math::FVector2(400.0f, 300.0f));
}

Modal::~Modal() {
    // R3 — dtor must NOT leave UIManager::_activeModal or
    // _focusedWidget pointing at freed memory. Mirror the Phase C landmine:
    // reset state BEFORE the CompoundFocusableWidget subobject is destroyed.
    //
    // Two cleanup paths cooperate:
    //   (1) this body closes the modal with UIManager (drops _activeModal,
    //       clears capture pointers in the modal subtree). This is the
    //       right place because it's a Modal-level concern.
    //   (2) `~FocusableWidget` (Phase D D2) drops the focus pointer via
    //       clearFocusNoDispatch — NOT via setFocus(nullptr), which would
    //       do dynamic_cast<FocusableWidget*>(prev) on the about-to-be-
    //       destroyed widget and fire virtual setFocus(false), undefined
    //       behavior (R3 landmine, same pattern as Phase C TextInput
    //       ~dtor → cancelComposition(this, fireEndOnOwner=false)).
    //
    // We intentionally do NOT call setFocus(_focusedBefore) here. Focus
    // restoration to the previously-focused widget is Modal::closeModal's
    // job (Q6 focus trap completion path). If a host program reaches the
    // `delete m` path with the manager's focus still on us, the cleanup
    // is "lose the focus" — a stricter pattern. Hosts that care about
    // restoration MUST closeModal() before deleting.
    if (_isOpen) {
        UIManager::get().closeModal(this, /*fireOnClose*/ false);
        _isOpen = false;
    }
    _focusedBefore = nullptr;
    if (_dimmer != nullptr) {
        _dimmer->setOnDismiss(nullptr);
        _dimmer = nullptr;
    }
}

void Modal::setDimmer(Dimmer* dimmer) {
    if (_dimmer == dimmer) return;
    // Detach the previous dimmer's callback so a stale _onDismiss can't fire
    // on a different Modal.
    if (_dimmer != nullptr) {
        _dimmer->setOnDismiss(nullptr);
    }
    _dimmer = dimmer;
    if (_dimmer != nullptr) {
        _dimmer->setOnDismiss([this]() { onDimmerClicked(); });
    }
}

void Modal::setContent(Widget* content) {
    if (content == nullptr) {
        // Detach the existing content without destroying it.
        if (_content != nullptr && _content->getParent() == this) {
            removeChild(_content);
        }
        _content = nullptr;
        return;
    }
    // If `content` is already a child of some other widget, detach it
    // before re-parenting. Mirror openPopup's defensive detach.
    if (content->getParent() != nullptr) {
        content->getParent()->removeChild(content);
    }
    addChildExternal(content);   // ref-only; host owns lifetime
    _content = content;
}

// No `setSize` override — Widget::setSize is not virtual. Hosts calling
// `m->setSize(...)` get the base implementation; they should call
// `performLayout()` directly if size changes while the modal is open.


void Modal::openModal() {
    if (_isOpen) return;   // idempotent
    UIManager& ui = UIManager::get();

    // Q14 — single-active invariant. If another modal is open, close it
    // first. UIManager::openModal owns this rule.
    ui.openModal(this);

    // Save the prior focus, but only if it isn't US. (idempotent re-opens
    // would otherwise overwrite the prior focus with ourselves.)
    if (_focusedBefore == nullptr) {
        _focusedBefore = ui.getFocusedWidget();
        if (_focusedBefore == this) {
            // Defensive: if we were focused before, don't try to restore
            // ourselves to ourselves — let the host decide what wins.
            _focusedBefore = nullptr;
        }
    }

    // Force focus into the modal subtree. setFocus(this) on a CompoundFocusableWidget
    // marks us focused; UIManager.onKeyDown's Tab branch will now route
    // focus to focusable descendants (TextInput/Button/etc) on the first
    // Tab press. The host typically wires a "defaultButton" inside the modal
    // that's already a focusable widget — focusNext will land there.
    if (_focusedBefore != this) {
        ui.setFocus(this);
    }

    // Mount the dimmer onto _overlayRoot, sized to viewport. UIManager owns
    // the lifecycle of overlay children — when the modal closes, the
    // dimmer must be detached too (UIManager::closeModal handles it).
    if (_dimmer != nullptr && _dimmer->getParent() == nullptr) {
        const math::FVector2 vp = ui.getClientSize();
        _dimmer->setSize(vp);
        _dimmer->setPosition(math::FVector2(0.0f, 0.0f));
        // Adopting the dimmer as an overlay child. openModal already
        // attached `this` to the overlay; the dimmer is a SIBLING under
        // _overlayRoot so its hitTest wins for clicks outside the modal
        // content rect. We use the public getOverlayRoot() accessor.
        UIManager::get().getOverlayRoot()->addChildExternal(_dimmer);
    }

    _isOpen = true;
    performLayout();
}

void Modal::closeModal() {
    if (!_isOpen) return;
    UIManager& ui = UIManager::get();
    // Detach first; if _isOpen gate is reset before closeModal call we
    // could infinite-loop.
    _isOpen = false;
    ui.closeModal(this, /*fireOnClose*/ true);
    if (_dimmer != nullptr && _dimmer->getParent() != nullptr) {
        _dimmer->getParent()->removeChild(_dimmer);
    }
    // Restore focus.
    if (_focusedBefore != nullptr && _focusedBefore != this) {
        ui.setFocus(_focusedBefore);
    }
    _focusedBefore = nullptr;
}

void Modal::onDimmerClicked() {
    // Q8 — closed by click on dimmer only when flag is true.
    if (!_dismissOnDimmerClick) return;
    closeModal();
}

void Modal::onForceClosedByManager(Widget* priorRoot) {
    // Q14 — single-active victim. Mirror ComboBox onPopupDismissedByManager:
    // - flip _isOpen flag first so closeModal-style paths stop firing
    //   (idempotent invariants on closeModal must observe an already-closed
    //   modal).
    // - detach from the overlay (priorRoot is what UIManager tracked; it
    //   may equal this if no overlay-sibling mounting exists).
    // - detach the dimmer from the overlay so destroyWidgetTree doesn't
    //   accidentally free a still-attached child.
    // - restore focus to whatever we saved in openModal.
    // - DO NOT fire _onClose — the host didn't request a dismiss on this
    //   modal; another modal landed on top.
    (void)priorRoot;   // currently identical to `this`; tracked for future split-ownership hooks.
    if (!_isOpen) return;
    _isOpen = false;
    if (_dimmer != nullptr && _dimmer->getParent() != nullptr) {
        _dimmer->getParent()->removeChild(_dimmer);
    }
    if (getParent() != nullptr) {
        getParent()->removeChild(this);
    }
    if (_focusedBefore != nullptr && _focusedBefore != this) {
        UIManager::get().setFocus(_focusedBefore);
    }
    _focusedBefore = nullptr;
}

void Modal::layoutChildren() {
    // v1: dimmer fills the whole modal bounds (which UIManager::openModal
    // sizes to the viewport via the OpenModal path). Content stacks
    // beneath the dimmer — pickTopmostWidget reverse-walks overlay
    // children, so the LAST mounted child wins. The order is:
    //   _overlayRoot children:
    //     [_dimmer (mounted first)] [_modal — its content is inside]
    // Per R-9 the dimmer is added before the modal content so that click
    // events falling outside the modal content area hit the dimmer.
    // Content is laid out at the modal's own size (hosts typically set
    // modal size = content size).
    if (_dimmer != nullptr) {
        // The dimmer is NOT a child of `this` — it's a sibling on the
        // overlay. We leave its size to the openModal path.
    }
    if (_content != nullptr) {
        const math::FVector2 s = getSize();
        _content->setSize(s);
        _content->setPosition(math::FVector2(0.0f, 0.0f));
    }
}

} // namespace ayt::ui
