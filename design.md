# AYUI Design

**文档修订：** 2026-08-28

**CMake 目标版本：** 1.0.0

**功能里程碑：** v1.5 已实现

**状态：** 根工程集成、Widget-local retained display-list、Layer/RenderTarget 契约、Serializer
完整化、AYRenderer 合批与 vector-path/stencil clip、DPI/UI scale、无障碍语义、主题继承、
Tab/RichText 产品化、POSIX Clipboard、Docking、Layout Editor、动画与完整单测均可构建。

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

- Widget 几何统一是逻辑 DIP；`setClientSize()` 输入物理 framebuffer 像素，逻辑视口等于
  `physicalSize / (dpiScale * uiScale)`。两种 scale 都为 1 时保持历史行为。
- 窗口事件坐标在 UIManager 分发入口从物理像素换算为 DIP；命中、布局、display-list、
  accessibility bounds 和 damage 均只保存逻辑坐标。
- `UIRenderBackend` 在最终顶点、SDF 半径/描边/阴影和字体 raster size 处应用有效 scale；
  测量结果再除以 scale 返回 DIP，布局不会因为高 DPI 字体位图变大而漂移。
- `position` 相对父 Widget；`getWorldBounds()` 计算并缓存世界矩形。
- 位置或尺寸变化必须调用 `markBoundsDirty()`，并使后代世界坐标缓存失效。
- VBox/HBox 维护与 children 对应的 slot；slot 可以是固定尺寸或 fill，并带 min/max 限制。
- ScrollView 的 `getClientRect()` 是绘制裁剪和命中测试的共同真值。
- GridPanel 的 row/column/cell attachment 是布局语义，不能仅靠 children 顺序推断。
- Dock tree 使用 VBox/HBox + SplitterHandle 作为实际 Widget 树，不维护第二套平行几何树。

### 4.3 绘制与脏标记

标准路径是：

1. `IRenderBackend::beginFrame()` 开启帧并丢弃上一帧的临时绘制指令。
2. `Widget::render()` 每帧遍历所有可见 Widget。dirty 或尚未缓存的 Widget 通过
   `DisplayListRecorder` 执行一次 `onRender()`；Recorder 在把命令立即转发给本帧后端的同时，
   生成本控件的候选高层 display-list。
3. clean Widget 不再执行 `onRender()`，而是把缓存的矩形、文本、图片、clip、blend 和 path recipe 等命令
   replay 给当前后端。bgfx 仍然每帧收到完整提交，不跨帧保留 `UiItem` 或 transient buffer。
4. 普通 `renderChildren()` 始终独立遍历后代；子控件的实际绘制命令不会被扁平化进父控件的
   list。ScrollView/ListView/TreeView 等为了 background/content/chrome 顺序而从 `onRender()`
   直接调用子控件时，父 list 只保留动态子 Widget 调用，子控件仍独立检查 dirty/rebuild。
   父级 clip、opacity 和子节点插入顺序仍在每帧 replay 中生效。
5. setter 改变本控件 presentation 时调用 `markDirty()`；后代 dirty 只向父级传播 damage 状态，
   不会无条件重建父级本地 list。位置或尺寸变化还要失效 bounds cache；由于当前绘制命令使用
   world-space 坐标，祖先几何变化会级联失效后代 display-list。
6. 完成提交后清理 dirty 状态；清理只表示本地 list 已同步，不能阻止下一帧 replay。

这是保留模式 Widget 树与即时提交后端之间的硬契约：display-list 减少静态 Widget 的
`onRender()`、样式查询和命令构造开销，但不违反 bgfx 的逐帧 submit 规则。Spinner、Tween
和滚动惯性仍按状态变化标脏，因此动画期间重建本地 list，完成后回到稳定 replay。

通用 Recorder 不跨帧持有后端资源句柄。vector path 以 backend-independent construction recipe
记录；每条 draw/clip 命令捕获当时的 immutable operation snapshot，replay 时临时创建后端路径、
重建、提交并释放，因此 releasePath 和后续 path mutation 不会改变已缓存命令。粒子更新、字体/
动画资源创建、RenderTarget/Layer 生命周期或显式 pass 控制仍使候选 list 失效；Recorder 已把
本帧命令转发给真实后端，下一帧继续旧即时路径。`DisplayListPolicy::Immediate` 可显式保留该路径，
用于自定义控件兼容、诊断和 A/B 对照。

图片组合遵循同一树顺序：父节点先执行 `onRender()`，子节点再按插入顺序绘制，`bringToFront()` 可把同级节点移到末尾。`Image` 提供纹理句柄、UV 和 opacity；图片按钮使用 `Button -> Image` 装饰子节点，命中仍返回 Button。图片背景上的交互层应使用 `Panel -> [Image, ..., Button]`，Panel 会下降命中并让后加入的 Button 覆盖在图片上。Gallery 的 `Images` 页面是该契约的可视化回归样例。

渲染后端负责矩形、边框、文本、纹理、裁剪、opacity stack 等图元，不负责 Widget 生命周期或布局。`UIRenderBackend` 的 `UiItem`、clip stack 和 transient geometry 都是 frame-local；不得通过跨帧保留 `items` 来模拟 Widget 缓存。

### 4.4 UI Layer / RenderTarget 抽象

`IRenderBackend` 已定义两级持久渲染资源：

- `RenderTargetDesc` / `RenderTargetHandle` 表达物理目标的尺寸、DPI、alpha 和内容保留策略，
  并提供创建、resize、bind、blit、纹理查询和释放。
- `LayerDesc` / `LayerHandle` 在 RenderTarget 之上表达逻辑 bounds、DPI、alpha、overlay、
  clear mode/color；`LayerPaint` 携带局部 damage 和 full-redraw 选择。

Layer 保留的是像素，display-list 保留的是绘制命令，两者不能混为一套缓存。Layer 的逻辑尺寸
变化或 DPI 变化必须把 backing target resize 为 `ceil(logicalSize * dpiScale)` 并要求重绘；
dirty Layer 在 `beginLayerPaint()` / `endLayerPaint()` 完成前不能 composite。局部 damage 当前是
后端可利用的重绘元数据，不授权后端破坏 clip、透明和 painter order。

该 API 是 capability-gated：不支持的后端返回无效 handle 或 `false`，调用方必须继续即时绘制。
MockRenderer 已实现完整生命周期和可观察事件，用于锁定 resize、DPI、damage、paint/composite
和 release 契约。AYRenderer 当前明确返回 `supportsRenderTargets() == false`；生产 bgfx FBO、
纹理生命周期、清除策略和子树选择尚未落地，因此本阶段没有默认把任何 Widget 子树转为像素层。
overlay 仍由 UIManager 的现有 painter order 管理，未来接入 Layer 时也必须由调用点显式 composite。

### 4.5 产品化呈现契约

1. **DPI 与 UI scale**：DPI 来自窗口所在显示器，UI scale 来自用户偏好，二者相乘但生命周期
   分离。宿主在 DPI change 和 framebuffer resize 时分别更新对应值；Gallery 已处理
   `WM_DPICHANGED`，自动化截图固定 scale=1 以保持可复现。
2. **无障碍语义**：每个 Widget 在运行期获得稳定 ID。`buildAccessibilityTree()` 对内置控件推断
   role、label、value、state 和 actions，显式 accessibility 元数据具有更高优先级；隐藏节点和
   不可见子树不进入快照。原生 UIA/AT-SPI/NSAccessibility adapter 消费快照，并通过
   `performAccessibilityAction()` 把动作路由回当前 Widget。
3. **Theme 继承**：Theme 通过名字延迟解析 `extends`，token 和 sheet 均按 parent-first、
   child-wins 合成；visited set 使缺失父级或继承环安全终止。控件树上的 token override 从祖先
   向后代级联，最近节点胜出，修改祖先 override 会标脏整个 style 子树。
4. **TabStrip overflow**：Scroll 是默认策略，维护 logical offset/max offset、滚轮滚动和选中项
   自动可见；Compress 从配置的 `minTabWidth` 向 24 DIP 交互硬下限按可用宽度分配；Clip 保留旧固定宽度行为。
   三种模式均裁剪 child paint，不允许 tab 越过 strip 覆盖相邻控件。
5. **RichText 段落布局**：run 保存字号、颜色、bold/italic、下划线、删除线、字距和基线偏移；
   paragraph layout 支持显式换行、word/character wrap、left/center/right/justify、垂直对齐、
   行高/行距、最大行数、clip/ellipsis，并输出 fragments/lines/contentSize 供测量、命中与 caret。
   justified whitespace 必须成为独立 advance，不能合并后丢失额外间距。
6. **Clipboard**：Win32 使用系统 API；macOS 使用 pbcopy/pbpaste；Linux 优先 Wayland
   wl-clipboard，再尝试 X11 xclip/xsel。所有 POSIX payload 都按 UTF-8 转换并设置 16 MiB 读取上限；
   helper 不存在、session 不可用或编码非法时返回 false，不创建进程内伪剪贴板。

## 5. 生命周期与所有权

以下规则是模块的负载不变量：

1. `UILayoutLoader`、`WidgetFactory` 和 `WidgetSerializer` 创建的根树由调用方持有，统一用 `destroyWidgetTree` 销毁。
2. `addChild` 将子控件纳入 `destroyWidgetTree` 的递归范围，但 `Widget` 析构本身不删除 children；`addChildExternal` 只用于调用方或专用管理器明确管理生命周期的场景。
3. Widget 析构时必须从父树解绑，避免父节点保留悬空指针。
4. UIManager overlay 管理 Popup、Tooltip、Modal dimmer、drag ghost 的层级和关闭顺序。
5. Popup 关闭可异步淡出；请求关闭后，不应假定原指针仍然有效。
6. `DockCard::setContent` 接管 content。替换 content 时旧树会销毁。
7. `Modal::setDimmerOwned` 接管 dimmer；`setDimmer` 是非拥有引用。
8. `ScrollView::setContentOwned`、`Modal::setContentOwned`、`ModalDialog::setBodyContentOwned` 和
   `TabControl::addTabOwned` 明确接管结构化内容；非 owned API 只建立外部引用。两种销毁入口都必须避免重复释放。
9. 关闭/热重载/销毁树之前，UIManager 必须清理 focus、hover、capture、active modal 和 drag 状态。
10. 回调中可能触发当前控件关闭；事件路径在回调返回后不得再次读取可能已释放的控件。

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
- `Theme` 提供 token，如 surface、border、accent 和文本颜色；`extends` 通过 ThemeManager 名字
  延迟解析，父 token/sheet 先合成，子 theme 覆盖同名项。
- Widget 的 `styleId` 选择命名样式；控件级 token override 沿父树继承，最近 override 优先于
  远祖和主题默认值。
- Style/Theme 变化必须触发受影响控件重绘。

### 7.2 文本与国际化

- 所有 JSON 文件按 UTF-8 处理。
- 公共文本 API 以 `std::wstring` 为主，以匹配 Windows 文本/IME 路径。
- loader 和 serializer 必须进行真实 UTF-8 ↔ wide conversion，禁止用 iterator 逐字节窄化/拓宽。
- `I18n` 表格式为 `{ key: { language: translation } }`；缺少当前语言时回退到 `en`，再缺失时返回 key/default。
- `ui.` 前缀由 `isI18nKey` 识别，loader 只有在设置了 I18n 时才解析。
- RichText paragraph layout 以 run 为样式边界，输出 line/fragment 几何；实际 glyph shaping 和
  rasterization 仍委托 IRenderBackend，避免控件层持有字体或 GPU 资源。

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
  "accessibilityRole": "button",
  "accessibilityLabel": "Confirm changes",
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

- 40 个公共注册类型都具有明确 type 决策和往返测试；通用位置、尺寸、可见性、style、opacity、
  layout-managed flags、style override 与 accessibility 元数据可往返。
- 核心叶控件、列表/树、Box、Panel、Window、Spinner 的视觉和行为字段有类型覆盖。
- UTF-8 文本必须无损往返。
- GridPanel 使用 row/column definitions、padding、spacing 和 `cells[]`，每个 cell 显式保存
  row/column/span/alignment/content；通用 `children` 只作为旧格式读取兼容，不与 cells 重复导出。
- ScrollView、Menu/MenuItem/MenuBar、StatusBar、TabControl/TabStrip、Modal/ModalDialog、
  DockCard/DockOverlay/DockArea 使用专用结构化内容，内部实现子树不作为通用 children 重复序列化。
- TabStrip overflow/min width 与 RichText paragraph/run 样式属于持久数据；scroll offset、layout
  fragments、semantic runtime ID 等派生/瞬态状态不写入 wire format。
- DockCard content、DockArea slot/floating/weight/min size 和 Modal 的 owned content 在反序列化后
  恢复明确所有权。

持久化边界不包含回调函数、焦点/hover/capture、动画瞬时值、拖拽会话、纹理后端句柄等运行时状态。
`DockTabGroup` 属于 DockArea 内部实现，不是独立注册或持久化根类型。

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

`tick()` override 必须先保持基类级联，再推进自身状态。动画中的 Widget 每帧标脏并重建自己的
display-list；完成后停止无意义的命令重建，改为稳定 replay，但可见内容仍按后端契约每帧提交。

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

Gallery 当前包含 Basics、Images、Input、Collections、Overlay、Layout、Capabilities、Backend、
Animation 和 Productization 十页；Images 页覆盖共享 `TextureRegistry` 纹理、UV crop、透明图片层
和控件/图片叠加，Productization 页可交互验证 UI scale、语义快照、父主题、Tab overflow、
RichText 和 Clipboard。

AYRenderer 的 `UIRenderBackend` 实现 `IRenderBackend`，UIPass 在 3D pass 后合成 UI；当前支持
display-list 的逐帧 replay，以及 CPU path tessellation + stencil path fill/clip，但生产 RenderTarget/Layer 能力尚未启用。MockRenderer 用于无 GPU
单测，并实现 Layer 生命周期测试面，不应和生产 backend 行为产生不同的 Widget 语义。

## 13. 测试与审计基线

2026-08-28 全模块审计统计：

- 76 个公共/支持 header
- 64 个非 demo、非 unittest 的 `.cpp`
- 92 个 `Test_*.cpp`
- 988 个 `TEST_CASE`
- Windows Debug：`4436 / 4436` 条断言通过

断言总数从旧基线的 7405 收敛到 4229，是因为参数矩阵、逐帧动画和压力循环不再在每次
迭代中调用 `CHECK`；循环体只累计失败数，并在循环结束后统一断言。测试文件数、测试用例
数和输入迭代次数均未减少。Retained display-list、Layer、Serializer、vector path 与产品化
能力测试随后把当前基线增加到 `4436 / 4436`。

审计覆盖：

- 全量构建和单测退出码
- Factory 注册与 serializer 类型一致性
- JSON/i18n UTF-8 文本
- 控件生命周期和拥有/非拥有指针
- dirty/cache invalidation、retained display-list、即时兜底、frame-local backend replay、
  world-bounds cache 和容器 clip/hit-test 契约
- Layer/RenderTarget 的 DPI、resize、damage、paint/composite 和 release 生命周期
- DPI/UI scale 的逻辑布局、物理输入换算、语义树/动作、主题继承与控件级联、Tab overflow、
  RichText wrap/justify/ellipsis/decoration/命中和 Serializer 往返
- TreeView 千节点重复建树性能
- 全部 `for` / `while` / `do` 循环及标准算法回调中的重复断言扫描
- Docking、布局保存/加载和 popup/modal 行为
- 生产 GPU 后端的 `OrderedRuns` / `OverlapAware` 十路径逐像素对照
- README/design 与当前实现偏差

本轮修复包括：

- 测试入口真实返回失败码，移除固定磁盘路径和不稳定堆地址断言。
- Spinner serializer 类型缺口。
- LayoutLoader 与 WidgetSerializer 的 UTF-8 无损转换。
- TreeView 复用 row widget pool，避免重复 allocator churn。
- 23 个测试文件中的循环内断言改为失败计数汇总；静态复扫结果为 0 个循环内 `CHECK`。
- AYUI 单测试采用单 translation unit include 模式；CMake 显式声明全部 `Test_*.cpp` 为
  `main.cpp` 的对象依赖，防止 MSVC/Ninja 漏记 include 后运行陈旧测试二进制。
- TreeView 千节点性能门槛按测试名统一为单次平均 `< 5ms`，不再误用 50 次总耗时 `< 100ms`。
- 独立 Factory 补齐 Dimmer/Modal/ModalDialog/TabStrip 注册。
- ScrollView 运行时 scrollbar enable/disable、Box 自然尺寸缓存和多个视觉 setter 的 invalidation。
- 公共聚合头和 CMake header 清单补齐。
- Widget-local retained display-list、复杂控件 replay 顺序、祖先几何级联失效与显式即时兜底。
- RenderTarget/UI Layer 接口及 MockRenderer 契约；AYRenderer 未实现 FBO 时明确关闭 capability。
- 40 个注册 Widget 的 serializer type/字段往返，Grid cell 和复合控件结构化 payload。
- backend-independent retained path recipe；AYRenderer 凹多边形/曲线 tessellation、winding 孔洞、
  miter stroke、嵌套 stencil path clip 和排序屏障。
- 逻辑 DIP/物理 framebuffer 分离、无障碍语义 snapshot/action、Theme 与 Widget token 继承、
  TabStrip 三种 overflow、RichText paragraph layout 和 POSIX Clipboard helper backend。

回归测试失败必须让进程返回非零；不得通过 batch wrapper 抹掉退出码。

合批视觉回归由 `demo/RunBatchVisualRegression.ps1` 驱动。它固定 Gallery 的时间步、页面、
交互动作和截图帧，在独立隐藏进程中运行两种 batch mode；当前十条路径均为字节级一致，
draw call 从保守路径的 60–94 次降至 23–41 次。这个结果锁定的是当前 Gallery 复杂控件路径，
不应被解释为所有未来自定义控件都会得到相同降幅。

## 14. 已知限制与后续工作

按优先级记录剩余边界：

1. 为 AYRenderer 实现生产 bgfx RenderTarget/Layer：FBO 与纹理回收、clear/preserve、局部 damage、
   resize/DPI、overlay 合成顺序和显存预算；在此之前保持 capability 关闭。
2. vector path 补充 self-intersection/fill-rule、布尔组合、join/cap 选择和独立 AA fringe；当前明确
   支持 simple contour、显式 clockwise hole 和 stencil nesting，不隐式承诺任意 SVG 语义。
3. 为 UI Automation、AT-SPI 与 NSAccessibility 提供宿主 adapter、原生事件发布和增量语义树；
   AYUI 核心继续只维护平台无关 snapshot/action 契约。
4. RichText 的下一层是 Unicode grapheme/bidi/UAX #14、内联对象和字体 family/weight face 选择；
   当前 paragraph layout 已覆盖产品常用 wrap/alignment/ellipsis/decorations/measure/hit/caret。
5. 将目前自动即时兜底的粒子和资源引用逐类评估为可安全保留的 typed command；不能保证
   句柄生命周期的操作继续保留为排序/缓存屏障。
6. 可选：统一散落在 loader、serializer、IME 和 i18n 中的 UTF-8 工具为一个经过测试的公共内部组件。

这些限制不阻塞当前 v1.5 功能，但实现新特性时不得继续扩大重复路径。

### 14.1 高价值扩展顺序

后续扩展按依赖关系和收益排序，不以增加控件数量为优先目标：

1. **Retained display-list（第一阶段完成）**：dirty Widget 只重建自己的本地高层绘制指令，
   clean Widget 每帧按 z-order replay；不缓存 bgfx transient buffer 或跨帧 `UiItem`。旧即时路径
   保留为显式策略和不安全命令的自动兜底。下一步是降低 `std::function` 存储开销、增加缓存
   内存统计/预算，并逐类扩展 typed command，而不是复制第二套 Widget 渲染器。
2. **UI Layer / RenderTarget（契约与 Mock 完成）**：接口已经定义透明、clear/preserve、damage、
   resize、DPI 和 overlay 元数据，MockRenderer 已锁定生命周期。下一步在 AYRenderer 实现 bgfx
   FBO/纹理池和显存回收，再选择高收益稳定子树 opt-in；滤镜、背景模糊、多 viewport 以及未来
   `UIPlane` / 世界空间 UI 均建立在该能力之上。
3. **Serializer 完整化（完成）**：40 个公共注册类型均有 type 决策；Grid cell、ScrollView、
   Menu/StatusBar、Tab、Modal 和 Dock 复合结构具有专用 wire contract 与往返测试。运行时瞬态明确排除。
4. **高级裁剪和矢量路径（第一阶段完成）**：DisplayList 保留 backend-independent path recipe；
   AYRenderer 共享一套 tessellation/submit 路径实现凹多边形、曲线、stroke、winding hole 与嵌套
   stencil clip。path fill/clip 是显式排序屏障，两种 batch mode 不复制实现。下一阶段是 SVG 级
   fill-rule/boolean/join-cap 和 AA fringe。
5. **产品化能力（第一阶段完成）**：Widget 使用逻辑 DIP，DPI/UI scale 在输入和最终 raster
   边界闭环；无障碍 snapshot/action、Theme/Widget 两级继承、TabStrip Scroll/Compress/Clip、
   RichText paragraph layout 和 Win32/macOS/Wayland/X11 Clipboard 均有 API、Serializer、Gallery
   或单测覆盖。下一阶段仅扩展原生 accessibility adapter、Unicode 编辑与字体 face，不复制核心路径。

`OrderedRuns` 与 `OverlapAware` 只允许在提交顺序规划上分叉；图元记录、合批兼容键、
顶点/索引构建、shader 和 submit 必须共享。新图元若不能安全重排，应进入统一命令流并声明
排序屏障，而不是在两种 batch mode 中各实现一次。

## 15. 决策摘要

| 决策 | 结论 |
|---|---|
| UI 模式 | 保留模式 Widget tree |
| 数据格式 | 复用 JSON UILayoutLoader，不引入第二套配置系统 |
| 渲染解耦 | `IRenderBackend`，AYRenderer 提供实现 |
| 帧提交 | 可见 Widget 每帧遍历；dirty 重建本地 display-list，clean replay，bgfx 仍逐帧 submit |
| 像素层缓存 | Layer/RenderTarget 契约与 Mock 已完成；AYRenderer capability 暂时关闭 |
| 矢量路径 | retained recipe；AYRenderer CPU tessellation + stencil fill/clip；复杂 path 是合批排序屏障 |
| DPI/UI scale | Widget/damage/accessibility 使用 DIP；宿主输入与 framebuffer 使用物理像素；后端最终缩放 |
| 无障碍 | AYUI 输出平台无关 snapshot/action；原生 UIA/AT-SPI/NSAccessibility 由宿主 adapter 发布 |
| 主题 | 命名 parent theme + 控件树 token override 级联，parent-first / nearest-wins |
| Tab/RichText | Tab 三种 overflow；RichText 输出 paragraph line/fragment 几何并委托 backend shaping |
| Clipboard | Win32 原生；macOS/Wayland/X11 helper backend，UTF-8、失败显式返回 |
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
