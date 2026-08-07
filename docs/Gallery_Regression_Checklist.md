# Gallery 语义验收表（AYUI）

> **用法**：每次 AYUI 改动后（或发版前整表过一遍），按 ID 跑一遍手测。
> 表内每行四列：操作 / 期望视觉 / 期望点击·焦点 / 状态。
> **基线日期**：2026-08-07（PR-Container-Contract-Cut2 + PR-TypeaheadBuffer + PR-SyncVerticalBar ship 后）。

## 验收范围

| ID | 类别 | 操作 |
|----|------|------|
| S1 | Capabilities / ScrollView | 拖主滚动条 / 触控板滚 |
| S2 | B3 ListView | 拖条 + 滚轮 |
| S3 | C3 Menu | A→B、同字母循环 |
| S4 | C2 ComboBox | typeahead + 弹层 |
| S5 | B1 Window | 标题中拖 / 顶缘 4px / 四角 |
| S6 | C1 Tooltip | 切页隐藏 |
| S7 | 嵌套滚轮 | List 在 ScrollView 里 |

## 验收表

| ID | 操作 | 期望视觉 | 期望点击·焦点 | 状态 | 日期 | 备注 |
|----|------|----------|----------------|------|------|------|
| S1 | Capabilities 页：拖主滚动条拖到中段 / 触控板两指滚 | 内容裁在视口内，无溢出；scrollbar thumb 跟手 | 点按钮点到的是画面上看到的那一个，不是旧坐标下的按钮 | ☒ FAIL | 2026-08-07 | **Widget-level PASS**（UT `scrollbar_gallery_drag_full_track_via_uimanager`：1268×608 viewport, 2500 tall content, drag 全程单调跟手）。根因非 widget 层：(a) 触控板两指 → AYDevice `WM_MOUSEWHEEL` handler 不收 WM_POINTERUPDATE（缺 raw-input 桥，precision trackpad 不生成 WM_MOUSEWHEEL），需 raw-input 或 SDL_HINT_MOUSE_TOUCH_EVENTS；(b)「不跟手」可能是 Gallery 在 _root = plain Widget（loadLayout 默认）时 UIManager pickTopmostWidget 只能 hit _root 自身不下传 — 在真实运行中 pickTopmostWidget 实际收到 CompoundWidget VBox root 故可能不命中，需 build 时确认 |
| S2 | B3 ListView：拖垂直条到底 / 鼠标滚轮连续 5 下 | 行跟着滚；row pool 复用；thumb 跟手 | 点哪一行选中的是当前可见的那行，不是被滚动遮住的旧行 | ☒ FAIL | 2026-08-07 | **Widget-level PASS**（UT `scrollview_wrapping_listview_wheel_routes_to_inner_only`：30 行 ListView 包在 ScrollView 内，wheel 路由 inner-only，outer offset 不动）。根因 = AYDevice wheel handler 只听 `WM_MOUSEWHEEL`，物理鼠标滚轮应 OK；触控板两指同 S1 缺桥。Gallery status label 不变 = wheel 真的没触发，AYDevice 路径嫌疑大 |
| S3 | C3 Menu：连按 A→B | 高亮跳到第一个以 B 开头的项 | 按 Enter 激活当前高亮项 | ☒ FIX | 2026-08-07 | ship `9a0b461`：`_hoveredIndex` 默认 0 → -1，typeahead startFrom 现包含 idx 0；新 helper `setHoveredIndexFromTypeahead` 强制 fire `_onHoverChanged` 解决 R「无反应」。UT 7 处更新匹配新 contract。**待基线重跑 Gallery 确认** |
| S4 | C2 ComboBox：键入 "ap" | typeahead 匹配 + 弹层打开 | 弹层不被父级裁切；点 ComboBox 外部关闭弹层 | ☐ PASS / ☐ FAIL | ____-__-__ | |
| S5 | B1 Window：拖标题中段移动 / 顶缘 4px / 四角 | 光标形态与行为一致（move / ns-resize / nwse-resize） | 拖动过程中正文区域跟着 resize；松开后状态稳定 | ☐ PASS / ☐ FAIL | ____-__-__ | |
| S6 | C1 Tooltip：hover 显页元素 → 切到隐页 → 切回显页 → 再 hover | 隐页时不弹 tip；显页重新计时 | 切回显页 hover 计时重置；不会立刻弹旧 tip | ☐ PASS / ☐ FAIL | ____-__-__ | |
| S7 | ScrollView 包 ListView，鼠标在内层 ListView 行上滚轮 | 内层 ListView 滚动；外层 ScrollView 不动 | 内层吃掉滚轮事件，外层 viewport 不变 | ☐ PASS / ☐ FAIL | ____-__-__ | |

## 跑表节奏

- **每次改容器/焦点/滚动/弹层**：跑相关 UT + 本表对应 2-3 条
- **AYUI 发版/bump 前**：整表过一遍（15-30 分钟）
- **踩到新语义 bug**：先加一条场景 UT（复现 hitTest / 按键序列），再改代码——比只修现象更稳

## 历史

| 日期 | 改动 | 跑表人 | 整体结果 |
|------|------|--------|----------|
| 2026-08-07 | 基线（PR-Container-Contract-Cut2 + PR-TypeaheadBuffer + PR-SyncVerticalBar ship 后启表） | — | 3 FAIL (S1/S2/S3) — 见各行备注 |
| 2026-08-07 | S3 fix ship + S1/S2 widget-level UT PASS 验证（新增 `scrollbar_gallery_drag_full_track_via_uimanager` + `scrollview_wrapping_listview_wheel_routes_to_inner_only`） | — | 3062/3062 PASS；S1/S2 widget-level OK，根因疑在 AYDevice Win32 输入层 |

## 已知 bug（暂不动手，等用户决定）

- **S1** ScrollView thumb 不跟手 — 仅中心段跟手（猜测区域不匹配）；触控板两指无反应
  - **根因分析 2026-08-07**：widget-level drag 验证 PASS（UT `scrollbar_gallery_drag_full_track_via_uimanager`，1268×608 viewport, 2500 tall content, 全程单调跟手）。触控板两指 = AYDevice WindowManager 只听 `WM_MOUSEWHEEL`，precision trackpad 不生成 WM_MOUSEWHEEL；需 raw-input bridge。
- **S2** ListView 鼠标滚轮无反应 — 备注：Gallery `wireCapabilities` line 617-625 注释说 "wrap ListView in ScrollView" 实际未做；ListView 已经在嵌套的 content_scroll 内
  - **根因分析 2026-08-07**：widget-level wheel 路由验证 PASS（UT `scrollview_wrapping_listview_wheel_routes_to_inner_only`）。Gallery 走 AYDevice bridge 调 `ui.onMouseWheel`；AYDevice 不收触控板事件，物理鼠标应 OK；若物理鼠标仍无反应，查 Gallery `state.mouse->getWheelDelta()` 是否返回 0
- **S3** Menu 首字母 typeahead — `_hoveredIndex` 默认 0（AYMenu.h:169）→ startFrom 跳过 idx 0。R 单按：wrap fallback setHoveredIndex(0) 但已 == 0 → early-return = "无反应"

## 关联

- 场景级 UT 复现这些语义：`unittest/Test_Scene_Suite_G.cpp`（post-scroll clip / nested wheel / typeahead+enter end-to-end）
- PR-Container-Contract-Cut2 root pin：`aac3b74`
- Scene_Suite_G root pin：`f23598d`
- 分层验证策略说明：见 `C:\Users\zhqmx\.claude\projects\d--Projects\memory\ay-ui.md` §分层验证策略