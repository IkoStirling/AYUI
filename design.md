# AYUI Design

**Version:** v1.5  
**Date:** 2026-07-26  
**Status:** Core library + refactor lane (R-1..R-10) + new-control lane (C-1..C-12) implemented. Loader parity patch (L1..L4) in flight. v1.5 adds **[§17 Docking & Sub-Window](#17-docking--sub-window-v15-roadmap)** to support the editor goal of stop-dockable panels + child windows.

> Historical note: [`AYUI-v1-Design.md`](AYUI-v1-Design.md) (v1.2) remains as reference.  
> **This file is authoritative** for architecture, phases, and data-driven contracts.

---

## 1. Overview

AYUI is the engine UI framework for in-game HUD and tools UI. v1 scope is **screen-space 2D overlay** composited after the 3D pass (see [§8 Engine integration](#8-engine-integration)).

### 1.1 Goals

| Goal | v1 approach |
|------|-------------|
| Data-driven UI | **Reuse existing JSON loader** ([§4](#4-data-driven-layer-reuse-assessment)) |
| Stable internal events | `Widget::UIEvent` bubble ([§5](#5-event-system)) |
| Renderer decoupling | `IRenderBackend` + one bgfx impl in AYRenderer (U2) |
| i18n | Key `ui.{section}.{key}` via `I18n` (implemented) |
| Hot reload | Extend `UILayoutLoader` file watch (U1); no AYConfig dependency in v1 |

### 1.2 Non-goals (deferred)

- 3D spatial UI (`SpatialWidget`, `UIPlane`)
- Pixel-mask hit test, advanced effects (blur, particles, RT in `IRenderBackend`)
- Grid / relative layout
- Replacing engine-wide event system

### 1.3 Position in engine (2026-07)

```
GameLoop
  → EntitySubSystem / RendererSubSystem
  → ForwardOpaquePass (3D RenderScene)     ← AYRenderer R4 + Engine ✅
  → UIPass / UI overlay (2D)               ← AYUI U2 (planned)
```

References: [AYRenderer/design.md](../AYRenderer/design.md) §10.4, [AYEntity/design.md](../AYEntity/design.md) §15.

---

## 2. Implementation status

| Area | Code | Tests | Notes |
|------|------|-------|-------|
| Widget tree, hit-test, bubble events | ✅ | ✅ | `Controls/AYWidget.*` |
| Button, TextLabel, Image, Window | ✅ | ✅ | ⚠ see [§15.2](#152-known-issues) for known issues |
| VBox / HBox | ✅ | ✅ | Layout props in JSON **partial** ([§4.3](#43-gaps-to-close-in-code)) |
| `UILayoutLoader` + `WidgetFactory` | ✅ | ✅ | Primary data-driven path |
| `WidgetSerializer` round-trip | ✅ | ✅ | Editor/export path; see B11 |
| `I18n` + `isI18nKey` | ✅ | ✅ | |
| `StyleSheet` JSON | ❌ stub | — | R-5 ([§16.3](#163-phased-refactor)) unblocks |
| `EventBridge` | ❌ | — | Design only |
| `UIManager` | ✅ | ✅ | `Controls/AYUIManager.*`; see R4/R6 |
| `AYUIRenderBackend` | ❌ | — | AYRenderer U2 |
| Monorepo build | ❌ | — | Root `CMakeLists.txt` comments out AYUI / AYFont |
| Hot-reload (`isReloadNeeded`) | ❌ | — | B10: stub returns false; R-4 unblocks |
| New abstractions (InteractiveWidget, TextContent, Thickness) | ❌ | — | R-1..R-3 |

Namespace: **`ayt::ui`** everywhere. Deprecate `Events/AYEvent.h` (`ayui::events` stub).

Control inventory + known issues: see [§15](#15-control-inventory-2026-07-audit).  
Refactor plan + new control schedule: see [§16](#16-abstraction-refactor-plan).

---

## 3. Phase roadmap

AYUI v1.4 splits work into three lanes. **Refactor lane (R-*) must precede new-control lane (C-*)** because CheckBox / RadioButton / TextInput all need `InteractiveWidget` and `TextContent` to land first. Engine-integration lane (U-*) follows.

### 3.1 Refactor lane (precedes any new control)

| Step | Scope | Exit criteria |
|------|-------|---------------|
| **R-1** | Extract `InteractiveWidget` base from Button | Button extends InteractiveWidget; state machine (Normal/Hovered/Pressed/Disabled), `_isMouseOver`/`_isPressed`/`_enabled`, `onClicked` cursor hint move to base |
| **R-2** | Add `Thickness` and `TextContent` helpers in `Style/` | New struct types, used by Button for text+padding |
| **R-3** | Break `TextLabel : Button`; `TextLabel` extends `LeafWidget` | B1 fixed; Button owns text via `TextContent`, TextLabel owns its own text directly |
| **R-4** | Implement `UILayoutLoader::isReloadNeeded` via `std::filesystem::last_write_time` | B10 fixed; `UIManager::update` triggers real reload |
| **R-5** | Implement `StyleSheet::loadFromString` + wire `StyleManager::applyStyle` into Button / Window / TextLabel onRender | R1 fixed; hardcoded colors removed from widget `.cpp` files |
| **R-6** | Move `Widget::_hoverWidget` to `CompoundWidget` only; `UIManager` keeps its own (separate concern) | B4 fixed; cursor hint no longer leaks after mouse-up |
| **R-7** | `UIManager::tryReload` calls `cancelCapture()` before destroying tree | R6 fixed; no UAF during reload mid-drag |
| **R-8** | Fix `WidgetSerializer::serializeWidgetToJson` default `j["type"]` | B11 fixed; round-trip safe for raw `Widget` |
| **R-9** | Promote `IRenderBackend::BlendMode` / `PathFillMode` / `AnimationCurve` to namespace scope | R12 fixed; call sites use `ayt::ui::BlendMode::Normal` |
| **R-10** | Add `Panel` (background + optional border) as first visual container | Replaces using CompoundWidget directly with raw background |

### 3.2 New-control lane (after R-1..R-3 land)

| Step | Scope | Reuses |
|------|-------|--------|
| **C-1** | `Panel` | R-10 |
| **C-2** | `CheckBox` + `RadioButton` (+ `RadioGroup`) | R-1 InteractiveWidget |
| **C-3** | `TextInput` (single-line, IME + caret + selection + password) | new FocusableWidget + R-2 TextContent |
| **C-4** | `ScrollView` + `ScrollBar` (H/V) | new ScrollableWidget |
| **C-5** | `ListView` (single/multi select, virtualized) | C-4 |
| **C-6** | `ComboBox` (popup uses ListView) | C-5 + new FocusableWidget |
| **C-7** | `Slider` + `ProgressBar` | new ValueWidget helper |
| **C-8** | `GridPanel` (form layout, row/col/span) | new layout class |
| **C-9** | `TabControl` + `TabItem` | C-5 (header list) |
| **C-10** | `TextArea` (multi-line) | C-4 + C-3 |
| **C-11** | `Tooltip` + `MenuBar` / `Menu` / `MenuItem` + `ToolBar` / `Separator` + `StatusBar` | C-2 + new SelectableWidget |
| **C-12** | `TreeView` + `TreeNode` + `RichText` | C-5 |

Each `C-*` follows CLAUDE.md rule 5: unit test in `unittest/` + `WidgetFactory::registerCreator` + `WidgetSerializer` field.

### 3.3 Engine-integration lane

| Phase | Scope | Exit criteria |
|-------|-------|---------------|
| **U0** | Docs + CMake enable + link AYFont | AYUI + AYFont build in monorepo |
| **U1** | `UIManager` + hot-reload + StyleSheet JSON MVP | MockRenderer demo; reload `.ui.json` on change (depends on R-4, R-5) |
| **U2** | `AYUIRenderBackend` + hook in `RendererSubSystem::renderFrame` | HUD over `AYEngineIntegration_Demo` |
| **U3** | `EventBridge` + input (AYDevice / Win32) | Click/hover on loaded buttons |
| **U4** | Optional AYConfig file watch for shared hot-reload | Only if U1 watch insufficient |
| **U5+** | 3D UI, mask hit-test, batching | Post-AYUI v1 |

AYRenderer **R5+** (shadow, GBuffer) does not block AYUI U1–U3.

---

## 4. Data-driven layer (reuse assessment)

**Conclusion: reuse current code.** Do not introduce a second config stack for v1.

### 4.1 What to reuse as-is

| Component | Role | Reuse |
|-----------|------|-------|
| **`UILayoutLoader`** | Parse layout JSON → widget tree | ✅ Primary entry |
| **`WidgetFactory`** | `type` string → widget ctor | ✅ Extensible via `REGISTER_WIDGET` |
| **`WidgetSerializer`** | Round-trip JSON for tools/tests | ✅ Keep parallel to loader |
| **`I18n`** | Resolve `ui.*` text keys in loader | ✅ Already wired in `buildWidgetTree` |
| **`bindEvent` + JSON `onClick`** | C++ handlers bound by widget `id` | ✅ Pattern works; document contract |
| **`Widget::setStyleId`** | Indirect style reference | ✅ Ready for StyleSheet |
| **nlohmann/json** | Parsing | ✅ Via CMake `find_package` |

Loader already supports (see `Loader/AYLayoutLoader.cpp`):

- `type`, `id`, `position`, `size`, `visible`, `style`, `text`, `children`
- i18n: `"text": "ui.menu.resume"` when key prefix is `ui.`
- events: `"onClick": "<ignored string>"` — handler comes from `bindEvent(id, "onClick", fn)`

### 4.2 Canonical layout JSON (v1 contract)

Aligned with unit tests and loader implementation:

```json
{
  "type": "Window",
  "id": "pause_menu",
  "position": { "x": 100, "y": 100 },
  "size": { "w": 400, "h": 300 },
  "visible": true,
  "style": "panel_default",
  "text": "ui.menu.pause_title",
  "children": [
    {
      "type": "VBox",
      "id": "menu_stack",
      "spacing": 10,
      "children": [
        {
          "type": "Button",
          "id": "btn_resume",
          "text": "ui.menu.resume",
          "size": { "w": 200, "h": 40 },
          "onClick": "resume_game"
        }
      ]
    }
  ]
}
```

**File convention:** `*.ui.json` (layout tree). Optional companion `*.ui.styles.json` (U1).

**Event binding (code-side, not in JSON executable logic):**

```cpp
loader.loadFromFile("pause.ui.json");
loader.bindEvent("btn_resume", "onClick", []{ /* resume */ });
// Loader connects binding when it sees "onClick" on widget id btn_resume
```

### 4.3 Gaps to close in code (U1, same JSON model)

Extend **`UILayoutLoader::buildWidgetTree`** — do **not** replace the loader.

| JSON field | Target | Status |
|------------|--------|--------|
| `spacing`, `padding` | `VBox` / `HBox` | ✅ shipped (PR-3 B1 lane) |
| `gravity` | `VBox` / `HBox` | 🟡 **L4 in loader (2026-07-26)** — `BoxBase::setGravity` exists, loader reads `gravity` field |
| `fontSize`, `texture`, `closable`, … | per-widget | 🟡 **[§4.3a Loader parity patch (L1..L4)](#43a-loader-parity-patch-l1l4-2026-07-26)** closing this row |
| `StyleSheet::loadFromFile` | named styles | ✅ shipped (R-5 / G11 Theme system) |
| `isReloadNeeded` | file mtime / FileWatcher | ✅ shipped (R-4) |
| JSON Schema validation | optional | U1 nice-to-have |

### 4.3a Loader parity patch (L1..L4, 2026-07-26)

The `WidgetSerializer` (used by Editor / tests / round-trip) has been **consistently ahead** of the `UILayoutLoader` (used at runtime to load `*.ui.json`). Hosts that ship a layout JSON end up silently losing fields that the serializer happily exports. Four follow-up sub-cuts close the gap; each ships as its own commit with its own test.

| Sub-cut | Field | Target widget | Why |
|---------|-------|---------------|-----|
| **L1** | `text` | `TextInput` / `TextArea` / `Tooltip` (`Tooltip` is host of `setText`); also `MenuItem` (already partial) | Editor inspector's `inp_pos_x` shows `"0.00"` on load — loader currently ignores `TextInput.text` and the Inspector refresh reburns the live value. Same root for `TextArea` and `Tooltip`. |
| **L2** | `textureName` | `Image` | `TextureRegistry` + `ImageTextureHandle` are landed (G10). Loader currently only reads `color`; a named texture in JSON is silently dropped. |
| **L3** | `cells` (object array: `{ row, col, rowSpan, colSpan, hAlign, vAlign }` ) | `GridPanel` | `GridPanel::setCell(int,int,Widget*,rowSpan,colSpan,hAlign,vAlign)` exists, but the loader only consumes `rowCount` / `columnCount` + flat `children` (auto-linear). A real `form layout` (`{row:0,col:1,rowSpan:2}`) round-trip is broken today. |
| **L4** | `gravity` | `VBox` / `HBox` | `BoxBase::setGravity(Gravity)` exists since v1.0; loader only reads `spacing` / `padding`. |

**Out of scope here** (explicitly deferred to a later patch):

- `closable` on `Window` (DECISION 4 in `AYWindow.h` — clamp GDI; deferred to v1.5 §17 docking cards).
- Every serializer field that the loader does not need at runtime (e.g. `selectionMode` round-trip on `ListView` is valuable for tests but the editor inspector does not need it — serializer already covers it).

**INV-L** invariants (must hold before/after):

1. Existing `*.ui.json` for `editor_shell` (in `AYEditor/assets/ui/`) **must keep parsing unchanged** — net-additive only.
2. Each new loader branch is guarded by `dynamic_cast` so a misregistered widget type degrades to "field ignored" (existing printer behavior).
3. Loader remains a **single-pass walk** of the JSON tree; no second pass for `cells` (the grid children list must be walked inside `setCell` calls, not via `addChild`).

### 4.4 i18n data format (reuse)

Separate lang table file (already supported by `I18n::loadFromFile`):

```json
{
  "ui.menu.resume": { "zh": "继续游戏", "en": "Resume" },
  "ui.menu.pause_title": { "zh": "暂停", "en": "Paused" }
}
```

Loader calls `isI18nKey()` + `I18n::resolve()` when building widgets.

### 4.5 Style data format (U1 — extend, don't fork)

**Hybrid (unchanged from v1.2):** code defaults via `StyleBuilder` + JSON overrides.

Proposed `pause.ui.styles.json`:

```json
{
  "styles": {
    "panel_default": {
      "backgroundColor": [0.12, 0.12, 0.14, 0.95],
      "border": { "width": 1, "cornerRadius": 6 }
    },
    "button_primary": {
      "backgroundColor": [0.25, 0.45, 0.85, 1.0]
    }
  }
}
```

Flow:

1. App loads styles JSON → `StyleSheet::loadFromString` (implement stub)
2. Layout JSON references `"style": "button_primary"`
3. `StyleManager::getComputedStyle` → widget draw via `IRenderBackend`

No need for AYConfig key-path API for UI v1.

### 4.6 When to consider AYConfig (U4+, optional)

| Use AYConfig | Stay with UILayoutLoader |
|--------------|---------------------------|
| Global engine settings shared with non-UI | Layout trees |
| Hierarchical keys + layer merge | Widget graphs with `children` |
| Already used for game tuning | i18n tables (simple flat JSON) |

**Decision:** v1 data-driven path = **UILayoutLoader + StyleSheet + I18n**. Revisit AYConfig only for cross-module config or unified file watching at scale.

### 4.7 Serializer vs Loader

| | `UILayoutLoader` | `WidgetSerializer` |
|--|------------------|---------------------|
| Purpose | Runtime load + id map + events | Export/import, tests |
| i18n resolve | Yes at load | No (stores raw text) |
| Event bindings | Via `bindEvent` | N/A |

Keep both; optionally share a private `WidgetJsonIO` helper in U1 to avoid field drift.

---

## 5. Event system

### 5.1 Principle

**Internal events stable; external input adapted via EventBridge (U3).**

### 5.2 Internal (implemented)

Use **`UIEvent` / `UIEventType`** in `Controls/AYWidget.h` only:

- Bubble: `Widget::bubbleEvent`
- Handlers: `addEventHandler(UIEventType, ...)`
- Controls implement mouse/keyboard → internal events

Remove or repurpose `Events/AYEvent.h` (wrong namespace, unused).

### 5.3 External (U3)

```
Input (AYDevice / platform)
  → EventBridge::translate
  → root Widget hit-test + dispatch UIEvent
```

EventBridge **does not** duplicate `UIEvent` types. It maps platform events to existing `UIEventType` values.

### 5.4 JSON `onClick` vs runtime events

- JSON `"onClick": "resume_game"` is an **identifier for documentation/tools only**
- Runtime wiring: **`UILayoutLoader::bindEvent(widgetId, "onClick", handler)`** before or after load (if after load, require re-bind API in U1 — today bindings apply during `buildWidgetTree`)

---

## 6. Rendering

### 6.1 `IRenderBackend`

Interface: `Style/IAYRenderBackend.h`. v1 **MVP subset**:

| Required for U2 | Defer (default no-op) |
|-----------------|------------------------|
| `beginFrame` / `endFrame` | Render targets, particles |
| `beginCanvas` / `endCanvas` | Blur, complex shadows |
| `drawRect`, `drawText`, `drawImage` | Nine-patch (use stretched rect first) |
| `clipRect`, `setTransform` | |

Implementations:

| Backend | Location | Phase |
|---------|----------|-------|
| `MockRenderer` | `AYUI/Style/` | ✅ tests |
| `AYUIRenderBackend` | `AYRenderer/src/` (proposed) | U2 |

Uses `ayt::font::FontHandle` — requires **AYFont** linked (U0).

### 6.2 Frame composition (U2)

In `RendererSubSystem::renderFrame`, after `Renderer::render(_scene)`:

```cpp
_renderer.beginFrame(clear);       // 3D
_renderer.render(_scene);
_uiManager.render(_uiBackend);     // 2D overlay, same swap chain
_renderer.endFrame();
```

UI uses orthographic pixel space matching window size; Y down.

---

## 7. UIManager

**Not implemented.** v1 spec:

```cpp
class UIManager {
public:
    static UIManager& get();

    void initialize(IRenderBackend* backend);
    void shutdown();

    bool loadLayout(const std::string& path);   // wraps UILayoutLoader
    void setLangTable(const std::string& path);

    void update(float dt);   // optional animations
    void layout();           // root->layout() if size changed
    void render();           // root->render(*_backend)

    Widget* root() const;
    Widget* findById(const std::string& id) const;

    void pollHotReload();    // mtime → reload layout/styles
};
```

Owned by `UISubSystem` (GameLoop) or `RendererSubSystem` (render thread policy TBD). v1: **same thread as GameLoop**, after logic update, before or inside render callback.

---

## 8. Engine integration

### 8.1 Subsystems

| Subsystem | Priority | Time | Responsibility |
|-----------|----------|------|----------------|
| `UISubSystem` | after Entity | Unscaled | `UIManager::update`, input bridge, hot-reload poll |
| `RendererSubSystem` | existing | Scaled | 3D + UI composite draw |

No `bootstrapModule` for UI until static-init registration is needed (copy AYEntity pattern if MSVC drops TU).

### 8.2 Dependencies

```
AYUI  → AYMath, AYFont (U0), nlohmann_json
AYRenderer (U2) → implements AYUIRenderBackend, calls UIManager::render
AYGameLoop → UISubSystem tick
AYDevice (U3) → pointer/keyboard → EventBridge
```

### 8.3 Build

Enable in root `CMakeLists.txt`:

```cmake
add_subdirectory(AYRuntime/AYFont)
add_subdirectory(AYRuntime/AYUI)
```

Fix AYUI CMake: link `AYFont` PRIVATE/PUBLIC; move `AYTest` to unittest target only.

---

## 9. Directory structure

**Current (repo):**

```
AYUI/
├── design.md                 ← this file
├── README.md
├── AYUI-v1-Design.md         ← v1.2 archive
├── CLAUDE.md
├── AYUI.h
├── Controls/
├── Layout/
├── Style/
├── Loader/
├── i18n/
├── Events/                   ← AYEvent.h stub; replace with EventBridge in U3
└── unittest/
```

**Planned additions:**

```
Controls/   → (optional) AYUIManager.h/cpp in U1
Events/     → AYEventBridge.h/cpp in U3
AYRenderer/src/detail/AYUIRenderBackend.* in U2
```

---

## 10. Testing strategy

- Continue **AYTest** in `unittest/` (~55 cases)
- U1: StyleSheet parse tests, loader `spacing`/`padding`, hot-reload mtime
- U2: integration test or demo executable `AYUIDemo` overlay on engine window
- No compile in AI agent loop (project rule)

---

## 11. Decisions log

| Date | Decision |
|------|----------|
| 2026-05 | JSON + nlohmann; hybrid styles; i18n keys `ui.*` |
| 2026-07 | Align with AYRenderer R4 engine loop; defer 3D UI |
| 2026-07 | **Reuse UILayoutLoader** for data-driven v1; StyleSheet JSON next |
| 2026-07 | Do not depend on AYConfig for UI v1 |
| 2026-07 | Single event model: `UIEvent`; deprecate `AYEvent.h` |
| 2026-07 | `AYUIRenderBackend` lives in AYRenderer, not legacy AliyatRenderer path |
| 2026-07 | Editor chrome is AYUI; modes/session in AYEditor ([§13](#13-editor-chrome)) |
| 2026-07-17 | **v1.4 audit:** `TextLabel : Button` flagged as wrong abstraction; introduce `InteractiveWidget` base before any new control. Refactor lane R-1..R-10 precedes new-control lane C-1..C-12. See [§15](#15-control-inventory-2026-07-audit) + [§16](#16-abstraction-refactor-plan). |

---

## 12. Reserved

_Section 12 intentionally reserved for future insertion. The v1.4 audit (§15) and refactor plan (§16) live at the end of the document so renumbering stays stable._

---

## 13. Editor chrome

AYUI provides **screen-space widgets** for the editor shell; it does **not** own Edit/Play mode or scene data.

| Concern | Module |
|---------|--------|
| Layout JSON (`editor_shell.ui.json`), draw toolbar/panels | **AYUI** (this module) |
| `EditorMode`, GameLoop control, viewport rect | **[AYEditor](../AYEditor/design.md)** |
| `BuildType::Editor`, subsystem registration | **[AYApplication](../AYApplication/design.md)** |
| Mode semantics (Edit/Play/Paused/Simulate) | **[AYExtension §3](../AYExtension/design.md)** |

### 13.1 AYUI responsibilities in editor

- Load chrome via **`UILayoutLoader`** — same JSON contract as [§4.2](#42-canonical-layout-json-v1-contract).
- i18n keys: `ui.editor.*` (toolbar labels).
- **`bindEvent`** for Play/Pause/Step/Stop — handlers live in `EditorSession`, not in JSON logic.
- Render with **`UIManager`** + `IRenderBackend` (Mock in E0 demo; `AYUIRenderBackend` in E2).
- **Unscaled** update when driven by `UISubSystem` / editor tools tick.

### 13.2 Out of scope for AYUI

- Inspector property fields from reflection (AYEditor E4+).
- Hierarchy tree bound to `World` (AYEditor E4+).
- Scene save/load (AYSerializer — AYEditor E4+).

See [AYEditor/design.md §5](../AYEditor/design.md#5-editor-chrome-ayui) for shell layout and demo phases **E0–E3**.

---

## 14. References

- [AYUI-v1-Design.md](AYUI-v1-Design.md) — v1.2 detailed notes (some paths obsolete)
- [CLAUDE.md](CLAUDE.md) — AI/dev conventions
- [AYEditor/design.md](../AYEditor/design.md) — editor system and chrome JSON
- [AYRenderer/README.md](../AYRenderer/README.md)
- [AYEntity/design.md](../AYEntity/design.md)

---

## 15. Control inventory (2026-07 audit)

A full read-through of every existing `.h`/`.cpp` in `Controls/` and `Layout/` produced this inventory + the issues below. The refactor plan in [§16](#16-abstraction-refactor-plan) is derived from these findings.

### 15.1 Implemented today (7)

| Widget | Class | Type | JSON fields | Notes |
|--------|-------|------|-------------|-------|
| `Widget` | `ayt::ui::Widget` | base | `position` / `size` / `visible` / `style` | Leaf base; tree, hit-test, bubble |
| `CompoundWidget` | `ayt::ui::CompoundWidget` | container base | (inherits) | Hosts children, no own render |
| `Button` | `ayt::ui::Button` | interactive | `text` + `onClick` | Hover/Pressed/Disabled state machine; **owns state machine that should be in `InteractiveWidget`** (B1) |
| `TextLabel` | `ayt::ui::TextLabel` | display | `text` / `fontSize` / `color` / `align` / `wordWrap` | **Inherits `Button`** to get text/draw helpers — wrong abstraction (B1) |
| `Image` | `ayt::ui::Image` | display | `color` (texture handle not yet in JSON) | `void*` texture, no lifetime management (R7) |
| `Window` | `ayt::ui::Window` | frame | `title` / `movable` / `resizable` / `minSize` / `titleBarHeight` | Drag, clamp, hit-test override; resizable TODO |
| `VBox` / `HBox` | `ayt::ui::VBox` / `HBox` | layout | `spacing` / `padding`; HBox: `slot.{min,max}Width{,Percent}` | fill/fixed slots; `SplitterHandle` sub-widget |

`SplitterHandle` is `HBox`-internal and not exposed as a standalone factory type.

### 15.2 Known issues (must address before/with new controls)

Severity: 🔴 functional bug, 🟡 risk/design smell.

| ID | Sev | Location | Issue |
|----|-----|----------|-------|
| **B1** | 🔴 | `Controls/AYTextLabel.h:7` | `TextLabel : Button` — wrong abstraction; forces TextLabel to `setEnabled(false)` to neutralize Button state machine |
| **B2** | 🟡 | `Controls/AYButton.cpp:24-39` | Disabled path sets `_state = Disabled` but never clears `_isMouseOver` — re-enable after hover leaves widget stuck |
| **B3** | 🟡 | `Controls/AYWindow.cpp:11-23` | `clampAxis` mixes `_titleBarHeight` into minKeepWidth — wrong axis |
| **B4** | 🟡 | `Controls/AYWidget.cpp:108-136` + `Controls/AYUIManager.cpp:25-34` | `_hoverWidget` dual ownership (Widget + UIManager); cursor hint leaks past mouse-up (workaround exists in `UIManager::onMouseButtonUp`) |
| **B5** | 🟡 | `Controls/AYWidget.cpp:98-105` | `const_cast` inside const `getWorldBounds()`; multi-thread UI would break |
| **B6** | 🟡 | `Controls/AYWindow.cpp:43-46` | `setMinSize` triggers `setSize` — implicit resize, no test covers |
| **B7** | 🟡 | `Controls/AYImage.cpp:16-18` | `Image::performLayout` is a no-op override that exists only because Image extends `CompoundWidget` — coupling mismatch |
| **B8** | 🟡 | `Layout/AYBox.h` + `Controls/AYButton.h` + `Controls/AYWindow.cpp` | No unified `Thickness`/`Padding` type — `FVector4(L,T,R,B)` ad-hoc everywhere; Editor reflection will be hard |
| **B9** | 🟡 | `Controls/AYButton.cpp:75-83` | `Button::setPadding` only affects text drawing, not hit area or layout — surprising semantics |
| **B10** | 🔴 | `Loader/AYLayoutLoader.cpp:122-135` | `isReloadNeeded` always returns `false` — hot-reload is broken (design.md §2 table says ✅) |
| **B11** | 🟡 | `Loader/AYWidgetSerializer.cpp:135` | `serializeWidgetToJson` sets `j["type"] = widget->getStyleId()` first; if no specific branch fires (raw `Widget`), exported type is the style id |
| **R1** | 🟡 | All `Controls/*.cpp` `onRender` | Hardcoded colors/font sizes; `StyleSheet`/`StyleManager` system never wired into widgets |
| **R2** | 🟡 | `Controls/AYTextLabel.h:54-67` | `setTextColor`/`setBackgroundColor` store as 4 floats instead of `FVector4` |
| **R4** | 🟡 | `Controls/AYUIManager.cpp:51-67` | `ensureBuiltInFactoriesRegistered` is a workaround for anonymous-namespace registrars being subject to MSVC COMDAT stripping |
| **R5** | 🟡 | `Controls/AYWidget.h:200` | `Widget::_hoverWidget` is only meaningful for compound widgets — should live in `CompoundWidget` |
| **R6** | 🟡 | `Controls/AYUIManager.cpp:170-180` | `tryReload` destroys tree without `cancelCapture()` — UAF if reload fires mid-drag |
| **R7** | 🟡 | `Controls/AYImage.h` | `_textureHandle` void*; no release path |
| **R9** | 🟡 | `Events/AYEvent.h` | Dead stub from old namespace; design.md §2 says "deprecate" — actually delete |
| **R10** | 🔴 | `Style/AYStyle.cpp` missing | `StyleSheet::loadFromString` / `loadFromFile` / `StyleBuilder::makeDefault` etc. never implemented; whole style system is empty shell |
| **R12** | 🟡 | `Style/IAYRenderBackend.h` | `BlendMode`/`PathFillMode`/`PathWinding`/`AnimationCurve`/`BorderStyle::Position` nested inside `IRenderBackend` class — call sites need full qualification |

### 15.3 Missing widgets (priority order)

**P0 — required for any meaningful UI / editor shell (post-refactor):**
`Panel`, `CheckBox`, `RadioButton` (+ `RadioGroup`), `TextInput`, `TextArea`, `ComboBox`, `ScrollView` (+ `ScrollBar`), `ListView`, `Slider`, `ProgressBar`, `TabControl`, `GridPanel`

**P1 — needed for editor chrome:**
`MenuBar` / `Menu` / `MenuItem`, `ToolBar` / `ToolButton`, `Separator`, `StatusBar`, `TreeView`, `RichText`, `Tooltip`

**P2 — visual / advanced:**
`ModalDialog` / `MessageBox`, `DragSource` / `DropTarget`, `ColorPicker`, `DockWidget`, `FloatingPanel`, `Accordion` (CollapsiblePanel), `Pagination`, `Canvas`

### 15.4 Missing layouts

Previously deferred in v1.3 ([§1.2](#12-non-goals-deferred)); now part of v1.4 scope:

| Layout | Use case | Priority |
|--------|----------|----------|
| `GridPanel` | Form layout, inventory grid | P0 |
| `RelativeLayout` (anchor) | Anchored resize-safe positioning | P1 |
| `StackPanel` | Overlay/Tooltip/Modal layering | P1 |
| `WrapPanel` | Auto-wrap toolbar/menu items | P2 |

---

## 16. Abstraction refactor plan

v1.3 added 7 widgets and exposed a missing middle of the hierarchy. v1.4 inserts **three new abstraction layers** before adding the next 10+ widgets, otherwise CheckBox/RadioButton/TextInput/Slider/etc. each re-invent the same state machine.

### 16.1 New class hierarchy

```
Widget
├── InteractiveWidget          // R-1: enable + hover/pressed + cursor hint + click
│   ├── Button
│   ├── CheckBox
│   ├── RadioButton (+ RadioGroup)
│   └── ToolButton / MenuItem
├── LeafWidget                 // leaf renderers (Image / TextLabel / ProgressBar)
├── FrameWidget                // Window (drag/close/minimize) — extracted from Window
├── FocusableWidget            // TextInput / ComboBox — caret + IME + tab order
├── SelectableWidget           // ListView item / TabItem / TreeNode
├── ValueWidget<T>             // Slider / ProgressBar / CheckBox (value + onChange)
└── ContainerWidget            // renamed CompoundWidget
    ├── Panel                  // R-10
    ├── VBox / HBox / BoxBase
    ├── ScrollView             // C-4
    ├── GridPanel              // C-8
    ├── WrapPanel
    ├── TabControl             // C-9
    └── StackPanel
```

### 16.2 New helper structs (in `Style/`)

```cpp
struct Thickness {                  // L,T,R,B — replaces ad-hoc FVector4 padding
    float left = 0, top = 0, right = 0, bottom = 0;
    static Thickness uniform(float v) { return {v, v, v, v}; }
    static Thickness symmetric(float h, float v) { return {h, v, h, v}; }
};

struct TextContent {                // R-2: shared text rendering payload
    std::wstring text;
    std::wstring fontFamily;
    int fontSize = 14;
    math::FVector4 color{1, 1, 1, 1};
    enum class HAlign { Left, Center, Right };
    enum class VAlign { Top, Center, Bottom };
    HAlign hAlign = HAlign::Left;
    VAlign vAlign = VAlign::Top;
    Thickness padding;              // text-box padding inside the widget
    bool wordWrap = false;
    float wrapWidth = 0.0f;
};
```

### 16.3 Phased refactor

The full step list (R-1..R-10) is in [§3.1](#31-refactor-lane-precedes-any-new-control). Summary of what each step buys:

| Step | Fixes | Enables |
|------|-------|---------|
| R-1 | B1 (partial) | CheckBox / RadioButton / ToolButton (C-2, C-11) |
| R-2 | R2, B8 | TextLabel refactor (R-3); TextInput (C-3) |
| R-3 | B1 | Clean LeafWidget; TextInput can extend FocusableWidget instead of inheriting weird state |
| R-4 | B10 | Real hot-reload — required for editor live-edit |
| R-5 | R1, R10 | No more hardcoded colors; StyleSheet JSON MVP unblocks |
| R-6 | B4, R5 | Cursor hint correctness; sets up multi-thread-safe hit-test |
| R-7 | R6 | Reload mid-drag no longer UAFs |
| R-8 | B11 | Editor round-trip safe for raw `Widget` |
| R-9 | R12 | Cleaner call sites in C-* widgets |
| R-10 | (no bug) | `Panel` (C-1) — first visual container |

### 16.4 New control schedule

The full C-1..C-12 list with dependencies is in [§3.2](#32-new-control-lane-after-r-1r-3-land). Per-CLAUDE.md rule 5 each step delivers:

1. `Controls/AY{Name}.h/cpp`
2. `WidgetFactory::registerCreator("Name", ...)` in `AYWidgetFactory.cpp`
3. `AYWidgetSerializer` field support for serialization round-trip
4. `unittest/Test_{Name}.cpp` with at least: construction/default state, key setter/getter, one interaction path, one render path

### 16.5 Out of scope for v1.4 refactor

- 3D spatial widgets (`SpatialWidget`, `UIPlane`, `WorldUI`) — still deferred to U5+
- Pixel-mask hit-test
- Replacing engine event system (U3 stays)
- `AYConfig` integration (U4 only if needed)

---

## 17. Docking & Sub-Window (v1.5 roadmap)

> **Goal:** make AYUI support **stop-dockable panels** (cards that can be left, right, top, bottom, center, or torn off into a floating sub-window) and **child windows** (independent toplevel OS windows for tools such as Material Editor, Node Graph, RenderDoc capture). The editor's `editor_shell.ui.json` is the first consumer; the engine can later use the same primitives for in-game dialogs (Inventory, Settings).

### 17.1 Vocabulary

| Term | Meaning |
|------|---------|
| **DockCard** | A leaf panel — `Panel` / `Window` content with a `title` + `icon`, host of a single widget tree. The "thing" the user moves. |
| **DockSlot** | A named region on the shell — `Left`, `Right`, `Top`, `Bottom`, `Center`. Slots have a `weight` and a `split` (HSplitter / VSplitter) so multiple cards can stack. |
| **DockArea** | The shell-frame root that holds the slots + a `DockOverlay` (floating cards). One per `EditorSession`. |
| **FloatingCard** | A card that has been torn off a slot; lives on `DockOverlay` and can be re-docked or closed. |
| **ChildWindow** | An OS-level top-level window (own `HWND` on Windows) that hosts a single `UIManager`. Tools that should survive host minimize/close often use this form. |

### 17.2 Data model (v1.5 contract)

Builds on the existing `*.ui.json` (no new config stack). The dump below is illustrative:

```json
{
  "type": "DockArea",
  "id": "editor_dock",
  "slots": {
    "Left":  { "weight": 0.20, "minWidth": 180 },
    "Right": { "weight": 0.25, "minWidth": 220 },
    "Top":   { "weight": 0.00, "visible": false },
    "Bottom":{ "weight": 0.00, "visible": false },
    "Center":{ "weight": 0.55 }
  },
  "cards": [
    {
      "id": "card_post_look",
      "title": "Post / Look",
      "slot": "Left",
      "order": 0,
      "content": { "type": "VBox", "id": "post_look_root", "children": [ /* sliders */ ] }
    },
    {
      "id": "card_inspector",
      "title": "Inspector",
      "slot": "Right",
      "order": 0,
      "content": { "type": "VBox", "id": "inspector_root", "children": [ /* rows */ ] }
    },
    {
      "id": "card_viewport",
      "title": "Viewport",
      "slot": "Center",
      "order": 0,
      "content": { "type": "Image", "id": "panel_viewport", "color": [0.05, 0.06, 0.07, 1.0] }
    }
  ],
  "floating": [
    {
      "id": "card_console",
      "title": "Console",
      "position": { "x": 50, "y": 600 },
      "size":     { "w": 600, "h": 200 },
      "content": { "type": "TextArea", "id": "console_root" }
    }
  ]
}
```

Each `slots` entry maps to a leaf `VBox` / `HBox` (already in v1) so the existing layout / splitter / drag machinery keeps working. `cards` dequeue content onto the slot's child VBox in `order` order. `floating` lives on `DockOverlay`.

### 17.3 Runtime API (C++)

```cpp
class DockArea : public CompoundWidget {
public:
    enum class Slot { Left, Right, Top, Bottom, Center };
    void addCard(Slot slot, std::unique_ptr<DockCard> card);
    void dockCard(DockCardId id, Slot slot);          // from float → slot
    void floatCard(DockCardId id, FVector2 pos);      // slot → float
    void closeCard(DockCardId id);
    DockCard* findCard(DockCardId id) const;
    void saveLayout(std::string& outJson) const;      // for persistence
    void loadLayout(const std::string& json);        // restore
};

class DockCard : public Panel {
public:
    DockCardId getId() const;
    void setTitle(const std::wstring&);
    void setIcon(const std::wstring&);               // optional glyph
    void setContent(Widget* w);                       // owns
    void setClosable(bool);
    void setFloatable(bool);                          // can be torn off
};

class DockOverlay : public CompoundWidget {
    // floating cards live here; hit-test passes through to DockArea
    // when no floating card is hit (preserves existing F3 freecam
    // "isPointOnChrome" logic — see AYEditorSession.cpp:299).
};

class ChildWindow : public NonCopyable {
public:
    bool open(const ChildWindowDesc& desc);
    void close();
    UIManager& ui();                                  // own UIManager per window
    void pump(float dt);
    void render();
};
```

### 17.4 Loader bridge (v1.5)

The Loader's `buildWidgetTree` adds three branches:

- `type == "DockArea"` → builds slots + iterates `cards[]` / `floating[]`
- `type == "DockCard"` → builds a card (used when nesting a card inside a slot)
- `type == "ChildWindow"` → registers a runtime-created `ChildWindow` (not built as a `Widget`; the loader invokes a host callback so the Editor can open it on load)

The four L1..L4 sub-cuts ship **before** §17.3 so the dock's content widgets already parse cleanly.

### 17.5 Sub-cuts (mirrors §11b PR discipline)

| Sub-cut | Scoped to | Exit criteria |
|---------|-----------|---------------|
| **D1** | `DockArea` + `DockCard` + `DockOverlay` widgets (no floating, no persistence) | 3 cards live-edit in `editor_shell.ui.json`; tear-off UX deferred to D3 |
| **D2** | Loader: `DockArea` / `DockCard` / `slots` / `cards` / `floating` parsing | `editor_shell.ui.json` round-trip with `DockArea`; existing flat layout stays parseable |
| **D3** | Floating cards: drag a card's title bar outside the slot to tear off; re-dock by dropping | Mouse path through `UIManager` + `DockOverlay`; tests in `Test_DockFloat.cpp` |
| **D4** | Layout persistence (`saveLayout` / `loadLayout`) + Editor session edits `~/.ay/editor_layout.json` | Same DockArea content across launches |
| **D5** | `ChildWindow` (own `HWND`, own `UIManager`) | Demo opens a Material Editor child window from a menu item |

D1..D5 are **implementation-ready** but each requires its own plan + commit slices. §17 in v1.5 is the **vision track**; the next plan-mode entry picks D1 vs D2 first based on §11.

### 17.6 Non-goals for v1.5

- Auto-hide sliding panels (drawer) — **defer** to v1.6
- Cross-window drag (drag a card from window A onto window B) — **defer** to v1.6 unless D5 demands it
- Per-card theme override UI — D1 cards inherit area theme; theme overrides still come through `Widget::setStyleTokenOverride` (G11)
- 3D viewport embedded in a floating card — depends on RenderBackend v2 (`UIRenderBackendExt3D`); see AYEditor gap analysis

### 17.7 Reference loaders

Docking is a well-explored pattern. v1.5 borrows the **data shape** from Qt's `*.ui` save format (slots + cards + floating) and the **drag UX** from Unity's editor layout / ImGui's `DockBuilder` (single tree + named nodes). Avoids re-inventing the persistence format.

---

## 18. API additions from 2026-08-02 code review

The 2026-08-02 review (full report at `AYDocs/AYUI_CodeReview_2026-08-02.md`) found 22 issues across the existing C++ surface; 17 HIGH/MEDIUM/LOW items landed in commits `7502ca7` (CRITICAL #1–5) and `0ae3de5` (HIGH/MEDIUM/LOW #6–22). A re-scan of the Style / i18n / base layer landed in `c93e5a0`. This section documents the API surface that emerged from those fixes so the next iteration doesn't lose them.

### 18.1 Tooltip — independent lifetime from target

**Context.** `Tooltip::attachTo(target)` mounts the tooltip on `UIManager::_overlayRoot`, NOT as a child of `target`. The header comment previously claimed the tooltip was an OWNING child of target — that was wrong.

**New API:**
```cpp
// Controls/AYTooltip.h
void Tooltip::detach();   // null _target + unmount from overlay + hide()
```

`detach()` is the orderly shutdown path. Hosts that store a `Tooltip*` and outlive `target` should call `detach()` BEFORE deleting the target. `tick(dt, mousePos, viewport)` also guards `_target == nullptr` and early-returns without dereferencing — defense in depth for hosts that forget to detach.

### 18.2 DockArea — slot-to-slot moves via `moveInSlot`

**Context.** The original drop callback for docked → docked cards routed through `floatCard()` then `dockCard()` — the card briefly flashed at the cursor position before snapping into the new slot. Code-review §8.

**New API:**
```cpp
// Controls/AYDockArea.h
bool DockArea::moveInSlot(const std::string& cardId, Slot target);
```

`moveInSlot()` reparents a docked card directly to the target slot without routing through the overlay. K-INV-D3-1 (same-slot no-op) is handled inside. The `onDrop` callback now calls `moveInSlot` for the docked→docked case; `floatCard` + `dockCard` are still used for docked→overlay and overlay→slot transitions respectively.

### 18.3 Modal — dimmer ownership split

**Context.** `Modal::setDimmer(d)` previously had ambiguous ownership: stack-allocated `Dimmer&` (test pattern, member vars) and heap-allocated `Dimmer*` (the common `setDimmer(new Dimmer())` pattern) had the same API, so the heap case leaked forever and the stack case UAF'd on premature host-side free. Code-review §12.

**New API:**
```cpp
// Controls/AYModal.h
void Modal::setDimmer(Dimmer* dimmer);        // NON-OWNING attach. Host owns lifetime.
void Modal::setDimmerOwned(Dimmer* dimmer);   // OWNING attach. ~Modal deletes.
```

Use:
- `m.setDimmer(&d)` for stack-allocated / member-var dimmers.
- `m.setDimmerOwned(new Dimmer())` for heap-allocated transient dimmers (the common Editor case).

`~Modal()` only deletes `_dimmer` when `_dimmerOwned == true` (set by `setDimmerOwned`). The `_dimmerOwned` flag defaults to false.

### 18.4 Menu — owner-host break path for dtor order

**Context.** When `MenuBar` is destroyed with an open Menu that was reparented onto the overlay, the Menu stays on the overlay indefinitely AND `MenuBar::findAccel()` dereferences the orphan Menu* on every accelerator dispatch. Code-review §5 + §6.

**New API:**
```cpp
// Controls/AYMenu.h
void Menu::clearOwnerHost();   // null _ownerHost so close() doesn't reparent into a freed host
```

~MenuBar walks its `_menus` snapshot, calls `e.menu->clearOwnerHost()` + `e.menu->close()` (reparents back) + `e.menu->detachFromParent()` + `delete e.menu`. The accessor exists so the dtor sequence can break the back-pointer without exposing `_ownerHost` as a public field.

### 18.5 Lifetime rules (recap)

Across the 2026-08-02 fixes, the following invariant is now enforced uniformly:

> **Every widget dtor that owns child widgets must call `destroyWidgetTree` or its equivalent before the dtor finishes — `~Widget()` itself does NOT delete children.**

Widgets that historically relied on `~Widget()` to clean up via `addChildExternal` (ListView scrollbar + pool rows, ScrollView bars, TabStrip buttons, DockOverlay floating cards) had real leaks that ASAN would now catch. The 2683/2683 test suite with `AY_ENABLE_ASAN=ON` (`Test_Leak` suite) is the regression guard.

### 18.6 Style / i18n caveats

Documented limitations from the Sweep #3 fixes:

- **Theme sheet fragments** are eagerly parsed at `Theme::loadFromJson` time; literal `$token` slots expand against the *currently active* theme at that moment. After `setActiveTheme()`, the fragment's `WidgetStyle.backgroundColor / border.color / textColor` are FROZEN. The `resolveStyle()` path (Sweep3-#3 fix) sidesteps this for token-captured slots — they re-expand against the active theme on every call. Hosts that need fully-dynamic sheets should keep token refs in fragment JSON, not literal arrays. Full re-parse-on-build is future work.
- **`I18n::resolve()` (wstring overload)** decodes UTF-8 properly since the 2026-08-02 fix. Surrogate pairs are emitted on Windows (`sizeof(wchar_t) == 2`); single wchar_t on Linux/macOS. Non-ASCII keys/values now render correctly via `MockRenderer::drawText` and the real backend.
- **`addChild(externallyOwnedChild)`** now promotes `_externallyOwned = false` so `destroyWidgetTree` actually frees the child instead of treating it as host-managed.

### 18.7 Test coverage

`unittest/Test_Leak.cpp` (added in commit `28f3099`) exercises the leak paths fixed in commit `7502ca7`. These tests have no `CHECK_*` assertions — the assertion happens at process exit via LeakSanitizer. Without `-DAY_ENABLE_ASAN=ON` the tests just exercise the destroy paths (redundant with the regular suite); with ASAN they catch any regression to the `addChildExternal` / `addChild` distinction the original leaks abused.

The build config option `AY_ENABLE_ASAN` (root `CMakeLists.txt`, commit `3b912c2`) enables `/fsanitize=address` on MSVC. CI / pre-release should run with it on; everyday edit-build loop stays default (ASAN is ~2-3x slower and ~3x larger).