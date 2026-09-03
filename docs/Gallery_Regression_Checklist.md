# Gallery 语义验收表（AYUI）

> **用法**：每次 AYUI 改动后（或发版前整表过一遍），按 ID 跑一遍手测。  
> 表内：操作 / 期望视觉 / 期望点击·焦点 / 状态。  
> **基线日期**：2026-08-11（Capabilities S1–S7 + Dock Phase 3/4 手测项补齐）。  
> **入口**：`out/build/x64-Debug/AYRuntime/AYUI/demo/AYUI_Gallery.exe`

## 验收范围

| ID | 页签 | 类别 | 操作摘要 |
|----|------|------|----------|
| S1 | 6. Capabilities | ScrollView | 拖主滚动条 / 触控板滚 |
| S2 | 6. Capabilities | ListView | 拖条 + 滚轮 |
| S3 | 6. Capabilities | Menu | A→B typeahead |
| S4 | 6. Capabilities | ComboBox | typeahead + 弹层 |
| S5 | 6. Capabilities | Window | 标题拖 / 顶缘 / 四角 |
| S6 | 6. Capabilities | Tooltip | 切页隐藏 |
| S7 | 6. Capabilities | 嵌套滚轮 | List 在 ScrollView 里 |
| D1 | 5. Layout | Dock 拆分 | 卡片边缘 drop → 新叶 |
| D2 | 5. Layout | Dock Join | 拖到中心区 → 并页签 |
| D3 | 5. Layout | Dock prune | 关闭拆出叶 → 无黑洞 |
| D4 | 5. Layout | Tear-off / 子窗 | 标题拖出 → 独立窗 |
| D5 | 5. Layout | Redock | 子窗拖回 dock 落点 |
| D6 | 5. Layout | Save / Load | 顶栏 Save Dock / Load Dock |
| D7 | 5. Layout | 最小宽度 | 分割条拖到极限 |
| D8 | 5. Layout | 内容裁剪 | 窄面板内按钮不渗出 |
| BK1 | 7. Backend | 渐变 | 2 色 / 4 色 / 暗面板渐变 |
| BK2 | 7. Backend | 混合模式 | Additive / Multiply / Screen + 发光 |
| BK3 | 7. Backend | SDF 边框 | r2 / 8 / 20 单条 draw call 圆角 |
| BK4 | 7. Backend | SDF 阴影 | blur 4 vs 12 渐隐 |
| BK5 | 7. Backend | 组合卡 | 阴影 + 填充 + 描边三层 |
| BK6 | 7. Backend | 纹理拉伸 | LINEAR 无马赛克 |
| BK7 | 7. Backend | 9-patch | 四角锐利不畸变不渗色 |
| BK8 | 7. Backend | 生命周期 | 滚动 / reload / 退出不崩 |
| A1 | 9. Animation | Timeline | 物理弹簧 + 四轮 yoyo |

---

## A. Capabilities（页签 **6. Capabilities**）

| ID | 操作 | 期望视觉 | 期望点击·焦点 | 状态 | 日期 | 备注 |
|----|------|----------|----------------|------|------|------|
| S1 | 拖主滚动条到中段 / 触控板两指滚 | 内容裁在视口内；thumb 跟手 | 点到的是画面上看到的控件，不是旧坐标 | ☐ PASS / ☐ FAIL | ____-__-__ | 可选：`AYUI_TRACE_INPUT=1` 看 stderr `isCompound=1`。UT 已 PASS；2026-08-07 后 raw-input wheel bridge 已合，待真跑闭环 |
| S2 | ListView：拖垂直条到底 / 滚轮连滚 5 下 | 行跟着滚；thumb 跟手 | 点中的是当前可见行 | ☐ PASS / ☐ FAIL | ____-__-__ | 物理鼠标与触控板路径都应可用 |
| S3 | Menu：连按 A→B | 高亮跳到第一个以 B 开头的项 | Enter 激活当前高亮 | ☐ PASS / ☐ FAIL | ____-__-__ | 2026-08-07 已 FIX（`9a0b461`）；本轮重确认 |
| S4 | ComboBox：键入 `ap` | typeahead 匹配 + 弹层打开 | 弹层不被父级裁切；点外部关闭 | ☐ PASS / ☐ FAIL | ____-__-__ | |
| S5 | Window：拖标题中段 / 顶缘 4px / 四角 | 光标 = move / ns-resize / nwse-resize | 正文跟 resize；松开后稳定 | ☐ PASS / ☐ FAIL | ____-__-__ | |
| S6 | Tooltip：显页 hover → 切隐页 → 切回 → 再 hover | 隐页不弹 tip；显页重新计时 | 不会立刻弹旧 tip | ☐ PASS / ☐ FAIL | ____-__-__ | |
| S7 | 鼠标在内层 ListView 行上滚轮 | 内层滚；外层 ScrollView 不动 | 内层吃掉滚轮 | ☐ PASS / ☐ FAIL | ____-__-__ | |

---

## B. Dock / Layout（页签 **5. Layout** → `mini_dock`）

顶栏按钮：**Save Dock** / **Load Dock**。标题栏应含 stamp `DockArea-20260810b-Bugfix`（或更新后的 stamp）。

| ID | 操作 | 期望视觉 | 期望点击·焦点 | 状态 | 日期 | 备注 |
|----|------|----------|----------------|------|------|------|
| D1 | 将 **Right**（或任意）卡标题拖到 **Center** 东/西/南/北边缘 drop | 出现新叶 + 分割条；无黑缝、无重叠 | 新叶可点、可再拖；**Center↔新叶边界可拖** | ☐ PASS / ☐ FAIL | ____-__-__ | 同叶仅 1 tab 边缘 = NO-OP。2026-08-11：空 Right 隐藏的 split 被 East 复用后曾不可拖，已修 `syncSplitterVisibility` |
| D2 | 将一卡拖到另一叶**中心区**（Join）放下 | 目标叶出现多 tab；源叶空则折叠/消失 | 点 tab 切换；内容跟 active | ☐ PASS / ☐ FAIL | ____-__-__ | 不应把原 Center 挤成空壳 |
| D3 | 拆出 `g_N` 后关闭该叶全部 tab（或拖走最后一卡） | 空叶 prune；邻接填满；**无黑色空洞** | 剩余面板可点、分割条仍可用 | ☐ PASS / ☐ FAIL | ____-__-__ | 含：侧栏 North 拆 → 再 Join 走后 mid 不留洞 |
| D4 | 拖卡标题离开 dock（超过 promote 阈值） | 弹出独立顶层窗；主 dock 该槽折叠或让位 | 子窗可拖移、可关；主窗仍响应 | ☐ PASS / ☐ FAIL | ____-__-__ | Gallery `GalleryChildWindows` 路径 |
| D5 | 子窗内再拖标题，移回主窗 dock 上松手 | 预览高亮与落点一致；卡回到树/槽 | 子窗关闭；卡在预期叶/槽；可继续拖 | ☐ PASS / ☐ FAIL | ____-__-__ | 落在折叠侧栏外缘应 revive，且不误抢 Center 东西拆 |
| D6 | **Save Dock** → 改布局（拆/并/拖）→ **Load Dock** | 恢复为保存时的树形与尺寸比例 | 卡仍可拖；promote 回调仍有效 | ☐ PASS / ☐ FAIL | ____-__-__ | 写 `%` 旁/`gallery_dock_tree.json`（见 Gallery 日志） |
| D7 | 拖 Left\|Center 或 Center\|Right 分割条，一直往极限拖 | 面板在约 **120px** 停住，不再缩成发丝 | 分割条跟手到限后卡住；松手后布局稳定 | ☐ PASS / ☐ FAIL | ____-__-__ | `BoxBase::kMinPanelSize` + `slotMinSizes` |
| D8 | 先把一侧拖到最窄（D7），看窄卡内「Ping」/标签 | 内容被裁在卡片内，**不画进邻居面板** | 点邻居面板点到邻居；窄卡内可见部分仍可点 | ☐ PASS / ☐ FAIL | ____-__-__ | DockCard / DockTabGroup `pushClip` |

### Dock 建议手顺（约 10–15 分钟）

1. 打开 **5. Layout**，确认三卡 Left / Center / Right 可见。  
2. **D7 → D8**（最快验证最近两修）。  
3. **D1 → D2 → D3**（树结构）。  
4. **D4 → D5**（子窗进出）。  
5. **D6**（持久化）。  
6. 若有余力：同叶多 tab 后再边缘拆一次（D1 备注）。

---

## C. Backend（页签 **7. Backend**）

> 验证 UIRenderBackend 扩充（P0 统一批 → P1 渐变+混合 → P2 SDF → P3 纹理+九宫格）。页面是 host-drawn demo（`BackendDemoWidget::onRender` 每帧直绘），内容在 page_backend 的 VBox 固定 820px 槽内，可随页面滚动。**真实 GPU 后端**（D3D11）验证 —— Noop/UT 不执行 shader。

| ID | 操作 | 期望视觉 | 期望点击·焦点 | 状态 | 日期 | 备注 |
|----|------|----------|----------------|------|------|------|
| BK1 | 看 P1 第一行三块渐变：① 2 色（顶红→底蓝）② 4 色（黄/绿/紫/蓝四角）③ 暗面板（深灰→蓝） | 每个都是**平滑连续过渡**，无条带/无跳变；四色块四角颜色可辨 | — | ✅ PASS | 2026-08-12 | 用户：「完美」。渐变走 per-vertex color 插值，无需 shader |
| BK2 | 看 P1 第二行：灰底上三块叠色（Additive 橙 / Multiply 绿 / Screen 红）+ 蓝半透明面板上叠 Additive 橙光 | Additive=变亮偏橙不盖底；Multiply=明显变暗；Screen=变亮但不过曝；橙光叠加区比面板本身亮（发光感），不发灰不发脏 | — | ✅ PASS | 2026-08-12 | 用户：「灰色长条（无叠色）+ 棕/黑/粉三小色块 + 蓝块 + 浅棕块」——叠色效果正常（橙×灰→棕、绿×灰→暗、红×灰→粉；蓝底+橙光→浅棕） |
| BK3 | 看 P2 第一行三个蓝色 3px 描边框（r=2 / 8 / 20） | ① r2 近直角、r8 明显圆角、r20 大圆角 ② **边缘无锯齿/无方块角**（此前是 8-rect 分解=方块角）③ 描边全周均匀 3px，不发虚不变粗 | — | ⚠️ 修后待复验 | 2026-08-12 | 用户手测时**无显示**（根因：fwidth 在圆角处膨胀 → ring 消失）；SDF constant-width stroke（`editor_ui_sdf_cw`，coverPx + 等距偏移环）已合，**待 Gallery 复验** |
| BK4 | 看 P2 第二行两对阴影卡（blur 4 / 12，offset 右下 4px，每张旁有 1px 灰框对照） | ① 阴影在卡片**右下外**，边缘渐隐（软边）② blur 12 扩散更大更柔、blur 4 更实 ③ **渐隐不空心**（模糊区无黑圈/无亮边） | — | ✅ PASS | 2026-08-12 | 用户：「黑色块 + 更大的黑色块（边缘透明）」——渐隐正常，blur 12 更大更柔 |
| BK5 | 看 P2 第三行 150×96 组合卡：深蓝填充 + 右下阴影 + 白色 2px 圆角描边 | 三层同时正确：填充不透明、阴影在卡外右下、描边全周清晰圆角；**阴影不被填充盖掉** | — | ⚠️ 修后待复验 | 2026-08-12 | 用户：「蓝色块 + 黑色边框」——填充/阴影 OK；白描边当时未显示（黑色边框=阴影硬边）→ 随 BK3 修复待复验 |
| BK6 | 看 P3 第一行：64×64 对角渐变纹理画成 240px 宽 + 180px 宽（横向压扁） | ① 拉伸后仍**平滑过渡，无马赛克/无像素块**（LINEAR 采样）② 颜色方向正确：左上偏暖（红）、右下偏蓝，过渡连续 | — | ✅ PASS | 2026-08-12 | 用户：「正确」 |
| BK7 | 看 P3 第二行：16×16 圆角按钮 9-patch 画成 48 / 96 / 192 px 宽（padding 4px） | ① **四角圆弧保持锐利圆润**，不畸变成椭圆/不压扁 ② 圆角外侧透明区**干净无渗色**（无杂色块/无 bleeding）③ 2px 亮环在四角连续不断线 | — | ✅ PASS（风格化保留） | 2026-08-12 | 用户：「淡棕色矩形 + 白色圆角描边」——认可并**保留**该风格化效果；页面新增 32×32 干净版 + stylized 二选一（sliders） |
| BK8 | 在 Backend 页滚轮滚动 → 按 Gallery reload（R/Ctrl+R 同 Capabilities）→ 关闭窗口退出 | ① 页面内容随滚动**正确裁剪**（demo 不渗出）② reload 后纹理重建、页面重绘正常（无花屏/无残留）③ **退出不崩溃**（纹理随 uiBackend.shutdown 释放，无 GPU 泄漏告警） | — | ☐ PASS / ☐ FAIL | ____-__-__ | teardownBackendPage 先于 loadLayout / shutdown |

### Backend 建议手顺（约 5 分钟）

1. 打开 **7. Backend**，自上而下 BK1 → BK7（纯目测，无需点击）。  
2. **BK8** 收尾：滚到底 → reload → 退出。  
3. 若 BK3/BK4 有锯齿或方块角、BK7 有渗色 → 记 FAIL + 截图步骤（SDF 边界外扩 / padding 比例）。

---

## D. Animation（页签 **9. Animation**）

| ID | 操作 | 期望视觉 | 期望状态 | 状态 | 日期 | 备注 |
|----|------|----------|----------|------|------|------|
| A1 | 点击 **Play timeline + yoyo**，完整观察约 2.6 秒；播放中再次点击重启 | ProgressBar 以物理弹簧速度前进/后退，共四轮，边界不跳帧、不超出槽位 | 标签最终显示完成，数值回到 0；reload 后可再次播放且无旧时间线残留 | ☐ PASS / ☐ FAIL | ____-__-__ | reduced-motion 开启时应直接落到最终 0；永久装饰循环由 UT 验证落到结束姿态并完成 |

---

## 跑表节奏

- **改容器 / 焦点 / 滚动 / 弹层**：Capabilities 相关 2–3 条 + 对应 UT  
- **改 Dock 树 / 分割条 / 裁剪 / 持久化**：Dock D1–D8（至少 D3/D7/D8）  
- **改渲染后端 / shader / 纹理路径**：Backend BK1–BK8（至少 BK3/BK4/BK6/BK7）  
- **改动画 / tick / 时间线**：Animation A1 + `AYUI_AnimationProductization` UT
- **AYUI 发版前**：整表（Capabilities + Dock + Backend，约 30–45 分钟）  
- **新语义 bug**：先加场景 UT，再改代码  

---

## 历史

| 日期 | 改动 | 跑表人 | 整体结果 |
|------|------|--------|----------|
| 2026-08-07 | 基线（Cut2 + Typeahead + SyncVerticalBar） | — | 3 FAIL (S1/S2/S3) |
| 2026-08-07 | S3 fix；S1/S2 widget UT PASS；input bridge ship | — | S3 FIX；S1/S2 待 Gallery 真跑 |
| 2026-08-11 | 补齐 Dock D1–D8（拆并/prune/子窗/SaveLoad/min/clip） | — | **待用户手测** |
| 2026-08-12 | 补齐 Backend BK1–BK8（UIRenderBackend P0–P3 demo 页） | — | **待用户手测**（2026-08-11 SDF D3D11 修复 `657ee0c` 后首次可进） |
| 2026-08-12 | Backend 用户手测：BK1/2/4/6/7 ✅（BK7 风格化效果用户认可并保留）；BK3 描边无显示、BK5 白描边未显示（黑框=阴影硬边）→ 根因 fwidth 圆角膨胀 → SDF constant-width stroke（`editor_ui_sdf_cw`）合入 | — | **主体 PASS；BK3/BK5 待复验** |

---

## 已知问题（历史口径，手测时请更新状态）

- **S1 / S2**：2026-08-07 曾 FAIL；随后 raw-input wheel bridge 已合，UT 绿——以本轮 Gallery 勾选为准。  
- **S3**：代码已 FIX；本轮勾选确认即可。  
- **Dock**：若 D3 仍见黑洞、D7 仍可拖到 0、D8 仍渗色，记 FAIL 并注明步骤。  
- **Backend**：若 BK3/BK4 见方块角或锯齿、BK7 渗色，记 FAIL + 截图（SDF 边界外扩 / 9-slice padding 待查）。

---

## 关联

- Capabilities 场景 UT：`unittest/Test_Scene_Suite_G.cpp`  
- Dock UT：`Test_DockTree.cpp` / `Test_DockFloat.cpp` / `Test_DockTabGroup.cpp` / `Test_Layout.cpp`（min panel）  
- Backend 页 demo：`AYUI_Gallery.cpp` `BackendDemoWidget` / `wireBackendPage` / `teardownBackendPage`  
- 渲染后端：AYRenderer `UIRenderBackend` / `UiGpuContext`（SDF shader cache-key `editor_ui_sdf`）  
- 设计顺序：`design.md` §17 / §19  
- PR-Container-Contract-Cut2：`aac3b74`  
- Dock 内容 clip：DockCard / DockTabGroup `compoundDescendClippedRender`（2026-08-11）  
