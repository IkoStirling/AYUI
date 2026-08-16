# AYUI

Engine UI framework: widget tree, JSON layout loading, i18n, and `IRenderBackend` abstraction.

**Authoritative design:** [`design.md`](design.md) (v1.3, 2026-07)

Legacy detail: [`AYUI-v1-Design.md`](AYUI-v1-Design.md) (v1.2)

---

## Status

当前为 v1.5：核心 Widget、数据驱动布局、输入控件、Overlay、Layout、Docking 与 Gallery 均已进入根工程构建。Docking 和 UI Animation 的后续顺序以 [design.md](design.md) 为准。

- 公开入口：`AYUI.h`
- 后端接口：`AYUI/IRenderBackend.h`
- 控件头：`include/AYUI/`
- 根工程已启用 `add_subdirectory(AYRuntime/AYUI)`

## Data-driven (quick start)

Layout JSON is loaded with **`UILayoutLoader`** — reuse as-is for v1:

```cpp
#include "AYUI.h"

ayt::ui::UILayoutLoader loader;
loader.bindEvent("btn_ok", "onClick", [] { /* ... */ });

ayt::ui::Widget* root = loader.loadFromFile("menu.ui.json");
// root->render(mockOrRealBackend);
```

- Text keys: `"ui.section.key"` → `I18n`
- Styles: `"style": "id"` → `StyleSheet` (JSON parser U1; `styleId` already set on widget)
- Do **not** add a second config system for layouts; see [design.md §4](design.md#4-data-driven-layer-reuse-assessment)

---

## Tests

```bat
cmake --build <build-dir> --target AYUI_UnitTests
<build-dir>/AYRuntime/AYUI/unittest/AYUI_UnitTests.exe
```

Requires enabling `AYUI` in CMake locally.

## Gallery (visual check)

Standalone UI-only demo — no 3D scene, no Editor shell:

```bat
cmake --build <build-dir> --target AYUI_Gallery
<build-dir>/AYRuntime/AYUI/demo/AYUI_Gallery.exe
```

Layout: `demo/assets/gallery.ui.json` (copied next to the exe). Sections:
Basics / Input / Collections / Overlay / Layout. Use **Reload JSON** after editing the layout file.

---

## Related engine docs

- [AYRenderer/README.md](../AYRenderer/README.md) — R4 + engine integration
- [AYEntity/design.md](../AYEntity/design.md) — ECS / bootstrapModule
