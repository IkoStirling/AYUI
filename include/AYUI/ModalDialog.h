#pragma once

#include "AYUI/Modal.h"
#include "AYUI/Button.h"
#include "AYUI/Panel.h"
#include <functional>
#include <string>

namespace ayt::ui {

// =============================================================================
// Phase D §5.3 — ModalDialog (cancel/OK template).
// =============================================================================
//
// A Modal pre-wired with an OK + Cancel button bar. Hosts wire a single
// `setOnResult(cb)` callback to react to the user's choice; `_onClose` (inherited
// from Modal) still fires for any close path so the host's "did anything
// dismiss the dialog?" cleanup can stay where it already lives.
//
// Inheritance (Q1): ModalDialog : Modal. We deliberately inherit rather than
// compose (an internal Modal member) so the focus trap, dimmer, Esc handler,
// and onForceClosedByManager single-active-victim logic all keep working
// without re-implementation.
//
// Body / content split (Q2):
//   `setBodyContent(Widget*)` puts the host's widget INSIDE our internal
//   Panel (`_bodyPanel`) — the panel sits above the button bar. Modal's own
//   `setContent` is NOT exposed on ModalDialog because mixing two bodies
//   (host content + our buttons) would compete for layout.
//
// Result semantics (Q3):
//   `Result::Cancel = 0`, `Result::Ok = 1`. `_onResult(cb)` fires on accept
//   or reject (whichever button the user clicked). `_onClose` (Modal's)
//   still fires for Esc / programmatic close — both callbacks are
//   independent. Esc does NOT set _result; the Result stays at the
//   constructor's default (Cancel) for cancel-style closures.
//   Dimmer click does NOT dismiss by default (forced-confirm); hosts that
//   want click-outside-to-cancel call setDismissOnDimmerClick(true).
//
// Default look (Q4-Q6): button bar is right-aligned along the bottom
// (Cancel to the left of OK), `Cancel = Reject`. Default text "OK" / "Cancel"
// English (Q5). No built-in title bar — host injects one via setBodyContent.
//
// Ownership (Q7): the OK + Cancel buttons are heap-allocated by
// ModalDialog, attached as children of `this`, and explicitly `delete`d in
// `~ModalDialog`. `~CompoundFocusableWidget` and `~Widget` do NOT delete
// children (per Phase A Widget semantics), so without the explicit delete
// these would leak. _bodyContent follows the DECISION-2 mirror:
// NOT owned, host frees via destroyWidgetTree after they remove it
// from the dialog.
//
// Button text (Q5): settable via `setAcceptText` / `setRejectText` so
// hosts can localize. v1 ships default English; i18n is the host's concern.
// =============================================================================
class ModalDialog : public Modal {
public:
    enum Result {
        Cancel = 0,
        Ok     = 1,
    };

    // Layout constants. Defaults per Q8. _bodyPanel = (width,
    // height - kButtonBarHeight) with no internal padding (Q9) so the
    // host's VBox / HBox can carry their own spacing.
    static constexpr float kDefaultWidth       = 400.0f;
    static constexpr float kDefaultHeight      = 180.0f;
    static constexpr float kButtonBarHeight    = 40.0f;
    static constexpr float kButtonSpacing      = 12.0f;  // Cancel↔OK gap
    static constexpr float kBarRightPadding    = 12.0f;  // gap from right edge
    static constexpr float kBarBottomPadding   = 8.0f;
    static constexpr float kButtonWidth        = 96.0f;
    static constexpr float kButtonHeight       = 24.0f;
    static constexpr float kBodyPadding        = 16.0f;  // text inset in body panel

    ModalDialog();
    ~ModalDialog() override;

    // Q2 — replaces Modal::setContent for the dialog subclass. The host's
    // widget is reparented onto the internal body panel (NOT directly onto
    // `this`), so layout math belongs to ModalDialog. Pass nullptr to clear.
    void setBodyContent(Widget* content);
    // Owning counterpart for factory/serializer-created body subtrees.
    void setBodyContentOwned(Widget* content);
    Widget* getBodyContent() const { return _bodyContent; }

    // Q3 — Result callback fires on user accept/reject. Argument is one
    // of the Result enum values. It is INDEPENDENT of the inherited
    // `_onClose` (any close path), which still fires for Esc / dimmer
    // click / programmatic close. See `modaldialog_esc_close_does_not_set_result`
    // for the test that pins this separation.
    void setOnResult(std::function<void(int)> cb) { _onResult = std::move(cb); }
    const std::function<void(int)>& getOnResult() const { return _onResult; }

    // Q5 — host-overridable button labels. Defaults to English "OK" / "Cancel";
    // i18n is the host's concern (Phase E / v1.1+).
    void  setAcceptText(const std::wstring& text) { _acceptText = text; if (_okButton) _okButton->setText(text); }
    void  setRejectText(const std::wstring& text) { _rejectText = text; if (_cancelButton) _cancelButton->setText(text); }
    const std::wstring& getAcceptText() const { return _acceptText; }
    const std::wstring& getRejectText() const { return _rejectText; }

    // Read-back of the most recent user-driven Result. Reset to Cancel on
    // construction; acceptDialog() / rejectDialog() flip it before firing
    // `_onResult`. NOT changed by close-via-dim-click or Esc.
    int getResult() const { return _result; }

    // Programmatic close — fires _onResult, then closeModal. Use these when
    // the dialog needs to be closed from outside the OK/Cancel callback paths
    // (e.g. async validation, network response).
    void acceptDialog();   // _result = Ok,  fires _onResult, closeModal()
    void rejectDialog();   // _result = Cancel, closeModal() WITHOUT firing _onResult
                           // (reject is the default Result value; firing it would be a no-op signal).

    // Q9 — own layoutChildren. Body panel sized to dialog minus button bar;
    // buttons laid out Cancel-then-OK along the right edge.
    void layoutChildren() override;

    // Plate chrome (shadow + fill + border). Matches Window floating look
    // so the dialog reads as a layer above the dimmer, not a flat patch.
    void onRender(IRenderBackend& renderer) override;

    // Phase D §5.3 hooks — these are private callbacks wired into the
    // Button click sinks. Public by implementation necessity (Button's
    // setOnClicked takes a std::function), but conceptually private —
    // external code should call acceptDialog() / rejectDialog() instead.
    void onAcceptClicked();
    void onRejectClicked();

private:
    void ensurePanelsCreated();
    void setBodyContentImpl(Widget* content, bool owned);

    Panel*       _bodyPanel    = nullptr;   // child of `this`
    Button*      _okButton     = nullptr;   // owned, deleted in dtor
    Button*      _cancelButton = nullptr;   // owned, deleted in dtor
    Widget*      _bodyContent  = nullptr;   // not owned (DECISION 2 mirror)
    std::wstring _acceptText   = L"OK";
    std::wstring _rejectText   = L"Cancel";
    int          _result       = Cancel;
    std::function<void(int)> _onResult;
};

Widget* createModalDialogWidget();

} // namespace ayt::ui
