#include "AYUI/Modal.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetFactory.h"

#include <algorithm>
#include <cmath>

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
        if (UIManager* ui = UIManager::tryGet()) {
            ui->closeModal(this, /*fireOnClose*/ false);
        }
        _isOpen = false;
    }
    // UI animation lane: if closeModal() left us mid-fade-out (animated
    // close keeps the modal mounted — UIManager::closeModal skips the
    // detach for pending popups), a host `delete` while the fade runs
    // must still drop the overlay's references NOW, or the pending
    // finalize / overlay teardown would deref freed memory.
    if (UIManager* ui = UIManager::tryGet()) {
        ui->cancelPendingPopupClose(this);
    }
    if (getParent() != nullptr) {
        getParent()->removeChild(this);
    }
    _focusedBefore = nullptr;

    // Direct `delete modal` must release serializer-owned content, while an
    // outer destroyWidgetTree(modal) has already removed that child before
    // entering this destructor. Test membership before dereferencing the
    // alias so both teardown routes are safe.
    const auto contentIt = std::find(getChildren().begin(), getChildren().end(),
                                     _content);
    if (_content != nullptr && contentIt != getChildren().end()) {
        if (_content->isExternallyOwned()) {
            removeChild(_content);
        } else {
            destroyWidgetTree(_content);
        }
    }
    _content = nullptr;

    if (_dimmer != nullptr) {
        // Code-review 2026-08-02 #12: detach callback. Only delete
        // when ownership was transferred via setDimmerOwned() —
        // setDimmer() (the stack-allocated host-owned path) must NOT
        // free the dimmer. _dimmerOwned defaults to false and is
        // flipped true ONLY by setDimmerOwned().
        _dimmer->setOnDismiss(nullptr);
        if (_dimmer->getParent() != nullptr) {
            // Fade-out path: the dimmer is still mounted on the overlay.
            _dimmer->getParent()->removeChild(_dimmer);
        }
        if (_dimmerOwned) {
            delete _dimmer;
        }
        _dimmer = nullptr;
        _dimmerOwned = false;
    }
}

void Modal::setDimmer(Dimmer* dimmer) {
    if (_dimmer == dimmer) return;
    // Non-owning path: detach the previous dimmer's callback so a
    // stale _onDismiss can't fire on a different Modal. If we previously
    // took ownership via setDimmerOwned, free the old dimmer now.
    if (_dimmer != nullptr) {
        _dimmer->setOnDismiss(nullptr);
        if (_dimmerOwned) {
            delete _dimmer;
            _dimmerOwned = false;
        }
    }
    _dimmer = dimmer;
    _dimmerOwned = false;
    if (_dimmer != nullptr) {
        _dimmer->setOnDismiss([this]() { onDimmerClicked(); });
    }
}

void Modal::setDimmerOwned(Dimmer* dimmer) {
    if (_dimmer == dimmer) return;
    // Owning path: same as setDimmer but flag the dimmer for deletion
    // in ~Modal(). Replaces any previously-attached dimmer (which is
    // freed if it was owned; detached if it was host-owned).
    if (_dimmer != nullptr) {
        _dimmer->setOnDismiss(nullptr);
        if (_dimmerOwned) {
            delete _dimmer;
        }
    }
    _dimmer = dimmer;
    _dimmerOwned = (_dimmer != nullptr);
    if (_dimmer != nullptr) {
        _dimmer->setOnDismiss([this]() { onDimmerClicked(); });
    }
}

void Modal::ensureDimmer() {
    if (_dimmer == nullptr) {
        setDimmerOwned(new Dimmer());
    }
}

void Modal::setContent(Widget* content) {
    setContentImpl(content, false);
}

void Modal::setContentOwned(Widget* content) {
    setContentImpl(content, true);
}

void Modal::setContentImpl(Widget* content, bool owned) {
    // Detach or destroy the old payload according to the ownership encoded
    // on the parent edge. Re-attaching the same pointer is intentional: it
    // allows callers to promote/demote ownership explicitly.
    const auto oldIt = std::find(getChildren().begin(), getChildren().end(),
                                 _content);
    if (_content != nullptr && oldIt != getChildren().end()) {
        if (_content == content) {
            removeChild(_content);
        } else if (_content->isExternallyOwned()) {
            removeChild(_content);
        } else {
            destroyWidgetTree(_content);
        }
    }
    _content = nullptr;

    if (content == nullptr) {
        return;
    }
    if (content->getParent() != nullptr) {
        content->detachFromParent();
    }
    if (owned) {
        addChild(content);
    } else {
        addChildExternal(content);
    }
    _content = content;
}

// No `setSize` override — Widget::setSize is not virtual. Hosts calling
// `m->setSize(...)` get the base implementation; they should call
// `performLayout()` directly if size changes while the modal is open.


void Modal::openModal() {
    if (_isOpen) return;   // idempotent
    // Phase D §5.3 — tryGet() instead of get(). get()'s fallback
    // bootstraps a fresh overlay if no active manager, which would pin
    // this Modal as _activeModal of the process-lifetime static
    // fallback. Prefer early-return over that path.
    UIManager* uiPtr = UIManager::tryGet();
    if (uiPtr == nullptr) {
        return;   // no active manager — defer open until one exists
    }
    UIManager& ui = *uiPtr;

    // Industrial default: full-viewport scrim when the host forgot one.
    // Stack/test fixtures that already called setDimmer keep theirs.
    ensureDimmer();
    updateViewportLayout(ui.getClientSize());

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

    // Mount the dimmer UNDER the modal on _overlayRoot. pickTopmostWidget
    // reverse-walks overlay children — last child wins. If the dimmer were
    // added after the modal it would swallow OK/Cancel hits across the
    // whole viewport. Order: [dimmer, modal].
    if (_dimmer != nullptr) {
        const math::FVector2 vp = ui.getClientSize();
        _dimmer->setSize(vp);
        _dimmer->setPosition(math::FVector2(0.0f, 0.0f));
        Widget* overlay = ui.getOverlayRoot();
        if (overlay != nullptr) {
            if (_dimmer->getParent() != nullptr) {
                _dimmer->getParent()->removeChild(_dimmer);
            }
            // Detach modal briefly so we can stack dimmer then modal.
            if (getParent() == overlay) {
                overlay->removeChild(this);
            }
            overlay->addChildExternal(_dimmer);
            overlay->addChildExternal(this);
        }
    }

    _isOpen = true;
    performLayout();

    // UI animation lane: fade the modal plate + dimmer scrim in together.
    // The dimmer is an overlay sibling; its own pushOpacity fades the
    // scrim as one unit.
    setOpacity(0.0f);
    animateOpacity(1.0f, 160.0f, AnimationCurve::EaseOut);
    if (_dimmer != nullptr) {
        _dimmer->setOpacity(0.0f);
        _dimmer->animateOpacity(1.0f, 160.0f, AnimationCurve::EaseOut);
    }
    // UI-anim cut 2: the plate slides in from 8px above; the full-screen
    // dimmer stays put (a scrim has nothing to slide).
    const math::FVector2 p = getPosition();
    setPosition(p + math::FVector2(0.0f, -8.0f));
    animatePositionTo(p, 160.0f, AnimationCurve::EaseOut);
}

void Modal::fitAndCenterInViewport(const math::FVector2& viewportSize,
                                   float outerMargin) {
    _fitAndCenterWithViewport = true;
    _viewportOuterMargin = std::max(0.0f, outerMargin);
    _viewportPreferredSize = getSize();
    updateViewportLayout(viewportSize);
}

void Modal::updateViewportLayout(const math::FVector2& viewportSize) {
    const float viewportWidth = std::max(0.0f, viewportSize.x);
    const float viewportHeight = std::max(0.0f, viewportSize.y);
    if (viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
        return;
    }

    if (_dimmer != nullptr) {
        _dimmer->setPosition(math::FVector2(0.0f, 0.0f));
        _dimmer->setSize(math::FVector2(viewportWidth, viewportHeight));
    }
    if (!_fitAndCenterWithViewport) {
        return;
    }

    const float margin = _viewportOuterMargin;
    const float availableWidth = std::max(1.0f,
        viewportWidth - 2.0f * std::min(margin, viewportWidth * 0.5f));
    const float availableHeight = std::max(1.0f,
        viewportHeight - 2.0f * std::min(margin, viewportHeight * 0.5f));

    math::FVector2 size = _viewportPreferredSize;
    size.x = std::min(std::max(1.0f, size.x), availableWidth);
    size.y = std::min(std::max(1.0f, size.y), availableHeight);
    setSize(size);
    setLayoutPositionManaged(false);
    setPosition(math::FVector2(
        std::round(std::max(0.0f, (viewportWidth - size.x) * 0.5f)),
        std::round(std::max(0.0f, (viewportHeight - size.y) * 0.5f))));
    performLayout();
}

void Modal::closeModal() {
    if (!_isOpen) return;
    // Phase D §5.3 — tryGet fallback. If the manager has already been
    // shut down (g_activeUIManager == nullptr), the static fallback in
    // get() would bootstrap a fresh overlay AND keep this Modal pinned
    // there as the focused widget — leading to batch SEGV at process
    // exit. closeModal simply becomes a no-op in that case.
    UIManager* uiPtr = UIManager::tryGet();
    if (uiPtr == nullptr) {
        _isOpen = false;
        return;
    }
    UIManager& ui = *uiPtr;
    // Detach first; if _isOpen gate is reset before closeModal call we
    // could infinite-loop.
    _isOpen = false;
    // UI animation lane: a live manager fades the modal + dimmer scrim
    // out instead of detaching instantly. Order matters: beginPopupFadeOut
    // must run BEFORE ui.closeModal so the pending entry exists when
    // closeModal's removeChild hits its isPendingPopupClose guard —
    // otherwise the modal detaches on the close frame and has no parent
    // to render the fade (or the fade is skipped entirely).
    if (!ui.isShuttingDown()) {
        constexpr float kModalFadeOutMs = 120.0f;
        ui.beginPopupFadeOut(this, /*destroy=*/false, _dimmer);
        ui.closeModal(this, /*fireOnClose*/ true);
        animateOpacity(0.0f, kModalFadeOutMs, AnimationCurve::EaseIn);
        if (_dimmer != nullptr) {
            _dimmer->animateOpacity(0.0f, kModalFadeOutMs, AnimationCurve::EaseIn);
        }
        // Finalize detaches both once the fade ends. The dimmer fade
        // covers the scrim; the plate fade covers the modal.
    } else {
        ui.closeModal(this, /*fireOnClose*/ true);
        if (_dimmer != nullptr && _dimmer->getParent() != nullptr) {
            _dimmer->getParent()->removeChild(_dimmer);
        }
    }
    // Phase D §5.3 R3-safe focus path. We deliberately drop the focus to
    // nullptr rather than calling ui.setFocus(_focusedBefore) because:
    //   (a) The previously-focused widget may already be mid-destruction
    //       if the host closed the modal during destruction order (R3
    //       landmine — setFocus does dynamic_cast + virtual setFocus(false)
    //       which crashes on already-destroyed widgets).
    //   (b) Hosts that need to restore focus should wire `setOnClose`
    //       and call setFocus themselves AFTER Modal::closeModal returns,
    //       when they can guarantee the previous widget is still alive.
    // The Phase C TextInput ~dtor uses the same clearFocusNoDispatch
    // path (PR-2 commit 6e6a31a) — this is the Modal-side mirror of
    // that fix for the fireOnClose=true path.
    if (_focusedBefore != nullptr) {
        if (ui.getFocusedWidget() == _focusedBefore) {
            // R3-safe drop: only reset if the manager's pointer still
            // points at the saved widget AND virtual call would be safe
            // (i.e. the widget hasn't been destroyed underneath us). The
            // widget may be alive or it may be on its way out; either
            // way we clear the manager's pointer. clearFocusNoDispatch
            // does NOT fire virtual on the widget, which is the R3 fix.
            ui.clearFocusNoDispatch(_focusedBefore);
        } else if (ui.getFocusedWidget() == this) {
            // We were the focused widget at closeModal time but the
            // previously-focused widget was already freed (R3 path: its
            // dtor ran before our closeModal). Drop our pointer too.
            ui.clearFocusNoDispatch(this);
        }
        _focusedBefore = nullptr;
    } else if (ui.getFocusedWidget() == this) {
        // No prior focus was saved (openModal was called without a
        // preceding focus state). We still hold the manager's pointer
        // — drop the reference so the post-closeModal state is clean.
        // This is the Q6 focus trap completion: when a modal opens
        // without a saved focus (rare but possible), closing should
        // leave the manager in a clean state.
        ui.clearFocusNoDispatch(this);
    }

    // Host dismissal notify (Esc / dimmer / OK / Cancel). Not fired from
    // ~Modal (closeModal(..., fireOnClose=false)) or force-close-by-manager.
    if (_onClose) {
        _onClose();
    }
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
        if (UIManager* ui = UIManager::tryGet()) {
            ui->setFocus(_focusedBefore);
        }
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
