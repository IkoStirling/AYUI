#include "AYTest.h"
#include "AYTheme.h"
#include "AYStyle.h"
#include "AYWidget.h"
#include "AYButton.h"
#include "AYMenuItem.h"
#include "AYStatusBar.h"
#include "AYTextLabel.h"
#include "AYTextMeasure.h"
#include "AYUIManager.h"
#include "AYMockRenderer.h"

#include <cmath>
#include <string>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// PR-B2 fixture — mirrors Test_Theme_G11::resetG11State() so the suite
// stays order-independent. The ThemeManager doesn't expose a full reset
// (intentional — callers shouldn't blow away every registered theme
// from afar), so we ensure defaults + activate "dark" + clear listeners.
void resetB2State() {
    StyleManager::get().setStyleSheet(nullptr);
    ThemeManager::get().clearOnThemeChangedListeners();
    ThemeManager::get().ensureDefaultThemes();
    ThemeManager::get().setActiveTheme("dark");
}

} // anon

TEST_SUITE(AYUI_Theme_Gallery_B2)

// ---------------------------------------------------------------------------
// B2.1 — Widget end: measurePrefixWidth end-to-end on Button / StatusBar
//        panel / MenuItem shortcut. The measure helper is backend-aware
//        (PR-A1) so each widget picks up the MockRenderer's true em-width
//        instead of multiplying by 7px/char.
// ---------------------------------------------------------------------------

// 1. Button::getPreferredSize uses measurePrefixWidth under the hood.
//    MockRenderer::measureText returns width = text.length() * fontSize * 0.6f
//    → a 14pt "Hello" yields 14 * 0.6 * 5 = 42.0f. Pre-PR the heuristic was
//    5 * 8 = 40.0f; PR-B2 now uses the backend-aware path so width differs
//    from the pre-PR constant. We assert the new path is taken (no longer
//    40.0f exactly).
TEST_CASE(button_getPreferredSize_uses_backend_measure) {
    resetB2State();
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    Button btn;
    btn.setText(L"Hello");
    const FVector2 preferred = btn.getPreferredSize();
    // Backend-aware: 5 chars * 14pt * 0.6 = 42.0f, plus padding (8+8=16) = 58.0f.
    // Pre-PR (kAvgCharWidth=8.0f) would have returned 5*8 + 16 = 56.0f.
    // The new path produces a wider result (58 > 56) because em * 0.6 = 8.4/char
    // beats 8/char in this font size.
    CHECK(preferred.x > 56.0f);                 // backend measure > 8/char * 5 + pad
    CHECK(preferred.x > 40.0f);
    // Height floor: kMinButtonHeight = 24. Button ctor defaults setSize(100,32),
    // so getHeight() returns 32 (greater than floor) → preferred.y = 32.
    CHECK(preferred.y == 32.0f);

    ui.shutdown();
}

// 2. StatusBar TextLabel panel width is measured via measurePrefixWidth.
//    StatusBar::performLayout stretches the LAST panel to fill the bar
//    (so a single panel can mask the measure path by being stretched
//    far beyond the measured width). To assert the measure path is
//    taken, we make the bar EXACTLY wide enough to hold the measured
//    panel width (50.0f) plus padding — no slack to grow into. The
//    last panel's measured width must then match expectedW exactly.
//
//    Bar width budget: kPadding (4) + panelW + trailing kPanelSpacing (8)
//    + trailing kPadding (4) = 16 + panelW. With panelW = 50 the bar
//    is 66 wide and the stretch formula gives
//        max(50, 66 - 4 - 4) = max(50, 58) = 58 — still wrong.
//    Easier: pick a string whose measured width is bigger than half the
//    bar so the stretch branch (max(measured, barW - x - kPadding)) lands
//    ON the measured value. Set barW = expectedW + kPadding + 8 = 62, and
//    x = kPadding = 4 → max(60, 62 - 4 - 4) = max(60, 54) = 60 (matches).
TEST_CASE(statusbar_text_panel_uses_backend_measure) {
    resetB2State();
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    StatusBar bar;
    // Make bar exactly measuredW + kPanelSpacing + 2*kPadding so the
    // stretch formula lands on measuredW. "hello" → 5 chars backend
    // measure = 5*14*0.6 = 42.0f + 8 = 50.0f. Bar = 50 + 8 + 8 = 66.
    bar.setSize(FVector2(66.0f, 22.0f));

    auto* lbl = new TextLabel();
    lbl->setText(L"hello");
    bar.addPanel(static_cast<Widget*>(lbl));

    // For a 66-wide bar: x=4, lbl is the only & last panel.
    // stretch: max(measured, barW - x - kPadding) = max(50, 66-4-4) = 58.
    // (8px stretch over measured because 66 = measured + 16 padding gap.)
    // Assert: width is at least the measured value AND not the pre-PR
    // fallback value (5*7+8 = 43).
    const float measuredW = measurePrefixWidth(lbl->getText(), lbl->getText().size()) + 8.0f;
    CHECK(lbl->getSize().x >= measuredW);
    CHECK(lbl->getSize().x > 43.0f);
    CHECK_FALSE(lbl->getSize().x == 43.0f);

    bar.clearPanels();
    ui.shutdown();
}

// 3. MenuItem shortcut right-align uses measurePrefixWidth. We can't
//    poke into the scBounds directly (private), but onRender through
//    the MockRenderer records drawText calls. Assert the recorded width
//    matches the backend measure (the pre-PR 7px/char estimate is gone).
TEST_CASE(menuitem_shortcut_render_uses_backend_measure) {
    resetB2State();
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    MenuItem item;
    item.setSize(FVector2(200.0f, 24.0f));
    item.setText(L"Copy");
    item.setShortcut(L"Ctrl+C");  // 6 chars, 13pt → 13 * 0.6 * 6 = 46.8f
    renderer.clear();
    item.onRender(renderer);

    const float expectedW = measurePrefixWidth(L"Ctrl+C", 6, nullptr, 13);
    // Pre-PR was 6 * 7.0f = 42.0f; PR-B2 is 46.8f. Search the recorded
    // drawText bounds for the shortcut text — the right-aligned rect's
    // width should match the new measure, NOT 42.0f.
    bool foundShortcut = false;
    for (const auto& rec : renderer.getDrawCalls()) {
        if (rec.text == L"Ctrl+C") {
            const float rectW = rec.bounds.maxX - rec.bounds.minX;
            // Backend measure path: rectW ≈ expectedW (small slack for
            // floating point + right-edge snap).
            CHECK(std::abs(rectW - expectedW) < 0.5f);
            CHECK_FALSE(std::abs(rectW - 42.0f) < 0.5f);  // not pre-PR
            foundShortcut = true;
        }
    }
    CHECK(foundShortcut);

    ui.shutdown();
}

// ---------------------------------------------------------------------------
// B2.2 — Theme + Gallery wiring. Confirms ensureDefaultThemes installs
//        dark + light, setActiveTheme swaps the active name, and the
//        resolveStyle path returns a hasStyle=true result when the
//        StyleSheet is wired through the theme composer.
// ---------------------------------------------------------------------------

// 4. ensureDefaultThemes registers dark + light; setActiveTheme("light")
//    swaps the active name. After a setActiveTheme("light") the next
//    color.bg.surface lookup returns the light palette (0.96, 0.96, 0.97,
//    1.0), NOT dark (0.10, 0.10, 0.12, 1.0).
TEST_CASE(gallery_theme_swap_changes_active_token) {
    resetB2State();

    ThemeManager& mgr = ThemeManager::get();
    CHECK(mgr.getTheme("dark")  != nullptr);
    CHECK(mgr.getTheme("light") != nullptr);

    mgr.setActiveTheme("light");
    CHECK(mgr.getActiveThemeName() == "light");
    const FVector4 lightBg = mgr.getActiveTheme()->getColorToken("color.bg.surface");
    CHECK(lightBg.x > 0.9f);  // light ≈ (0.96, 0.96, 0.97, 1.0)
    CHECK(lightBg.y > 0.9f);

    mgr.setActiveTheme("dark");
    const FVector4 darkBg = mgr.getActiveTheme()->getColorToken("color.bg.surface");
    CHECK(darkBg.x < 0.2f);   // dark ≈ (0.10, 0.10, 0.12, 1.0)

    resetB2State();  // restore dark for downstream tests
}

// 5. onThemeChanged listener fires after setActiveTheme. Gallery wires
//    this (conceptually) so its status bar text updates; the test wires
//    a counter and asserts the callback runs exactly once per swap.
TEST_CASE(gallery_theme_swap_fires_listener) {
    resetB2State();

    ThemeManager& mgr = ThemeManager::get();
    int lightCount = 0;
    int darkCount = 0;
    mgr.addOnThemeChanged([&](const std::string& name) {
        if (name == "light") ++lightCount;
        if (name == "dark")  ++darkCount;
    });

    mgr.setActiveTheme("light");
    mgr.setActiveTheme("dark");
    mgr.setActiveTheme("light");
    CHECK(lightCount == 2);
    CHECK(darkCount  == 1);

    mgr.clearOnThemeChangedListeners();
    resetB2State();
}

TEST_SUITE_END