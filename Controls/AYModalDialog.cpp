#include "AYUI/ModalDialog.h"
#include "AYUI/UIManager.h"
#include "AYUI/IRenderBackend.h"

#include <algorithm>
#include <utility>

namespace ayt::ui {

// ----------------------------------------------------------------------------
// Construction / destruction
// ----------------------------------------------------------------------------

ModalDialog::ModalDialog() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    // OK/Cancel dialogs are forced-confirm by default: the scrim blocks
    // input but does NOT dismiss. Esc still cancels via UIManager (Q7).
    // Hosts that want click-outside-to-cancel call setDismissOnDimmerClick(true).
    setDismissOnDimmerClick(false);
    // Lazy panel + button construction. We do NOT create them in the
    // ctor body directly because adding Button children during
    // construction of a CompoundFocusableWidget risks double-init via
    // virtual dispatch from base subclasses. ensurePanelsCreated runs
    // inside layoutChildren the first time layout is invoked (or
    // openModal calls performLayout which triggers it).
    ensurePanelsCreated();
}

ModalDialog::~ModalDialog() {
    // R7 / R3 landmine pattern (Phase D PR-2 confirmed): base ~Modal
    // does NOT destroy `_content` (DECISION 2 mirror). Likewise,
    // ~CompoundFocusableWidget / ~Widget don't free children. The
    // OK + Cancel buttons are owned by us and would leak without an
    // explicit delete.
    //
    // Order matters:
    //   (1) Detach our click sinks so a partial teardown cannot trigger
    //       onAcceptClicked/onRejectClicked → closeModal → virtual
    //       dispatch on `this` (already mid-dtor — R3 landmine).
    //   (2) Detach the buttons from `this` so the base dtor's tree walk
    //       doesn't iterate them after we've freed them.
    //   (3) delete each button (the freed pointers become null for the
    //       sanitizer — they don't need to be cleared because the dtor
    //       itself is unwinding).
    const auto isDirectChild = [this](const Widget* child) {
        return child != nullptr
            && std::find(getChildren().begin(), getChildren().end(), child)
                != getChildren().end();
    };
    if (isDirectChild(_okButton)) {
        _okButton->setOnClicked(nullptr);
        destroyWidgetTree(_okButton);
    }
    if (isDirectChild(_cancelButton)) {
        _cancelButton->setOnClicked(nullptr);
        destroyWidgetTree(_cancelButton);
    }
    _okButton = nullptr;
    _cancelButton = nullptr;

    // The body panel is an owning child. Its body edge records whether the
    // host or the dialog owns the body, so destroyWidgetTree detaches an
    // external body and destroys a serializer-owned one. If an outer tree
    // teardown already visited the panel, it is no longer a direct child and
    // the stale aliases are never dereferenced.
    if (isDirectChild(_bodyPanel)) {
        destroyWidgetTree(_bodyPanel);
    }
    _bodyPanel = nullptr;
    _bodyContent = nullptr;
}

// ----------------------------------------------------------------------------
// Body content (Q2)
// ----------------------------------------------------------------------------

void ModalDialog::setBodyContent(Widget* content) {
    setBodyContentImpl(content, false);
}

void ModalDialog::setBodyContentOwned(Widget* content) {
    setBodyContentImpl(content, true);
}

void ModalDialog::setBodyContentImpl(Widget* content, bool owned) {
    ensurePanelsCreated();
    if (_bodyPanel == nullptr) return;

    const auto& bodyChildren = _bodyPanel->getChildren();
    const auto oldIt = std::find(bodyChildren.begin(), bodyChildren.end(),
                                 _bodyContent);
    if (_bodyContent != nullptr && oldIt != bodyChildren.end()) {
        if (_bodyContent == content || _bodyContent->isExternallyOwned()) {
            _bodyPanel->removeChild(_bodyContent);
        } else {
            destroyWidgetTree(_bodyContent);
        }
    }
    _bodyContent = nullptr;

    if (content != nullptr) {
        if (content->getParent() != nullptr) {
            content->detachFromParent();
        }
        if (owned) {
            _bodyPanel->addChild(content);
        } else {
            _bodyPanel->addChildExternal(content);
        }
        _bodyContent = content;
    }
    performLayout();   // re-run layout so the new body fits the body panel
}

// ----------------------------------------------------------------------------
// Buttons + clicks
// ----------------------------------------------------------------------------

void ModalDialog::onAcceptClicked() {
    // The user pressed OK. Set _result, fire _onResult, then close.
    // closeModal fires _onClose (inherited). We do NOT order matters
    // because Result → _onResult → Modal::closeModal → _onClose is
    // a clear cause → effect → dismissal chain, with no virtual
    // dispatch in the middle (closeModal touches Modal's own fields,
    // not ModalDialog virtuals).
    _result = Ok;
    if (_onResult) {
        _onResult(_result);
    }
    closeModal();
}

void ModalDialog::onRejectClicked() {
    // The user pressed Cancel. Q3 — Esc / dimmer-click do NOT touch
    // _result (it stays at the constructor's default Cancel anyway),
    // so firing _onResult(0) here would be a redundant signal. We
    // close the modal silently. Hosts that want to distinguish "user
    // pressed Cancel" from "Esc closed" should listen to _onClose for
    // the dismissal signal and assume Cancel semantics.
    closeModal();
}

void ModalDialog::acceptDialog() {
    // Programmatic accept (no button click). Same path as onAcceptClicked.
    onAcceptClicked();
}

void ModalDialog::rejectDialog() {
    onRejectClicked();
}

// ----------------------------------------------------------------------------
// Layout
// ----------------------------------------------------------------------------

void ModalDialog::ensurePanelsCreated() {
    // Body panel is a transparent layout host — chrome is painted by
    // ModalDialog::onRender (shadow + rounded plate). Keep border off so
    // we don't double-stroke the inset body.
    if (_bodyPanel == nullptr) {
        _bodyPanel = new Panel();
        _bodyPanel->setBorderEnabled(false);
        _bodyPanel->setBackgroundEnabled(false);
        addChild(_bodyPanel);
    }
    if (_okButton == nullptr) {
        _okButton = new Button();
        _okButton->setText(_acceptText);
        _okButton->setOnClicked([this]() { onAcceptClicked(); });
        addChild(_okButton);
    }
    if (_cancelButton == nullptr) {
        _cancelButton = new Button();
        _cancelButton->setText(_rejectText);
        _cancelButton->setOnClicked([this]() { onRejectClicked(); });
        addChild(_cancelButton);
    }
}

void ModalDialog::layoutChildren() {
    ensurePanelsCreated();
    const float w = getWidth();
    const float h = getHeight();
    const float barTop = std::max(0.0f, h - kButtonBarHeight);

    // Body plate fills (width, height - button bar). Host content is
    // inset by kBodyPadding so labels are not glued to the chrome edge.
    if (_bodyPanel != nullptr) {
        _bodyPanel->setSize(math::FVector2(w, barTop));
        _bodyPanel->setPosition(math::FVector2(0.0f, 0.0f));
        if (_bodyContent != nullptr) {
            const float innerW = std::max(0.0f, w - kBodyPadding * 2.0f);
            const float innerH = std::max(0.0f, barTop - kBodyPadding * 2.0f);
            _bodyContent->setPosition(math::FVector2(kBodyPadding, kBodyPadding));
            // Keep host-authored size when larger; otherwise fill the inset.
            if (_bodyContent->getWidth() < 1.0f || _bodyContent->getHeight() < 1.0f) {
                _bodyContent->setSize(math::FVector2(innerW, innerH));
            }
        }
    }

    // Q4 — button bar right-aligned along the bottom. Cancel sits to
    // the LEFT of OK (industry convention: cancel = negative
    // affirmation, on the left). Compute x positions by walking
    // right-to-left from the right edge.
    if (_cancelButton != nullptr && _okButton != nullptr) {
        const float okX = w - kBarRightPadding - kButtonWidth;
        const float okY = barTop + (kButtonBarHeight - kButtonHeight) * 0.5f;
        _okButton->setSize(math::FVector2(kButtonWidth, kButtonHeight));
        _okButton->setPosition(math::FVector2(okX, okY));

        const float cancelX = okX - kButtonSpacing - kButtonWidth;
        const float cancelY = okY;
        _cancelButton->setSize(math::FVector2(kButtonWidth, kButtonHeight));
        _cancelButton->setPosition(math::FVector2(cancelX, cancelY));
    }
}

void ModalDialog::onRender(IRenderBackend& renderer) {
    const math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }
    // Mirror Window floating chrome so the plate lifts off the dimmer.
    renderer.drawRectShadow(bounds, IRenderBackend::ShadowStyle{
        math::FVector4(0.0f, 0.0f, 0.0f, 0.45f),
        math::FVector2(0.0f, 4.0f),
        10.0f,
        4.0f
    });
    renderer.drawRoundedRect(bounds, math::FVector4(0.18f, 0.18f, 0.20f, 1.0f), 4.0f);
    renderer.drawBorderRect(bounds, math::FVector4(0.08f, 0.08f, 0.08f, 1.0f), 1.0f, 4.0f);
}

Widget* createModalDialogWidget() {
    return new ModalDialog();
}

} // namespace ayt::ui
