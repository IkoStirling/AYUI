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

## 跑表节奏

- **改容器 / 焦点 / 滚动 / 弹层**：Capabilities 相关 2–3 条 + 对应 UT  
- **改 Dock 树 / 分割条 / 裁剪 / 持久化**：Dock D1–D8（至少 D3/D7/D8）  
- **AYUI 发版前**：整表（Capabilities + Dock，约 25–40 分钟）  
- **新语义 bug**：先加场景 UT，再改代码  

---

## 历史

| 日期 | 改动 | 跑表人 | 整体结果 |
|------|------|--------|----------|
| 2026-08-07 | 基线（Cut2 + Typeahead + SyncVerticalBar） | — | 3 FAIL (S1/S2/S3) |
| 2026-08-07 | S3 fix；S1/S2 widget UT PASS；input bridge ship | — | S3 FIX；S1/S2 待 Gallery 真跑 |
| 2026-08-11 | 补齐 Dock D1–D8（拆并/prune/子窗/SaveLoad/min/clip） | — | **待用户手测** |

---

## 已知问题（历史口径，手测时请更新状态）

- **S1 / S2**：2026-08-07 曾 FAIL；随后 raw-input wheel bridge 已合，UT 绿——以本轮 Gallery 勾选为准。  
- **S3**：代码已 FIX；本轮勾选确认即可。  
- **Dock**：若 D3 仍见黑洞、D7 仍可拖到 0、D8 仍渗色，记 FAIL 并注明步骤。

---

## 关联

- Capabilities 场景 UT：`unittest/Test_Scene_Suite_G.cpp`  
- Dock UT：`Test_DockTree.cpp` / `Test_DockFloat.cpp` / `Test_DockTabGroup.cpp` / `Test_Layout.cpp`（min panel）  
- 设计顺序：`design.md` §17 / §19  
- PR-Container-Contract-Cut2：`aac3b74`  
- Dock 内容 clip：DockCard / DockTabGroup `compoundDescendClippedRender`（2026-08-11）  
