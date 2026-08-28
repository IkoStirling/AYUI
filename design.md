# AYUI Design

**文档修订：** 2026-08-27

**CMake 目标版本：** 1.0.0

**功能里程碑：** v1.5 已实现

**状态：** 根工程集成、AYRenderer 后端、Docking、Layout Editor、动画与完整单测均可构建。

> 本文描述当前代码，不再把已完成的 R/C/D 阶段当作未来路线图。历史 v1.2 方案保留在 [AYUI-v1-Design.md](AYUI-v1-Design.md)。发生冲突时，以代码、测试和本文为准。

## 1. 范围与目标

AYUI 是 AliyatEngine 的 UI 领域层。它管理 UI 状态和控件树，但不持有图形 API 资源，也不决定引擎主循环。

当前范围：

- 屏幕空间 2D UI、工具界面和 HUD
- 保留模式 Widget 树、布局、绘制、命中测试和输入路由
- JSON 布局、主题、i18n、热重载和布局持久化
- Popup、Modal、菜单、Docking 和独立 Layout Editor
- 通过 `IRenderBackend` 接入 MockRenderer 或 AYRenderer

非目标：

- 3D spatial UI、世界空间 Widget
- 像素遮罩命中测试、模糊/粒子等高级合成效果
- 跨线程直接修改 Widget 树
- 在 JSON 中执行任意脚本

## 2. 系统位置

```text
AYDevice / host events
          │
          ▼
      UIManager ────────────── UILayoutLoader / I18n / Theme
          │
          ├─ root
          ├─ overlay root ─── Popup / Tooltip / Modal / drag ghost
          └─ per-window state: focus / capture / hover / drag
          │
          ▼
       Widget tree ─────────── Layout / controls / docking
          │
          ▼
     IRenderBackend
          ├─ MockRenderer (tests)
          └─ AYRenderer::UIRenderBackend → UIPass
```

依赖方向必须保持单向：AYUI 只看 `IRenderBackend`；AYRenderer 可以依赖 AYUI 并实现后端。控件代码不能直接调用 bgfx、D3D 或平台绘图 API。

## 3. 模块结构

| 目录 | 职责 |
|---|---|
| `include/AYUI/` | 公共类型和控件 API |
| `interface/AYUI/` | `IRenderBackend` 契约 |
| `Controls/` | Widget、UIManager、控件、Popup、Modal、Docking |
| `Layout/` | VBox/HBox、GridPanel、Constraint、Splitter |
| `Loader/` | UILayoutLoader、WidgetFactory、WidgetSerializer |
| `Style/` | StyleSheet、Theme、MockRenderer |
| `i18n/` | JSON 语言表和 UTF-8 解析 |
| `unittest/` | 单元、场景、生命周期和性能回归 |
| `demo/` | Gallery、Layout Editor、round-trip 工具 |

`AYUI.h` 是稳定的便利聚合头，包含常用控件、Docking、Theme、Loader、UIManager 和渲染接口。实现辅助头（例如 `DockTrace`、`ScrollBarSync`）仍应按需显式包含。

## 4. Widget 模型

### 4.1 基础层次

```text
Widget
├─ LeafWidget
│  ├─ TextLabel / Image / ProgressBar / Spinner
│  └─ InteractiveWidget
│     ├─ Button / CheckBox / RadioButton / Slider
│     └─ FocusableWidget derivatives such as TextInput
└─ CompoundWidget / CompoundFocusableWidget
   ├─ Panel / Window / VBox / HBox / GridPanel
   ├─ ScrollView / ListView / TreeView / ComboBox / TabControl
   ├─ Menu / MenuBar / ToolBar / StatusBar
   ├─ Modal / ModalDialog / TabStrip
   └─ DockArea / DockCard / DockOverlay / DockTabGroup
```

类层次主要表达绘制和输入契约，不代替组合。复杂控件应复用现有 Widget，而不是复制独立状态机。

### 4.2 坐标和布局

- `position` 相对父 Widget；`getWorldBounds()` 计算并缓存世界矩形。
- 位置或尺寸变化必须调用 `markBoundsDirty()`，并使后代世界坐标缓存失效。
- VBox/HBox 维护与 children 对应的 slot；slot 可以是固定尺寸或 fill，并带 min/max 限制。
- ScrollView 的 `getClientRect()` 是绘制裁剪和命中测试的共同真值。
- GridPanel 的 row/column/cell attachment 是布局语义，不能仅靠 children 顺序推断。
- Dock tree 使用 VBox/HBox + SplitterHandle 作为实际 Widget 树，不维护第二套平行几何树。

### 4.3 绘制与脏标记

标准路径是：

1. `IRenderBackend::beginFrame()` 开启帧并丢弃上一帧的临时绘制指令。
2. `Widget::render()` 每帧重放所有可见 Widget；`onRender()` 绘制本控件，`renderChildren()` 负责后代和裁剪。
3. setter 改变可见状态时调用 `markDirty()`；改变几何或自然尺寸时同时使 bounds/layout 相关缓存失效。
4. 完成提交后清理 dirty 状态，但 clean 不能阻止下一帧重放。

这是保留模式 Widget 树与即时提交后端之间的硬契约：保留的是 UI 状态和树，不是 bgfx 的临时提交。dirty 当前只表示 presentation/cache invalidation，为未来 display-list 或离屏层缓存保留；在这些持久缓存落地前，不能把 dirty 当作 paint gate。Spinner、Tween 和滚动惯性仍按状态变化标脏，以保证未来缓存能够正确失效。

图片组合遵循同一树顺序：父节点先执行 `onRender()`，子节点再按插入顺序绘制，`bringToFront()` 可把同级节点移到末尾。`Image` 提供纹理句柄、UV 和 opacity；图片按钮使用 `Button -> Image` 装饰子节点，命中仍返回 Button。图片背景上的交互层应使用 `Panel -> [Image, ..., Button]`，Panel 会下降命中并让后加入的 Button 覆盖在图片上。Gallery 的 `Images` 页面是该契约的可视化回归样例。

渲染后端负责矩形、边框、文本、纹理、裁剪、opacity stack 等图元，不负责 Widget 生命周期或布局。`UIRenderBackend` 的 `UiItem`、clip stack 和 transient geometry 都是 frame-local；不得通过跨帧保留 `items` 来模拟 Widget 缓存。

## 5. 生命周期与所有权

以下规则是模块的负载不变量：

1. `UILayoutLoader`、`WidgetFactory` 和 `WidgetSerializer` 创建的根树由调用方持有，统一用 `destroyWidgetTree` 销毁。
2. `addChild` 将子控件纳入 `destroyWidgetTree` 的递归范围，但 `Widget` 析构本身不删除 children；`addChildExternal` 只用于调用方或专用管理器明确管理生命周期的场景。
3. Widget 析构时必须从父树解绑，避免父节点保留悬空指针。
4. UIManager overlay 管理 Popup、Tooltip、Modal dimmer、drag ghost 的层级和关闭顺序。
5. Popup 关闭可异步淡出；请求关闭后，不应假定原指针仍然有效。
6. `DockCard::setContent` 接管 content。替换 content 时旧树会销毁。
7. `Modal::setDimmerOwned` 接管 dimmer；`setDimmer` 是非拥有引用。
8. 关闭/热重载/销毁树之前，UIManager 必须清理 focus、hover、capture、active modal 和 drag 状态。
9. 回调中可能触发当前控件关闭；事件路径在回调返回后不得再次读取可能已释放的控件。

所有权 API 的命名应直接表达语义；不要通过“新地址是否不同”判断对象是否已析构，测试应使用析构哨兵。

## 6. 输入、事件与多窗口

`UIManager` 是输入路由和每窗口状态的中心：

- 鼠标命中顺序：overlay 优先，再到 root；容器的 clip 同时约束绘制与 hit-test。
- mouse-down 可以建立 capture；mouse-up、取消、reload 和 shutdown 必须释放 capture。
- 键盘和文本输入发送给 focused Widget。
- IME composition/commit 使用 UTF-8 字节输入，TextInput/TextArea 转换为平台 `wstring` 表示。
- `UIEvent` 通过 `Widget::bubbleEvent` 向父树冒泡，handled 后停止。
- DropdownManager 语义由 UIManager popup API 实现：同一窗口只保留当前活动 popup。
- 多窗口宿主应为每个原生窗口维护对应 UIManager/窗口状态，不共享 focus 或 capture 指针。

拖放使用 `DragPayload`、drop target 和 overlay ghost。DockArea 在拖动期间计算 slot/leaf 引导，提交后重新布局 dock tree。

## 7. Style、Theme 与 I18n

### 7.1 样式

- `StyleSheet` 从 JSON 加载命名 style。
- `Theme` 提供 token，如 surface、border、accent 和文本颜色。
- Widget 的 `styleId` 选择命名样式；控件级 token override 优先于主题默认值。
- Style/Theme 变化必须触发受影响控件重绘。

当前主题继承是扁平模型；嵌套 parent theme 尚未实现。

### 7.2 文本与国际化

- 所有 JSON 文件按 UTF-8 处理。
- 公共文本 API 以 `std::wstring` 为主，以匹配 Windows 文本/IME 路径。
- loader 和 serializer 必须进行真实 UTF-8 ↔ wide conversion，禁止用 iterator 逐字节窄化/拓宽。
- `I18n` 表格式为 `{ key: { language: translation } }`；缺少当前语言时回退到 `en`，再缺失时返回 key/default。
- `ui.` 前缀由 `isI18nKey` 识别，loader 只有在设置了 I18n 时才解析。

## 8. JSON、Factory、Serializer 契约

### 8.1 UILayoutLoader

通用字段：

```json
{
  "type": "Button",
  "id": "btn_ok",
  "position": { "x": 0, "y": 0 },
  "size": { "w": 120, "h": 32 },
  "visible": true,
  "style": "button.primary",
  "styleOverrides": {
    "color.accent": [0.2, 0.5, 0.9, 1.0]
  },
  "text": "ui.common.ok",
  "onClick": "confirm"
}
```

Loader 还解析各控件的专用字段，例如 Box spacing/padding/gravity、ListView selection、Image textureName、Grid cells、Dock cards/floating/weights。

事件字符串不是脚本。宿主先用 `bindEvent(widgetId, eventName, callback)` 注册 C++ 回调，loader 再按 id 连接。

### 8.2 WidgetFactory

Factory 是唯一的 `type` → constructor 注册表。默认注册覆盖普通控件、布局、DockArea/DockCard/DockOverlay，以及 Dimmer/Modal/ModalDialog/TabStrip。`UILayoutLoader` 不应依赖先构造 UIManager 才能获得内置注册。

`DockTabGroup` 是 DockArea 的内部 leaf，由 DockArea 创建，不作为通用 JSON 根类型。

### 8.3 WidgetSerializer

Serializer 服务于测试、编辑器导出和 Dock 布局持久化。其保证分层：

- 通用 Widget 字段可往返。
- 核心叶控件、列表/树/菜单、Box、Window、Spinner 和 DockArea 持久化路径有类型/字段覆盖。
- UTF-8 文本必须无损往返。
- DockCard content、DockArea slot/floating/weight 使用专用结构。

当前不承诺所有运行时控件都能通过通用 serializer 完整重建内部语义：

- GridPanel 的 cell attachment 仍需专用 payload；只写 children 会丢 row/col/span。
- Modal/ModalDialog、TabStrip、DockOverlay 的运行时内部子树不应盲目按通用 children 导出。
- DockTabGroup 属于内部实现，不是独立持久化边界。

新增可 JSON 创建的公共控件时，至少要同时提供 factory 测试和 serializer/loader 决策；若不支持完整往返，必须在这里明确记录。

### 8.4 热重载

`UILayoutLoader` 通过 AYIO FileWatcher 监控已加载文件。UIManager reload 前取消 capture 并清理旧树相关状态，再替换根树。Reload 失败时不得留下半构建树。

## 9. Popup、Modal 与 Docking

### 9.1 Overlay

Overlay root 是跨普通父容器裁剪边界的 UI 层。Menu、ComboBox popup、Tooltip、Modal dimmer、drag ghost 都挂载在此处。Popup 定位统一使用 viewport clamp/flip 规则。

### 9.2 Modal

Modal 打开后，UIManager 只向 active modal 子树路由输入；dimmer 吞掉后方点击。是否点击 dimmer 关闭由 `dismissOnDimmerClick` 控制。ModalDialog 是带标题、正文和结果按钮的常用模板。

### 9.3 Docking

已实现的数据模型：

- `DockArea`：dock tree 根、slot API、权重/min size、拖放命中和持久化入口
- `DockCard`：稳定 id、标题、图标、content、closable/floatable/collapsed
- `DockTabGroup`：同 leaf 多卡片的 tab 切换和关闭
- `DockOverlay`：浮动卡片
- VBox/HBox + SplitterHandle：嵌套 split tree

关键约束：

- card id 在一个 DockArea 内稳定且唯一。
- card 同一时刻只能位于一个 leaf 或 overlay。
- 移动 card 时先从旧容器安全解绑，再加入新容器；不能让两个父节点同时持有。
- 空 leaf 应被 prune；split tree 必须保留至少一个可见 fill panel。
- 保存格式记录 dock topology、slot weight/min size 和 floating frame，而不是序列化内部临时 hover/drag 状态。

## 10. 动画与时间推进

动画由 `UIManager::update(dt)` 驱动：

- Widget opacity/position tween
- InteractiveWidget 颜色状态过渡
- Menu/ComboBox popup 淡入、位移和延迟销毁淡出
- TabStrip indicator tween
- Spinner phase 与 ProgressBar indeterminate
- ScrollView/ListView 等滚动惯性

`tick()` override 必须先保持基类级联，再推进自身状态。动画中的 Widget 每帧标脏；完成后停止无意义的缓存失效，但可见内容仍按后端契约每帧提交。

## 11. 公共 API 与兼容性

- 命名空间统一为 `ayt::ui`。
- `include/AYUI/Event.h` 中旧的 `ayui::events` 仅保留兼容，不用于新代码。
- 公共渲染枚举位于 namespace scope，避免把后端实现细节嵌入接口类。
- 新控件优先复用 InteractiveWidget、FocusableWidget、SelectableWidget、ScrollableWidget 等现有契约。
- Setter 必须保持幂等；可见/布局状态变化需要正确 dirty/cache invalidation。
- 删除或改名公共类型前，应先提供迁移路径；JSON `type` 名同样视为兼容面。

## 12. 构建与引擎集成

根工程已包含：

```cmake
add_subdirectory(AYRuntime/AYUI)
```

AYUI 静态库公开依赖基础数学、字体、设备接口和公共 headers；AYIO FileWatcher 是实现依赖。若找到系统 `nlohmann_json` 则链接其 target，否则使用模块内 bundled header。

主要目标：

- `AYUI`
- `AYUI_UnitTests`
- `AYUI_Gallery`
- `AYUI_LayoutEditor`
- `AYUI_LayoutEditor_RoundTrip`

Gallery 当前包含 Basics、Images、Input、Collections、Overlay、Layout、Capabilities、Backend 和 Animation 九页；Images 页覆盖共享 `TextureRegistry` 纹理、UV crop、透明图片层和控件/图片叠加。

AYRenderer 的 `UIRenderBackend` 实现 `IRenderBackend`，UIPass 在 3D pass 后合成 UI。MockRenderer 用于无 GPU 单测，不应和生产 backend 行为产生不同的 Widget 语义。

## 13. 测试与审计基线

2026-08-27 全模块审计统计：

- 73 个公共/支持 header
- 61 个非 demo、非 unittest 的 `.cpp`
- 89 个 `Test_*.cpp`
- 963 个 `TEST_CASE`
- Windows Debug：`7405 / 7405` 条断言通过

审计覆盖：

- 全量构建和单测退出码
- Factory 注册与 serializer 类型一致性
- JSON/i18n UTF-8 文本
- 控件生命周期和拥有/非拥有指针
- dirty/cache invalidation、frame-local backend replay、world-bounds cache 和容器 clip/hit-test 契约
- TreeView 千节点重复建树性能
- Docking、布局保存/加载和 popup/modal 行为
- README/design 与当前实现偏差

本轮修复包括：

- 测试入口真实返回失败码，移除固定磁盘路径和不稳定堆地址断言。
- Spinner serializer 类型缺口。
- LayoutLoader 与 WidgetSerializer 的 UTF-8 无损转换。
- TreeView 复用 row widget pool，避免重复 allocator churn。
- 独立 Factory 补齐 Dimmer/Modal/ModalDialog/TabStrip 注册。
- ScrollView 运行时 scrollbar enable/disable、Box 自然尺寸缓存和多个视觉 setter 的 invalidation。
- 公共聚合头和 CMake header 清单补齐。

回归测试失败必须让进程返回非零；不得通过 batch wrapper 抹掉退出码。

## 14. 已知限制与后续工作

按优先级记录剩余边界：

1. 为 Modal/ModalDialog/TabStrip/DockOverlay 定义明确的独立 serializer wire contract，或继续声明它们只由专用宿主路径持久化。
2. 完成 GridPanel cell attachment 的 serializer round-trip 保证。
3. 设计 retained display-list：dirty Widget 只重建自己的本地绘制指令，但所有缓存指令仍每帧按 z-order replay；若进一步采用离屏层缓存，需要同时定义透明、clip、resize/DPI 和 overlay 的失效规则。
4. POSIX Clipboard 从 no-op 升级为平台实现。
5. TabStrip overflow 增加滚动/压缩策略；RichText 增加更完整的排版能力。
6. 可选：统一散落在 loader、serializer、IME 和 i18n 中的 UTF-8 工具为一个经过测试的公共内部组件。

这些限制不阻塞当前 v1.5 功能，但实现新特性时不得继续扩大重复路径。

## 15. 决策摘要

| 决策 | 结论 |
|---|---|
| UI 模式 | 保留模式 Widget tree |
| 数据格式 | 复用 JSON UILayoutLoader，不引入第二套配置系统 |
| 渲染解耦 | `IRenderBackend`，AYRenderer 提供实现 |
| 帧提交 | 可见 Widget 每帧完整 replay；dirty 只做缓存失效，不是 paint gate |
| 文本编码 | 文件/JSON UTF-8，Widget 文本 `std::wstring` |
| 事件 | Widget 内部冒泡；宿主回调用 id + bindEvent |
| Popup | UIManager overlay 集中管理 |
| Dock tree | VBox/HBox/Splitter 直接作为 Widget tree |
| 销毁 | `destroyWidgetTree` + 明确 owned/external API |
| 线程 | UI 树单线程修改 |
| 历史文档 | `AYUI-v1-Design.md` 只读保留 |

## 16. 相关文档

- [README.md](README.md)
- [AYUI-v1-Design.md](AYUI-v1-Design.md)
- [AYRenderer README](../AYRenderer/README.md)
- [AYEntity design](../AYEntity/design.md)
