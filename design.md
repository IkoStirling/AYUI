# AYUI Design

**文档修订：** 2026-09-08

**CMake 目标版本：** 1.0.0

**功能里程碑：** v1.6 已实现

**状态：** 根工程集成、Widget-local retained display-list、Production root/subtree UI Layer、共享
RenderTargetPool、Serializer 完整化、AYRenderer 合批与 vector-path/stencil clip、DPI/UI scale、无障碍语义、主题继承、
Tab/RichText 产品化、Unicode shaping、Windows UI Automation adapter、POSIX Clipboard、Docking、
Layout Editor（扩展工具箱、图片预览、声明式 controller/event）、动画与完整单测均可构建。

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

### 1.1 版本与兼容性口径

- CMake `project(VERSION 1.0.0)` 是包/目标版本；`v1.6` 是能力完成度里程碑，不构成第二套
  发布版本。正式发布记录以 CMake 版本和 `CHANGELOG.md` 为准。
- 1.x 稳定源码面包括 `AYUI.h`、Widget/UIManager、内置控件、布局、Theme、Loader、
  Serializer 和 `IRenderBackend` 的基础契约。新增能力优先使用可选字段、重载或 capability gate。
- 已注册 JSON `type` 名和文档化持久字段属于 wire compatibility；运行时焦点、hover、拖放、
  动画进度、display-list、GPU/平台句柄不进入持久格式。
- 当前不承诺 C++ 二进制 ABI。公共头、编译器或构建选项变化后，AYUI、AYRenderer 和宿主必须
  一起重新编译。RenderTarget/Layer、原生平台 adapter 和 display-list 内部表示仍可演进。

## 2. 系统位置

```text
AYDevice / host events
          │
          ▼
      UIManager ────────────── UILayoutLoader / I18n / Theme
          │
          ├─ root ───────────── optional retained pixel layer
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
| `LayoutEditor/` | Editor-only document/selection/command/viewport、authoring registry/schema 与 Session |
| `i18n/` | JSON 语言表和 UTF-8 解析 |
| `unittest/` | 单元、场景、生命周期和性能回归 |
| `demo/` | Gallery、Layout Editor standalone/round-trip 宿主 |

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
   ├─ ScrollView / ListView / TileView / TreeView / ComboBox / TabControl
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
- 自由布局 child 可选用 `AnchorLayout`。每个轴的边界满足
  `childMin = parentSize * anchorMin + offsetMin`、
  `childMax = parentSize * anchorMax + offsetMax`；anchor 使用 `[0, 1]` 归一化父级坐标，offset 使用
  DIP。`anchorMin == anchorMax` 表示固定锚点，二者不同表示随父级伸缩。Pivot 是后续旋转/缩放的
  变换原点语义，不改变上述边界方程。
- Anchor 仅在父级没有接管 child position/size 时参与 descend layout；Grid/VBox/HBox 的 cell/slot
  规则拥有确定的更高优先级。切换 Anchor preset 必须重算 offset 以保持当前视觉矩形，直接移动、
  resize、nudge、snap 或 align 后则反向刷新 offset，避免下一帧布局弹回。内层自由容器的
  `setSize()` 必须立即把新尺寸传播给锚定 child，不能只等待 UIManager 的 viewport layout gate。
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

`UIManager::setRootLayerCachingEnabled(true)` 在上述命令保留之上再增加一层可选像素保留：首次、
主树 dirty、尺寸/DPI 变化或后端报告 layer dirty 时，将 root 完整绘制到透明离屏层；clean 帧不再
遍历/replay 主树，只在主 view 提交一次 composite。`markDirty(rect)` 在 sidecar 中保留最多 8 个
damage region；重叠或接触的矩形立即合并，第 9 个区域退化为 union，累计 repaint 面积达到 layer
面积 70% 时改为 full redraw。其余情况逐 region clear/clip/replay，避免两个相距很远的小更新被
union 成大面积重绘。overlay root 与 drag ghost 始终随后即时绘制，保证 popup/modal/tooltip 的
painter order。capability、create/update、paint 或 composite 前置条件失败时，同一帧直接执行原始
`_root->render()`，所以 Production Layer 不是第二套 Widget renderer。

`Widget::setLayerCachePolicy(Always/Auto)` 把相同像素保留能力下沉到复杂子树。`Always` 强制缓存；
`Auto` 在连续 3 个 clean frame、逻辑面积至少 4096、估算 display command 至少 12 条时晋升，连续
3 个 invalidated frame 时撤销 backing 并回到 display-list。子树 paint 调用原
`renderSubtreeContent()`，可以嵌套 path clip/子 Layer；任何失败都只影响当前帧并立即走原路径。

通用 Recorder 不跨帧持有后端资源句柄。vector path 以 backend-independent construction recipe
记录；每条 draw/clip 命令捕获当时的 immutable operation snapshot，replay 时临时创建后端路径、
重建、提交并释放，因此 releasePath 和后续 path mutation 不会改变已缓存命令。粒子更新、字体/
动画资源创建、RenderTarget/Layer 生命周期或显式 pass 控制仍使候选 list 失效；Recorder 已把
本帧命令转发给真实后端，下一帧继续旧即时路径。`DisplayListPolicy::Immediate` 可显式保留该路径，
用于自定义控件兼容、诊断和 A/B 对照。

`SvgDocument` 在这条 vector-path recipe 上提供原生矢量图标层，不引入 SVG→PNG 烘焙器或第三方
DOM/rasterizer。当前安全子集支持 `<svg>`/`<path>`、`viewBox`、多个 path/subpath、完整
`M/L/H/V/C/S/Q/T/A/Z` 命令、fill/stroke/currentColor/opacity，以及 butt/round/square cap 和
miter/round/bevel join；曲线与椭圆弧在提交时自适应离散，按 `xMidYMid meet` 缩放到逻辑 DIP。
`SvgIcon` 是叶 Widget，`Button` 可持有共享不可变 `SvgDocument`，因此 DPI 变化不需要重新生成
位图或持有后端纹理句柄。为避免“部分画对、部分静默丢失”，`transform`、CSS `style`、显式
`fill-rule`、clip/mask/filter 和非 path 图元目前直接返回解析错误；输入还受 4 MiB 文档与路径/
命令数量上限约束。这是图标 SVG 子集，不等同浏览器级 SVG 实现。

图片组合遵循同一树顺序：父节点先执行 `onRender()`，子节点再按插入顺序绘制，`bringToFront()` 可把同级节点移到末尾。`Image` 提供纹理句柄、UV 和 opacity；图片按钮使用 `Button -> Image` 装饰子节点，命中仍返回 Button。图片背景上的交互层应使用 `Panel -> [Image, ..., Button]`，Panel 会下降命中并让后加入的 Button 覆盖在图片上。Gallery 的 `Images` 页面是该契约的可视化回归样例。

渲染后端负责矩形、边框、文本、纹理、裁剪、opacity stack 等图元，不负责 Widget 生命周期或布局。`UIRenderBackend` 的 `UiItem`、clip stack 和 transient geometry 都是 frame-local；不得通过跨帧保留 `items` 来模拟 Widget 缓存。

### 4.4 UI Layer / RenderTarget 抽象

`IRenderBackend` 已定义两级持久渲染资源：

- `RenderTargetDesc` / `RenderTargetHandle` 表达物理目标的尺寸、DPI、alpha 和内容保留策略，
  并提供创建、resize、bind、blit、纹理查询和释放。
- `LayerDesc` / `LayerHandle` 在 RenderTarget 之上表达逻辑 bounds、DPI、alpha、overlay、
  clear mode/color；`LayerPaint` 携带局部 damage 和 full-redraw 选择。

Layer 保留的是像素，display-list 保留的是绘制命令，两者不能混为一套缓存。Layer 的逻辑尺寸
变化或 DPI 变化必须把逻辑边界的 min/max 分别按 DPI 向外 `floor/ceil` 到物理像素，backing target
使用两条对齐边之差并要求重绘；只对 `logicalSize * dpiScale` 做 `ceil` 会让小数 origin 的离屏像素
中心相对主 framebuffer 偏移，point composite 在图元边缘产生一像素接缝。合成完整 backing 后必须
裁回公开 logical bounds，Color padding 不得泄漏到层外。
dirty Layer 在 `beginLayerPaint()` / `endLayerPaint()` 完成前不能 composite。局部 damage 是实际重绘
契约：调用方必须把 replay 限制在 damage clip 内；Transparent/Color Layer 先以无混合覆盖写清除
damage，Preserve Layer 保留原像素。任何实现都不得破坏区域外像素、clip、透明和 painter order。

该 API 是 capability-gated：不支持的后端返回无效 handle 或 `false`，调用方必须继续即时绘制。
MockRenderer 实现完整生命周期和可观察事件；AYRenderer 现已实现 bgfx FBO、纹理查询/blit、
Layer create/update/release、paint/composite/invalidate 和 reset 后重建。离屏 paint 使用 view 26–249，
主 UI composite 使用 view 255；target 切换前 flush 当前 batch，并在 paint 结束后恢复 canvas、clip、
opacity、blend 与 path-clip 状态。Layer backing target 带 depth/stencil，因此离屏复杂控件路径继续
支持 nested path clip。每帧最多调度 224 次离屏 pass；第 225 次确定失败，下一帧从 view 26 重新开始。
这个数值描述 pass 容量，不表示 UI 可以独占同等数量的 renderer-wide framebuffer 句柄。

AYRenderer 的 FrameGraph 与 UI Layer 共用 renderer-wide RenderTargetPool。池按宽、高、格式、
depth、sampleCount 和采样方式精确匹配，lease 带 generation；release 后默认隔离两帧再复用/销毁。
FrameGraph 使用允许暂时超预算的 soft acquire；UI Layer 使用 strict acquire，先淘汰越过 quarantine
的 idle LRU，再撤销最久未 composite 的 Layer backing。逻辑 LayerHandle 不随 backing 被撤销而失效，
只重新标脏；当前帧若仍拿不到 target 则 immediate 降级，后续帧在 quarantine 结束后重试。这样
256 MiB 预算对 UI 是硬上限，对既有 FrameGraph 行为仍是 best-effort。resize/MSAA/device reset 会
立即失效 lease 并使逻辑 Layer 变 dirty。当前仅支持 1× sample，所有调用限定 renderer thread。
`Preserve` 仍可供不需要背景清除的后端/调用方使用。

`LayerCacheStats` 把策略反馈闭环暴露给宿主：layer create/release、full/partial paint、composite、
cache hit、物理 repaint area、allocation failure/degradation，以及 pool allocation/reuse/eviction、
live lease、idle target、allocated/budget bytes。重置统计不影响 live cache；预算调整可在下一次
acquire/beginFrame 触发回收。

UI RenderTarget 使用 point sampling 保持同尺寸复合的 texel identity；FrameGraph 默认目标仍使用线性
采样，二者通过 pool key 隔离，不能误复用。Layer 内的 straight-alpha draw 使用独立 RGB/alpha
source-over，使 backing texture 保存 premultiplied RGB 与正确 coverage alpha；最终 composite 使用
premultiplied-over，避免抗锯齿边缘二次乘 alpha。局部 replay 的所有图元必须满足“裁剪不重新布局”——
纹理重映射 UV，四角渐变按原 bounds 双线性重映射颜色，SDF/path 使用原 shape 和 damage clip。
半透明 Color clear 同样必须先写入 `(rgb*alpha, alpha)`，不能把 straight RGB 直接放进 premultiplied
backing。Additive/Multiply/Screen 只改变 RGB blend 方程，alpha 一律独立使用 coverage source-over；
否则 Multiply/Screen 会把已经不透明的隔离层重新变成透明，最终错误混入 Layer 外部背景。

### 4.5 产品化呈现契约

1. **DPI 与 UI scale**：DPI 来自窗口所在显示器，UI scale 来自用户偏好，二者相乘但生命周期
   分离。宿主在 DPI change 和 framebuffer resize 时分别更新对应值；Gallery 已处理
   `WM_DPICHANGED`，自动化截图固定 scale=1 以保持可复现。
2. **无障碍语义与原生桥**：每个 Widget 在运行期获得稳定 ID。`buildAccessibilityTree()` 对内置
   控件推断 role、label、value、range、state 和 actions，显式 accessibility 元数据具有更高优先级；
   隐藏节点和不可见子树不进入快照。`AccessibilityAdapter` 扁平化并比较相邻快照。Windows 实现
   `IRawElementProviderSimple/Fragment/FragmentRoot`，支持 Invoke、Toggle、RangeValue、
   ExpandCollapse 和 SelectionItem pattern，处理 `WM_GETOBJECT`，发布结构/属性/焦点事件；UIA
   工作线程上的动作通过私有窗口消息同步封送回 UI 线程。adapter 每帧在 layout 后更新，并必须在
   UIManager shutdown 前销毁。非 Windows 当前只保留 snapshot/diff，AT-SPI/NSAccessibility 是同一
   平台无关状态机的后续 provider，不得复制 Widget 语义推断。
3. **Theme 继承**：Theme 通过名字延迟解析 `extends`，token 和 sheet 均按 parent-first、
   child-wins 合成；visited set 使缺失父级或继承环安全终止。控件树上的 token override 从祖先
   向后代级联，最近节点胜出，修改祖先 override 会标脏整个 style 子树。
4. **TabStrip overflow**：Scroll 是默认策略，维护 logical offset/max offset、滚轮滚动和选中项
   自动可见；Compress 从配置的 `minTabWidth` 向 24 DIP 交互硬下限按可用宽度分配；Clip 保留旧固定宽度行为。
   三种模式均裁剪 child paint，不允许 tab 越过 strip 覆盖相邻控件。
5. **RichText Unicode 段落布局**：run 保存字号、颜色、family、weight、bold/italic、language、
   下划线、删除线、字距和基线偏移；paragraph layout 支持显式换行、word/character wrap、
   left/center/right/justify、垂直对齐、行高/行距、最大行数、clip/ellipsis，并输出
   fragments/lines/contentSize/caret stops。`UnicodeText` 先按扩展字素簇切分，再计算 UAX #14 兼容
   soft break 与 bidi level；Windows 使用 Uniscribe 的系统 Unicode 表增强 itemize/break，portable
   路径提供确定性子集。每一行按 UAX #9 L2 进行可视重排。后端 `shapeText()` 返回与实际 draw path
   相同的 source cluster/advance；一个 shaping cluster 内不可换行，letter spacing 只应用一次。
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

### 6.1 TileView：二维虚拟集合

`TileView` 是面向编辑器资产、缩略图浏览和大数据图块的二维虚拟集合控件。它不为 N 个 item
建立 N 个 Widget，而只维护
`(可见行 + 2 * overscanRows) * 当前列数` 个 `TileCell`。列数由 client width、tile size、
spacing、padding 和自动垂直 scrollbar 共同求解；窗口宽度改变后逻辑行会重排，选择仍由逻辑 index
保存，不依赖 cell 地址。滚动只更新 first pooled row 并重绑池中 cell；`CellBinder` 只访问当前池，
因此宿主可投影复杂模型，但不得长期保存 `TileCell*` 与数据项的一一对应关系。

焦点游标 `_focusedIndex` 与 `_selectedIndices` 分离：普通方向键移动焦点并选择，Ctrl+方向键只移动
焦点，Shift+方向键从 anchor 扩展连续范围；左右按 1、上下按当前列数移动，Home/End 和
PageUp/PageDown 使用同一网格映射。F2 只发出 rename request，不在 AYUI 内创建编辑器；Enter 发出
activate。双击识别由 `TileCell::HitRegion` 固定为 `Thumbnail / Label / Body`，宿主无需
根据像素重复推断区域；安装 region-aware callback 后由它独占该手势，否则回退到 item activate。

TileCell 在按下时完成选择，移动超过 DIP 阈值后提升到 UIManager 通用 drag session。默认
`DragPayload.kind` 为 `AYUI.TileItems`，宿主可提供 payload builder 改为 FileList/EditorAssets 等
领域类型。拖拽会话活动期间，TileView 用 UIManager 的最后指针位置做上下边缘自动滚动；池可继续
重绑，但 drag payload 已在 beginDrag 时复制，不会因源 cell 改绑而改变。滚轮、惯性、scrollbar
drag、keyboard ensure-visible 与 edge auto-scroll 最终都经过同一 offset clamp/rebind/bar-sync
路径。

TileView 统一持有 `infoStripHeight`、`cornerMarkerSize` 和 `thumbnailAspectRatio`，保证所有池化 cell
采用相同的“按比例预览 → InfoStrip → 文件名”几何。`InfoStrip` 只承载宿主提供的文字与颜色，
`CornerMarker` 只承载可见性与颜色；AYUI 不定义资源类型、类别或编辑器标记语义。角标通过通用
vector path 绘制并位于预览之上，cell 的 focus/selection border 始终最后绘制。InfoStrip 按 Body
命中；可见角标覆盖处也按 Body 命中，不增加专用 HitRegion。`resetPresentation()` 在每次 binder
之前清空 InfoStrip、角标、缩略图、Badge、辅助文字及其余瞬态展示状态，防止池化复用串项。

TileView 的 cell pool 和 scrollbar 是运行时内部子节点，不写入 `children[]`。Serializer 持久化
items、选择/焦点、网格尺寸、InfoStrip/角标/缩略图布局参数、overscan、drag 开关和 scroll offset；
单 item 的 InfoStrip 文本/颜色、CornerMarker 状态/颜色、binder、回调、hover、press 和 drag
session 都是瞬态。每个可见 TileCell 发布 ListItem 语义及 Select/Press/Focus 动作，TileView 发布
List 语义。

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
- RichText paragraph layout 以 Unicode grapheme、bidi level 和 run 样式为边界，输出 line/fragment/
  caret 几何；实际 glyph shaping 和 rasterization 仍委托 `IRenderBackend::shapeText/drawText`，避免
  控件层持有字体或 GPU 资源。
- AYFont HarfBuzz 输入在 Windows 使用 UTF-16，在其他 `wchar_t==4` 平台使用 UTF-32；direction、
  language 和 monotone grapheme cluster 均显式设置，不能把 UTF-16 low surrogate 暴露为 source start。
- AYRenderer 按 `(family, rasterSize, weight, italic)` 缓存 face，每个 face 持有独立 GPU atlas；测量、
  cluster geometry 和 draw 必须选择同一 face、direction、language。不同 atlas 是合法合批边界，
  同 atlas/状态的连续 glyph 仍合为一个 draw call。

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
  "controller": "SettingsController",
  "events": {
    "onClick": "confirm"
  }
}
```

Loader 还解析各控件的专用字段，例如 Box spacing/padding/gravity、ListView selection、Image textureName、Grid cells、Dock cards/floating/weights。

事件字符串不是脚本。Widget 保存 controller ID 和 `eventName -> handlerName` 元数据；Loader 按以下
优先级解析宿主注册的 C++ 回调：

1. `bindEvent(widgetId, eventName, callback)`，兼容旧布局和单实例精确覆盖；
2. `bindControllerEvent(controllerId, handlerName, callback)`，供同一 controller 下多个 Widget 复用；
3. `bindHandler(handlerName, callback)`，供宿主注册全局命名动作。

内置类型接线覆盖 `onClick`、`onToggled`、`onValueChanged`、`onTextChanged`、`onSubmit`、
`onSelectionChanged`、`onItemActivated` 与 `onClose`。回调参数仍由具体宿主/controller 从自己的状态
读取；RadioButton 参与 `onToggled`，TextArea 参与 `onTextChanged`。JSON 不承诺动态执行脚本或反序列化函数。旧顶层 `onClick` 继续读入并归一化为
`events.onClick`。

### 8.2 WidgetFactory

Factory 是唯一的 `type` → constructor 注册表。默认注册覆盖普通控件、布局、DockArea/DockCard/DockOverlay，以及 Dimmer/Modal/ModalDialog/TabStrip。`UILayoutLoader` 不应依赖先构造 UIManager 才能获得内置注册。

`DockTabGroup` 是 DockArea 的内部 leaf，由 DockArea 创建，不作为通用 JSON 根类型。

### 8.3 WidgetSerializer

Serializer 服务于测试、编辑器导出和 Dock 布局持久化。其保证分层：

- 41 个公共注册类型都具有明确 type 决策和往返测试；通用位置、尺寸、可见性、style、opacity、
  layout-managed flags、style override、accessibility 及 controller/event 元数据可往返。
- 核心叶控件、列表/树、Box、Panel、Window、Spinner 的视觉和行为字段有类型覆盖。
- UTF-8 文本必须无损往返。
- GridPanel 使用 row/column definitions、padding、spacing 和 `cells[]`，每个 cell 显式保存
  row/column/span/alignment/content；通用 `children` 只作为旧格式读取兼容，不与 cells 重复导出。
- ScrollView、Menu/MenuItem/MenuBar、StatusBar、TabControl/TabStrip、Modal/ModalDialog、
  DockCard/DockOverlay/DockArea 使用专用结构化内容，内部实现子树不作为通用 children 重复序列化。
- 生产 `UILayoutLoader` 必须与 Serializer 的结构化格式对称；Tab page、Modal content/body 等嵌套
  payload 通过 `buildWidgetTree()` 递归加载，使 inactive page 和深层事件节点也进入 ID 注册表。
- TabStrip overflow/min width 与 RichText paragraph/run 样式属于持久数据；scroll offset、layout
  fragments、semantic runtime ID 等派生/瞬态状态不写入 wire format。
- DockCard content、DockArea slot/floating/weight/min size 和 Modal 的 owned content 在反序列化后
  恢复明确所有权。

持久化边界不包含回调函数、焦点/hover/capture、动画瞬时值、拖拽会话、纹理后端句柄等运行时状态。
处理器名字属于数据并可持久化，但只有宿主显式注册后才会产生行为。
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
- 模板树的 self-heal 必须先于 pristine weight 同步；同步时 root-mid 和 Center 显式恢复为
  `size=0` 的 fill slot。临时折叠/恢复 Bottom 不能把 Bottom 误设为 fill 并把 Center 压到最小值。
- 保存格式记录 dock topology、slot weight/min size 和 floating frame，而不是序列化内部临时 hover/drag 状态。

### 9.4 Layout Editor 宿主边界

`AYUILayoutEditorCore` 是不进入游戏 runtime 的 authoring 静态库；独立 `AYUI_LayoutEditor` 与
AYEditor 的独立 Designer 工具窗链接同一库。`LayoutEditorSession` 是 UI/手势协调器，
`attach(UIManager&)` 支持完整窗口 chrome；
`attach(UIManager&, Widget* chromeRoot)` 支持通用 View fallback，使 `canvas_host`、Palette、
Hierarchy 和属性栏 id 只在给定子树内解析。文档状态回调只发布路径/dirty，不引入 AYEditor
文档类型、Scene、资源或原生窗口语义。

Authoring 状态拆为四个独立组件：`LayoutDocumentModel` 持有 root/path/dirty、全树 ID index 与重复
检测；`LayoutSelectionModel` 维护 primary/multi-selection 不变量；`LayoutCommandStack` 管理事务、
coalescing 与 undo/redo；`LayoutCanvasViewport` 维护非破坏性的 view transform。Session 仍协调画布
手势和 `UILayoutLoader`/Serializer 往返；owner window、
渲染 backend、资源身份、关闭提示和工作区命令路由由上层宿主负责。AYEditor 通过一个共享
Controller 适配 Session，正常路径不再嵌入 Scene Center；AYUI 不知道自己运行在 standalone、
Dock fallback 还是 AYDevice 顶层窗口中，也不复制第二套布局编辑状态机。

`WidgetAuthoringRegistry` 是 Palette 与 Inspector 的单一 authoring 元数据源，集中 type/display name、
分类、SVG、ID prefix、默认尺寸、factory 后初始化、可编辑属性和事件 schema；运行时构造仍委托
`WidgetFactory`。`PropertySchema` 以字段和 section 描述 Inspector，并携带 Text/Number/Integer/
Boolean/Enum/Color/Resource 编辑器种类、可选数值范围/步长和枚举选项；类型切换、控件绑定和选项列表
共享同一元数据，不再依赖持续增长的显隐矩阵或重复字符串表。属性读写中的少量控件 adapter 仍允许渐进迁移，但新类型首先注册 descriptor，
不得再增加平行的 Palette/default-size/icon 表。

类型化 Inspector 第一阶段覆盖 Slider/ProgressBar 的范围和值、Image tint 与 UV crop、ListView/
TileView 选择模式和集合尺寸、Tree/List item height、ScrollView 两轴 scrollbar policy、TabStrip overflow/
min width、Grid 行列/间距以及 RichText wrap/overflow/line-height/max-lines。数值输入按 schema 约束归一；
Grid 不允许缩小到会丢弃已占用 cell 的尺寸。Image color/UV 和 Grid spacing 在 `UILayoutLoader` 与
`WidgetSerializer` 中对称往返，避免“Inspector 可编辑但保存丢失”的半实现状态。第二阶段把显示名也
纳入 schema，并在 Session attach 时自动创建这 25 个类型专用属性行；两份 chrome JSON 不再复制其
label/control 定义。复杂基础行仍保留声明式 JSON，旧版或部分迁移的 chrome 若已提供相同 row id，
生成器会复用它而不会创建重复控件，文档模型和属性写入协议保持不变。`UIManager::findById()` 保持
Loader registry 的 O(1) 快路径，并在 miss 时遍历实时树，使动态生成控件与静态控件具有一致查询语义。

命令栈已经区分 `Property`、`Insert`、`Delete`、`Reorder`、`Transform`、`Clipboard` 与
`SnapshotFallback` edit intent。当前 entry 同时保存完整 JSON snapshot，作为复杂复合控件和旧路径的
可靠恢复兜底；后续可以逐类替换为小粒度 typed command，而不改变 Session/宿主接口。snapshot 包含
dirty 状态，undo 到已保存版本会恢复 clean，redo 才重新进入 dirty。

Chrome 本身采用 File/Edit 菜单栏、单列 Widget Library + Document Outline、Canvas、滚动
Inspector、状态栏布局。`UILayoutLoader` 与 Serializer 都支持 MenuBar 的结构化 `menus/items`
声明及 shortcut/submenu；命令不再依赖一排临时按钮。Widget Library 行绑定类型专属 SVG，点击
和拖放共享同一创建语义；对齐、分布与 Snap 属于 Inspector 的选择上下文。

自由布局 Inspector 提供 4×4 Anchor preset：横轴 Start/Center/End/Stretch 与纵轴
Start/Center/End/Stretch 的笛卡尔组合，并提供 Absolute 退回入口。选中 anchored child 时画布绘制
琥珀色 anchor range/point，属性区暴露归一化 Min/Max、DIP OffsetMin/OffsetMax 与 Pivot；preset、
数值编辑、清除 Anchor 均进入同一 undo/redo snapshot 和 Serializer 往返。该 section 对 document
root、结构 content root 以及 Grid/VBox/HBox child 隐藏。Align 只在至少两个同父级自由控件时可用，
Distribute 至少三个，其余状态禁用而不是静默执行。普通 preset 点击保留当前矩形；Ctrl+点击把
固定轴的 widget pivot 吸附到 anchor，Stretch 轴使用零边距贴合父级，且操作仍可撤销。

工具箱按 Basic/Input/Collections/Layout/Overlay 分类，覆盖常用文本和交互叶控件、Image、
ComboBox/ListView/TileView/TreeView、TabStrip/TabControl、Grid/Scroll 及 Window/Modal。新增控件必须
通过 WidgetFactory 创建，不能在 Designer 中维护另一份构造器。第三阶段引入
`LayoutStructuredContentModel`，统一编辑 Combo/List/Tile item、完整 Tree source、TabStrip/
TabControl page 与 RichText run；它提供 add/remove/move/rename 及 Tree add-child、RichText style
适配，并由 Session 统一生成命令 snapshot。Tree Serializer 读取完整 source 而不是只读取展开后的
visible rows；TabControl reorder 在控件层保持 owned page、当前选择和 mount 状态。旧 `|` 输入只作为
简单类型兼容入口，不再承担结构化模型的主编辑路径。

Image authoring 把“资源身份”和“预览句柄”分开：`setTexturePathPicker()` 返回写入 `textureName` 的
名字，`setTexturePreviewLoader()` 由当前宿主把该名字解析为临时 `ImageTextureHandle`。独立工具通过
AYRenderer 上传预览，AYEditor 子窗口通过 GDI DIB/AlphaBlend 预览；后端句柄不进入 undo snapshot
或 JSON。宿主应返回资源系统可解析的相对路径/资源键，绝对文件路径只适合本机草稿。

`LayoutResourceCatalog` 是纯 authoring 数据模型，以稳定 key 去重、排序并执行大小写无关搜索；
`TextureResourceProvider` 由宿主枚举 Project/Assets、EngineAssets 或其他资源索引。Inspector 资源浏览器
只把选中 key 写入 Image，预览句柄仍由 `setTexturePreviewLoader()` 临时生成；空目录、missing key 和
失效 preview 都显示明确状态。目录扫描、导入和资源 GUID 不下沉到 AYUI。

`LayoutPreviewModel` 把设备预览定义为 physical width/height、DPI scale 与 physical safe-area inset，
统一换算为 Canvas 使用的逻辑 DIP。内置 Document/Desktop/HiDPI/Phone/Tablet preset 与自定义设置
共用该模型；Safe Area 由 editor-only overlay 表示。预览 root extent 是 transient host state，
`captureSnapshot()` 和保存会短暂恢复 authored root size，完成序列化后重新应用预览，因此设备切换
不会制造 dirty、undo entry 或改写布局文件。预览覆盖启用时 root resize handles 暂停，避免用户把
设备尺寸误当成文档固有尺寸。

Session 明确区分 `Edit` 与 `Interact`。进入 Interact 前捕获文档 snapshot，并恢复 attach 时记录的
enabled/read-only 状态；此时 Session 不截获画布鼠标，运行时 Widget 接收正常 UIManager 输入，所有
选择 chrome 隐藏。退出时从 snapshot 重建文档并恢复 Edit 冻结状态，保证按钮 toggle、文本输入、
Tab/列表选择及 controller 副作用不会污染 authoring 文档。F6、View 菜单和 Canvas header 使用同一
状态切换 API；该模式不是游戏脚本沙箱，也不执行 JSON 中不存在的任意代码。

Inspector 根据选中类型暴露 `controller` 和有效事件字段。Session 只编辑名字，不持有游戏 controller
对象；运行时由 UILayoutLoader 使用 8.1 的三层优先级绑定。这样 Designer 可以完成交互契约创作，
同时维持“JSON 不执行任意脚本”的安全边界。

第四阶段 Authoring Quality 把资源解析、样式检查和文档诊断继续拆成独立模型。纹理目录项同时保存
可序列化 `key` 与只在当前宿主有效的 `previewPath`；Image 始终保留 key，即使 decoder 返回以绝对
路径命名的句柄，也会在装入前恢复稳定身份。文件选择器返回的路径若能反查目录项会自动归一为 key，
无法归一的旧绝对路径仍可打开，但会以 missing resource 诊断显式暴露。standalone 使用
`Assets/`、`Engine/CoreTextures/`，AYEditor 使用 `Assets/`、`EngineAssets/` 命名空间；目录扫描和
真实路径仍由宿主负责。

`LayoutStyleInspectorModel` 区分 default、missing、style sheet、继承 token override 和本地 token
override，Inspector 可预览 Normal/Hovered/Pressed/Disabled 的声明式背景色并一键恢复默认 Style。
多选 Inspector 对不一致的 X/Y/Width/Height 显示混合值，数值和 Style 提交对当前选择集执行一次
事务；ID 仍只允许单选修改，避免隐式生成名称。

`LayoutValidationModel` 只消费 Session 导出的 authored widget 集，不递归猜测控件内部结构，因此
ListView/TileView 虚拟 cell、Tab/Modal 私有组合节点和 editor overlay 不会产生误报。当前诊断覆盖
空/非法/重复 ID、非有限/非正几何、缺失 Style/纹理、事件缺 Controller、交互控件无可访问名称、
非法 Anchor、自由控件越过父级以及 Grid span 重叠。诊断列表和 View/Validate Layout 共用同一结果，
点击诊断通过运行时 Widget 引用定位；引用仅存在于 authoring session，不进入 JSON。Controller/handler
是否真实存在仍需未来由宿主注册表提供解析结果，AYUI 不依赖游戏反射系统。

画布选择装饰仅绘制透明、像素对齐的单层 outline 与 handles，不能用半透明填充覆盖控件；后端
必须跳过 alpha=0 的矩形。选择或 Hierarchy 切换后，Session 在同一输入事务内同步属性 section
显隐并执行一次 invalidate/layout，保证 Inspector 不会短暂保留上一类型的行结构。VBox 的可伸缩
子区只声明主轴 `h=0`，不同时固定 `w/h`；这是 `UILayoutLoader` 的 layout-managed 契约。属性
面板以 row container 为显隐单位，旧的无 row 资源仍通过 label/control fallback 兼容。

Document root 是固定的 authoring origin：画布拖动、方向键、X/Y 属性和排列命令都不能改变其
位置，Ctrl+滚轮缩放也保持根的左上锚点不变。普通 free-position Widget 使用方向键做 1px 微调，
Shift+方向键使用当前 grid step；方向键微调不再被 Snap 立即吸回原网格点。固定的是 root origin，
不是 preview extent：root 仍显示 Width/Height，并提供右边、下边和右下角 resize handle，以便直接
验证 responsive anchor。Inspector 在主选中项变化时归零自身 scroll offset，防止上一类型较长的
属性表把较短的 root 属性区整体滚出 viewport。

画布视图状态由非文档节点 `LayoutCanvasViewport` 持有。Pan/Zoom 通过 backend transform 和逆向
输入坐标映射作用于 authored subtree，不再缩放或平移 Widget 的 position/size，也不进入 dirty、
undo snapshot 或 `.ui.json`；缩放 pivot 下的文档点保持不动。AYRenderer 与 AYEditor GDI backend
都实现平衡的 transform stack，frame 开始时会恢复调用方遗留的未配对状态。选择框、resize handle、
palette drop 和 marquee 统一经过 document/screen 映射，避免显示坐标与编辑坐标分叉。

第五阶段 Reuse & Responsive 保持“编辑器创作能力与运行时求值分离”。`LayoutReuseLibrary` 保存命名
Widget 子树 JSON，插入时立即展开并重新生成整棵子树 ID；实例不是共享引用，因此修改、撤销和运行时
加载均沿用现有 Widget 模型。只有存在定义时文档才升级为 `{format, version, reusable, root}` 信封，
旧 root-only 文件保持字节结构兼容；生产 Loader 忽略 `reusable` 并只构建 `root`，模板管理不进入
AYUI runtime 的对象生命周期。

响应式布局采用父级逻辑宽度查询，而不是全局窗口查询，使嵌套工具面板和 RenderTarget 内 UI 也能
独立响应。规则区间使用 `[minWidth, maxWidth)`，`maxWidth <= 0` 表示无上界；第一个匹配规则可覆盖
effective visibility，并可为非 layout-managed Widget 替换 AnchorLayout。authored visibility 和基础
Anchor 始终保留，离开断点后无损恢复。VBox/HBox/Grid 等结构布局继续决定子项几何，响应式规则只以
可见性影响占位；这样不会形成 Anchor 与 slot/cell 的双重布局权。Designer 先固定 Compact/Medium/Wide
三个产品断点，preview width 属于 transient host state，不进入 snapshot 或布局 JSON；规则编辑、block
定义/插入/删除均进入命令栈。Validation 对无效区间、区间重叠和无基础 Anchor 的覆盖给出诊断。

第六阶段 Animation Authoring 把动画作为文档级声明数据加入同一信封，而不是把回调或 Widget 指针
序列化。`UIAnimationLibrary` 用稳定 ID 记录 clip、opacity/position/size 轨道、排序关键帧、曲线和
repeat/yoyo/importance；`UILayoutLoader` 构建完 Widget ID 索引后再生成引用本次树的
`AnimationTimeline`。Designer 的结构化面板负责新建 clip、绑定当前单选 Widget、捕获当前属性值和
确定性 scrub。`AnimationTimeline::seek()` 只采样第一轮且不改变 Idle/Running/Paused 状态；Session
在首次 scrub 前保存轨道涉及的 authored 属性，后续 scrub 先还原这些属性，Reset 后也恢复基线，因此
预览不会重建 Widget 树、累积误差或污染保存结果。Widget ID 重命名通过 library 预检后原子重定向
全部相关轨道，避免编辑
身份字段后静默断链。Validation 对缺失目标、空/单关键帧以及 Anchor/容器拥有的几何轨道
给出诊断。事件触发图不混入这一数据闭环。

第七阶段把结构化动画数据接入图形时间轴。Play/Pause/Stop/Loop 与 scrub 共用同一
`AnimationTimeline` 求值路径，并由宿主真实帧时钟推进；时间轴提供标尺 seek、关键帧拖动、滚轮缩放、
中键平移、纵向轨道滚动和选中曲线预览。关键帧移动保持时间排序并合并同时间碰撞，完整拖动只生成一个
Animation 撤销事务；窗口失焦等 capture cancel 会回滚完整事务而不提交伪造坐标。移动过程中只增量同步
被修改轨道和属性预览，释放后再刷新 Clip/Track/Key 编辑列表，避免交互热路径反复复制全部轨道或重建
列表项。预览时钟不截断宿主已流逝时间，播放期间切换 Loop 会立即更新活动 Timeline。时间轴是
authoring-only Widget，不进入布局 JSON 或运行时 WidgetFactory。

编辑可靠性以文档事务为边界：ID 在提交前校验格式、保留前缀和全树唯一性，切换选中项会结束正在
合并的属性事务；Duplicate 直接复制当前 selection snapshot，不改写系统剪贴板，Paste 每次优先读取
系统剪贴板，避免复用过期的内部 payload。Image 的 `textureName` 在打开、恢复和粘贴后通过宿主
preview loader 重新生成运行时句柄。文件保存先写同目录唯一临时文件并 flush，再执行原子替换，失败
时保留旧文件并清理临时文件。

Outline 展示 authored semantic tree，不暴露 List/Tile/Tree 的虚拟 cell 或 Tab/Modal 的实现 chrome。
ScrollView content、TabControl page、Modal content/body 是固定结构槽：drop into 会重定向到当前
content/page host，槽根本身不能作为普通 sibling 被拖离。这个限制保证控件内部内容别名、所有权边和
Serializer 的结构化 payload 始终与画布树一致；未来若提供 Tab page reorder/remove，应走专用模型
命令而不是通用 `removeChild()`。创建尚未挂载的复合控件时，内部 page/content ID 也必须与文档树和
该 detached subtree 同时查重，不能等挂载后再依赖 Outline 扫描。

## 10. 动画与时间推进

动画由 `UIManager::update(dt)` 驱动：

- Widget opacity/position tween
- InteractiveWidget 颜色状态过渡
- Menu/ComboBox popup 淡入、位移和延迟销毁淡出
- TabStrip indicator tween
- Spinner phase 与 ProgressBar indeterminate
- ScrollView/ListView/TreeView/Menu 等滚动惯性

`tick()` override 必须先保持基类级联，再推进自身状态。动画中的 Widget 每帧标脏并重建自己的
display-list；完成后停止无意义的命令重建。普通路径回到稳定 replay；启用 root Layer 后则进一步
回到单次像素层 composite，但 bgfx 可见内容仍按后端契约每帧提交。

2026-08-31 tick 级联复扫已修复 ComboBox 与 SplitterHandle 漏调直接基类的问题，并增加本体
opacity 回归测试。动画产品化层随后完成：

- `AnimationSettings` 统一控制 duration scale 与 reduced-motion；倍率 `2` 表示动画耗时变为两倍，
  `0` 表示有限 tween 立即完成。reduced-motion 默认关闭，开启后 `Decorative` 动画立即收尾，显式
  标记为 `Essential` 的反馈仍播放并服从倍率。Widget tween、Timeline、Spinner、ProgressBar、
  TabStrip、InteractiveWidget 状态颜色和滚动惯性共享这套策略。
- `AnimationOptions` 为 Widget opacity/position 提供完成与取消回调、curve 和 importance；Widget
  同时提供 pause/resume/cancel，取消可选择保持当前值或 snap 到终点。直接 retarget 会取消旧播放。
- `AnimationTimeline` 在同一时钟上采样 float/FVector2/FVector4 多轨 keyframe；每段使用目标
  keyframe 的 curve。时间线支持有限 repeat、`RepeatForever` 和逐轮反向的 yoyo；当前轮次从累计
  播放时钟直接求值，因此单帧跨越多个周期时不依赖逐周期循环，也不会丢失时间。reduced-motion
  对有限循环落到其真实最终方向，永久装饰循环落到作者结束姿态后完成。
- `AnimationTimeline::seek()` 提供不改变播放状态的确定性第一轮采样，用于 authoring scrub 和测试；
  文档级 `UIAnimationLibrary` 可从稳定 Widget ID 创建同一套 Timeline，不复制运行时插值实现。
- `SpringParameters` 为 Timeline 的目标 keyframe 提供 mass/stiffness/damping/initialVelocity 二阶
  响应和可选 overshoot clamp。参数只影响该段 Timeline；原有轻量 `AnimationCurve::Spring`、Widget
  tween 与 renderer handle 的点一致兼容路径保持不变。
- `AnimationSequence::append().then()` 串行播放 Timeline，并提供序列级完成/取消与 pause/resume；
  一帧越过步骤终点时，未消费的 `dt` 会继续推进后续步骤，避免低帧率下序列被人为拉长。
- Style JSON 支持 `states.normal/hovered/pressed/disabled.backgroundColor`（以及紧凑的
  `stateColors` 写法）和 `transition.backgroundColor.{durationMs,curve}`。Button、CheckBox、
  RadioButton 使用声明式状态色；Slider/MenuItem 等既有状态颜色继续复用同一颜色 tween。

时间线和 Widget 动画都位于 CPU/UI 语义层，不依赖 `IRenderBackend::AnimationHandle`。后端句柄的
默认空实现仍不构成生产调度器承诺；这样 retained display-list、即时兜底和 Production Layer 不需要
分别维护动画状态。

## 11. 公共 API 与兼容性

- 命名空间统一为 `ayt::ui`。
- `include/AYUI/Event.h` 中旧的 `ayui::events` 仅保留兼容，不用于新代码。
- 公共渲染枚举位于 namespace scope，避免把后端实现细节嵌入接口类。
- 新控件优先复用 InteractiveWidget、FocusableWidget、SelectableWidget、ScrollableWidget 等现有契约。
- Setter 必须保持幂等；可见/布局状态变化需要正确 dirty/cache invalidation。
- 删除或改名公共类型前，应先提供迁移路径；JSON `type` 名同样视为兼容面。
- 兼容性承诺以 1.1 节为准；公共虚函数或 public struct 变化即使保持源码兼容，也可能破坏 ABI。

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
和控件/图片叠加，Animation 页包含可重放的物理弹簧 + 四轮 yoyo Timeline，Productization 页可交互
验证 UI scale、Windows UIA 语义、父主题、Tab overflow、Unicode/Bidi 多字体 RichText 和 Clipboard。

AYRenderer 的 `UIRenderBackend` 实现 `IRenderBackend`，UIPass 在 3D pass 后合成 UI；当前支持
display-list replay、Production root UI Layer，以及 CPU path tessellation + stencil path fill/clip。
FrameGraph 与 UI Layer 通过同一个 RenderTargetPool 获得物理 FBO。MockRenderer 用于无 GPU 单测，
并实现相同 Layer 生命周期测试面，不应和生产 backend 行为产生不同的 Widget 语义。

## 13. 测试与审计基线

2026-08-29 全模块审计统计：

- 78 个公共/支持 header
- 66 个非 demo、非 unittest 的 `.cpp`
- 93 个 `Test_*.cpp`
- 1022 个 `TEST_CASE`
- Windows Debug：`4643 / 4643` 条断言通过

2026-09-01 原生 SVG、连续 contour、设备输入桥接、容器溢出修复、虚拟化 TileView、
InfoStrip/CornerMarker，以及 Timeline repeat/yoyo、物理弹簧和跨步骤时间守恒回归纳入当前
Windows Debug 基线：`5147 / 5147`；上面的 2026-08-29 数量保留为该轮审计快照。

2026-09-08 扩展 Layout Editor 工具箱、Image 选择/后端预览、controller/event 往返、生产 Loader
结构化 payload 对称性及 detached Tab page ID 唯一性回归后，Insider Windows Debug 基线为
`5322 / 5322`。同日完成 Layout Editor 可靠性第一阶段：非破坏性 CanvasViewport、ID/undo 事务、
原子保存、剪贴板优先级和 Image preview rehydrate 纳入回归，当前基线为 `5330 / 5330`；
`AYUI_LayoutEditor_RoundTrip` 同步通过并验证 Pan/Zoom 前后序列化字节一致、根尺寸 authoring、
ID 拒绝、clipboard/duplicate 边界、图片恢复和原子替换。

同日完成 Layout Editor 架构第二阶段：抽取 `AYUILayoutEditorCore`，拆分 Document/Selection/Command/
Viewport，引入 Widget authoring registry 与 schema-driven Inspector，并为类型化 edit intent 保留 JSON
恢复兜底。新增核心单测后 Insider Windows Debug 基线为 `5366 / 5366`，round-trip 同步通过；
AYEditor 全量回归为 `1940 / 1940`。

同日完成 Layout Editor 产品编辑第三阶段：结构化集合/Tree/Tab/RichText 模型、可搜索纹理资源目录、
物理分辨率/DPI/Safe Area 设备预览和 snapshot 回滚式 Interact 模式进入共享 core；Tree 折叠后代与
Tab page reorder 的 Serializer/ownership 边界纳入回归。当前 Insider Windows Debug 基线为
`5402 / 5402`，`AYUI_LayoutEditor_RoundTrip` 同步验证 transient preview 保存隔离与交互退出回滚。

同日完成 Layout Editor Authoring Quality 第四阶段：稳定资源 key 与本机 preview path 分离、样式来源/
四状态预览、多选公共几何与 Style 编辑、统一可定位 Validation 诊断进入共享 core；资源路径不会随
预览句柄泄漏进 JSON。新增核心与 round-trip 回归后 Insider Windows Debug 基线为 `5417 / 5417`。

2026-09-09 完成 Reuse & Responsive 第五阶段：文档内可复用 Widget block、实例 ID 重生成、兼容旧版
root-only JSON 的版本化文档信封，以及 Compact/Medium/Wide 可见性与 Anchor 覆盖进入共享 core。
运行时直接读取信封 root，结构化容器会在断点隐藏后重排；非法/重叠规则进入 Validation。新增运行时、
模型、持久化和 headless round-trip 回归后 Insider Windows Debug 基线为 `5476 / 5476`。

同日完成 Animation Authoring 第六阶段：文档级 clip/track/keyframe 模型、版本化信封持久化、
`UILayoutLoader` 运行时 timeline 实例化、Designer 结构化创作与 snapshot scrub、动画专用 Validation
诊断进入共享 core。新增 seek、模型、持久化、运行时绑定和 headless round-trip 回归后 Insider
Windows Debug 基线为 `5510 / 5510`。

同日完成 Animation Authoring 第七阶段：图形时间轴、连续 transport、真实帧时钟、关键帧单事务拖动、
时间轴缩放/平移和曲线预览进入共享 Designer core；预览基线收敛为属性级恢复，不再重建 Widget 树。
新增交互、运行时播放和宿主集成回归后，Insider Windows Debug 基线为 AYUI `5534 / 5534`、
AYEditor `2099 / 2099`，Layout Editor headless round-trip 同步通过。

2026-09-10 完成类型化 Inspector 第一阶段：property schema 增加编辑器类型、范围、步长与枚举选项，
并接入数值控件、Image、集合、ScrollView、TabStrip、GridPanel 和 RichText 的 25 项运行时属性；
Image tint/UV 与 Grid spacing 的 Loader/Serializer 对称性、Grid occupied-cell 缩小保护及 headless
属性/保存/重载回归同时落地。Insider Windows Debug 当前提交基线为 AYUI `5579 / 5579`，Layout Editor
headless round-trip 通过。

同日完成类型化 Inspector 第二阶段：25 个类型专用属性行改由 `PropertySchema` 的显示名和控件契约
自动生成，静态 chrome 仅保留基础与复合编辑界面。回归同时验证源 JSON 不再包含生成控件、attach 后
完整 Widget 树和按选中类型显隐均正确；源码 ABI 更新到 113，当前提交链基线为 AYUI `5593 / 5593`。

断言总数从旧基线的 7405 收敛到 4229，是因为参数矩阵、逐帧动画和压力循环不再在每次
迭代中调用 `CHECK`；循环体只累计失败数，并在循环结束后统一断言。测试文件数、测试用例
数和输入迭代次数均未减少。Retained display-list、Layer、Serializer、vector path、产品化、
设备输入、容器回归、动画产品化和 TileView 随后把当前基线增加到 `5049 / 5049`。

审计覆盖：

- 全量构建和单测退出码
- Factory 注册与 serializer 类型一致性
- JSON/i18n UTF-8 文本
- 控件生命周期和拥有/非拥有指针
- dirty/cache invalidation、retained display-list、即时兜底、frame-local backend replay、
  world-bounds cache 和容器 clip/hit-test 契约
- Layer/RenderTarget 的 DPI、resize、damage、paint/composite、release、共享池 quarantine/预算和 reset 生命周期
- DPI/UI scale 的逻辑布局、物理输入换算、语义树/动作/diff、主题继承与控件级联、Tab overflow、
  RichText wrap/justify/ellipsis/decoration/Unicode grapheme/Bidi/命中和 Serializer 往返
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
- TreeView scrollbar 改为无溢出自动隐藏，补齐 gutter 命中、滚轮/惯性、实际 row 位移和内容收缩夹紧。
- Menu popup 按 viewport 限高、翻转/夹紧，长菜单提供自动隐藏垂直 scrollbar、滚轮/惯性和键盘项滚入可视区。
- ComboBox 与 SplitterHandle 的 tick override 恢复直接基类级联，避免本体 opacity/position 动画停滞。
- 23 个测试文件中的循环内断言改为失败计数汇总；静态复扫结果为 0 个循环内 `CHECK`。
- AYUI 单测试采用单 translation unit include 模式；CMake 显式声明全部 `Test_*.cpp` 为
  `main.cpp` 的对象依赖，防止 MSVC/Ninja 漏记 include 后运行陈旧测试二进制。
- TreeView 千节点性能门槛按测试名统一为单次平均 `< 5ms`，不再误用 50 次总耗时 `< 100ms`。
- 独立 Factory 补齐 Dimmer/Modal/ModalDialog/TabStrip 注册。
- ScrollView 运行时 scrollbar enable/disable、Box 自然尺寸缓存和多个视觉 setter 的 invalidation。
- 公共聚合头和 CMake header 清单补齐。
- Widget-local retained display-list、复杂控件 replay 顺序、祖先几何级联失效与显式即时兜底。
- RenderTarget/UI Layer 接口、MockRenderer 契约与 AYRenderer bgfx 实现；root 主树 opt-in 像素保留，
  clean frame 单 composite，overlay 即时叠加，失败同帧回退。
- renderer-wide RenderTargetPool 统一 FrameGraph/UI Layer 物理 FBO，使用 generation lease、两帧
  quarantine、精确键复用、LRU 预算回收和 reset 失效。
- Widget 显式 dirty rect 在祖先链上以最多 8 个 region 传播；Production root/subtree Layer 对每个
  damage 做透明覆盖清除和 clip replay，70% 面积阈值、region 溢出、无范围 dirty 与 backend/device
  失效确定性退化为全量重绘。
- Widget 子树支持 `Always`/`Auto` Layer 策略；Auto 由稳定帧、面积、display-command 数和连续失效帧
  驱动晋升/降级，不复制控件绘制实现。
- RenderTargetPool 对 FrameGraph 保留 soft budget，对 UI 使用 strict budget；压力下撤销 LRU backing、
  保持逻辑 Layer handle、同帧 immediate fallback，并通过统一统计暴露命中、重绘面积和降级。
- UIManager 的 Production Layer 状态放在 out-of-line sidecar，不改变既有对象布局；ActiveScope
  不会在 shutdown 或 host 显式切换 active slot 后恢复陈旧上下文。
- 41 个注册 Widget 的 serializer type/字段往返，Grid cell、TileView 和复合控件结构化 payload。
- backend-independent retained path recipe；AYRenderer 凹多边形/曲线 tessellation、winding 孔洞、
  miter stroke、嵌套 stencil path clip 和排序屏障。
- 逻辑 DIP/物理 framebuffer 分离、无障碍语义 snapshot/action、Theme 与 Widget token 继承、
  TabStrip 三种 overflow、RichText paragraph layout 和 POSIX Clipboard helper backend。
- Unicode grapheme/UAX #14-compatible break/Bidi line reorder，AYFont UTF-16/UTF-32 HarfBuzz direction
  与 language，AYRenderer family/weight/italic face、多 atlas 及 shaping-cluster 一致测量/断行/绘制。
- Windows UI Automation Fragment provider、常用 control pattern、增量事件与跨线程动作封送；Gallery
  在 `WM_GETOBJECT`、layout 后 update 和 shutdown 顺序上提供生产接入样例。

回归测试失败必须让进程返回非零；不得通过 batch wrapper 抹掉退出码。

合批视觉回归由 `demo/RunBatchVisualRegression.ps1` 驱动。它固定 Gallery 的时间步、页面、
交互动作和截图帧，在独立隐藏进程中运行两种 batch mode；当前十条路径均为字节级一致，
draw call 从保守路径的 60–94 次降至 23–41 次。这个结果锁定的是当前 Gallery 复杂控件路径，
不应被解释为所有未来自定义控件都会得到相同降幅。

## 14. 已知限制与后续工作

按优先级记录剩余边界：

1. Production UI Layer 已完成多 damage region、root/Always/Auto 子树分层、strict budget/LRU 压力
   降级、统计反馈及 D3D11/D3D12/Vulkan/OpenGL 的 36-capture 矩阵。下一步是基于历史 repaint cost
   和命中率自适应 Auto 阈值、按 Layer 类别分预算，以及 filtered/backdrop Layer；不能让策略判断
   进入每个控件的业务实现。
2. vector path 补充 self-intersection/fill-rule、布尔组合和独立 AA fringe；continuous contour 及
   butt/round/square cap、miter/round/bevel join 已落地。原生 SVG 当前只承诺上文图标子集，
   transform/CSS/clip/mask/filter 与非 path 图元仍显式拒绝，不隐式承诺任意 SVG 语义。
3. Windows UI Automation provider 已完成第一阶段；后续补 TextPattern/selection range/live region，
   并以同一 snapshot/action/diff 状态机实现 AT-SPI 与 NSAccessibility，不能在平台层重新推断控件语义。
4. RichText 的 grapheme/bidi/UAX #14-compatible break、family/weight face 与真实 shaping 已完成第一阶段；
   下一层是内联对象、跨字体 glyph fallback、可变/彩色字体、完整 selection/editing，以及非 Windows
   字体发现。portable line-break fallback 是确定性子集，完整 Unicode 表应由平台或生成数据提供。
5. 将目前自动即时兜底的粒子和资源引用逐类评估为可安全保留的 typed command；不能保证
   句柄生命周期的操作继续保留为排序/缓存屏障。
6. 可选：统一散落在 loader、serializer、IME 和 i18n 中的 UTF-8 工具为一个经过测试的公共内部组件。
7. 动画产品化已完成状态化 Style transition、多轨 timeline/keyframe、完成/取消/暂停/串联 API、
   全局 animation scale、reduced-motion、repeat/yoyo、物理弹簧参数与跨 Sequence 步骤的时间守恒；
   文档级 clip/track/keyframe、Designer 结构化创作、图形时间轴、连续 transport、关键帧拖动和曲线
   预览也已闭环。后续是 OS reduced-motion 偏好自动桥接、可编辑 Bezier/物理参数与事件触发图，
   不要求修改 Widget tween 或另建一套动画求值路径。

这些限制不阻塞当前 v1.6 功能，但实现新特性时不得继续扩大重复路径。

### 14.1 高价值扩展顺序

后续扩展按依赖关系和收益排序，不以增加控件数量为优先目标：

1. **Retained display-list（第一阶段完成）**：dirty Widget 只重建自己的本地高层绘制指令，
   clean Widget 每帧按 z-order replay；不缓存 bgfx transient buffer 或跨帧 `UiItem`。旧即时路径
   保留为显式策略和不安全命令的自动兜底。下一步是降低 `std::function` 存储开销、增加缓存
   内存统计/预算，并逐类扩展 typed command，而不是复制第二套 Widget 渲染器。
2. **UI Layer / RenderTarget（Production 第三阶段完成）**：接口、Mock 和 AYRenderer bgfx
   实现已经闭环；FrameGraph/UI Layer 共用 generation-safe RenderTargetPool。UIManager 可 opt-in
   root 像素层，Widget 可选择 Always/Auto 子树层；最多 8 个 damage region 独立 repaint，70% 阈值
   full fallback，overlay 即时叠加，失败同帧回退；每帧 224 次离屏 pass 的边界可确定复现并跨帧恢复。
   UI strict budget 会撤销 LRU backing 而不销毁逻辑 handle，并暴露 cache/pool 统计。四个生产后端的
   36-capture 图像矩阵已验证
   immediate/full/clean/partial、透明/opacity/blend 和完整生命周期；clean reuse、isolated blend 与
   Preserve 字节精确，通常最多差 1 LSB，RGBA8 group opacity 因双重量化最多 2 LSB。下一阶段是
   adaptive cost model、分级预算；滤镜、背景模糊、多 viewport 以及未来
   `UIPlane` / 世界空间 UI 均建立在该能力之上。
3. **Serializer 完整化（完成）**：41 个公共注册类型均有 type 决策；Grid cell、ScrollView、TileView、
   Menu/StatusBar、Tab、Modal 和 Dock 复合结构具有专用 wire contract 与往返测试。运行时瞬态明确排除。
4. **高级裁剪和矢量路径（图标 SVG 阶段完成）**：DisplayList 保留 backend-independent path
   recipe；AYRenderer 共享一套 tessellation/submit 路径实现凹多边形、曲线、连续 contour、
   cap/join stroke、winding hole 与嵌套 stencil clip。`SvgDocument`/`SvgIcon`/Button icon slot
   直接消费安全 SVG path 子集。path fill/clip 是显式排序屏障，两种 batch mode 不复制实现。
   下一阶段是完整 fill-rule/boolean、transform/CSS/clip 语义和 AA fringe。
5. **产品化能力（第二阶段完成）**：Widget 使用逻辑 DIP，DPI/UI scale 在输入和最终 raster
   边界闭环；无障碍 snapshot/action、Theme/Widget 两级继承、TabStrip Scroll/Compress/Clip、
   RichText paragraph layout 和 Win32/macOS/Wayland/X11 Clipboard 均有 API、Serializer、Gallery
   或单测覆盖；本阶段新增 Windows UIA provider、Unicode grapheme/Bidi/断行、HarfBuzz direction/
   language 与 family/weight 多 face atlas。下一阶段沿现有 adapter/text/backend 契约补平台 provider、
   TextPattern、字体 fallback 和 inline object，不复制核心路径。

`OrderedRuns` 与 `OverlapAware` 只允许在提交顺序规划上分叉；图元记录、合批兼容键、
顶点/索引构建、shader 和 submit 必须共享。新图元若不能安全重排，应进入统一命令流并声明
排序屏障，而不是在两种 batch mode 中各实现一次。

### 14.2 通用颜色创作控件（2026-09-08）

`ColorPicker` 是 AYUI 的通用颜色创作入口，统一提供 HSV 饱和度/明度面、色相与透明度条、
`#RRGGBB` / `#RRGGBBAA` 输入、命名色组、记忆/替换色块和拾色请求回调。控件只管理交互
状态；屏幕/画布像素如何采样由宿主通过回调决定，色组的跨会话保存也由宿主选择项目或用户
偏好存储。这样 AYUI 不依赖渲染器回读或某个编辑器的配置格式，而 AYEditor、材质工具、粒子
工具和 AY2D 可以复用同一个颜色模型与交互。控件类型、当前颜色、活动色组及色样进入
`WidgetSerializer` wire contract，内部组合控件不会作为外部 children 重复序列化。

## 15. 决策摘要

| 决策 | 结论 |
|---|---|
| UI 模式 | 保留模式 Widget tree |
| 数据格式 | 复用 JSON UILayoutLoader，不引入第二套配置系统 |
| 渲染解耦 | `IRenderBackend`，AYRenderer 提供实现 |
| 帧提交 | 可见 Widget 每帧遍历；dirty 重建本地 display-list，clean replay，bgfx 仍逐帧 submit |
| 像素层缓存 | Production root + Always/Auto subtree Layer；最多 8 个 damage region；FrameGraph/UI 共用 RenderTargetPool，UI strict budget/LRU 降级；clean frame 单 composite |
| 矢量路径 | retained recipe；AYRenderer CPU tessellation + stencil fill/clip；原生安全 SVG 图标子集；复杂 path 是合批排序屏障 |
| DPI/UI scale | Widget/damage/accessibility 使用 DIP；宿主输入与 framebuffer 使用物理像素；后端最终缩放 |
| 无障碍 | AYUI 输出 snapshot/action/diff；Windows adapter 发布原生 UIA，AT-SPI/NSAccessibility 待接入 |
| 主题 | 命名 parent theme + 控件树 token override 级联，parent-first / nearest-wins |
| Tab/RichText | Tab 三种 overflow；RichText 按 grapheme/Bidi/UAX break 排版并复用 backend shaping cluster |
| Clipboard | Win32 原生；macOS/Wayland/X11 helper backend，UTF-8、失败显式返回 |
| 文本编码 | 文件/JSON UTF-8，Widget 文本 `std::wstring` |
| 事件 | Widget 内部冒泡；宿主回调用 id + bindEvent |
| Popup | UIManager overlay 集中管理 |
| 颜色创作 | 通用 ColorPicker；HSV/Hex/命名色组归控件，像素采样和持久化归宿主 |
| Dock tree | VBox/HBox/Splitter 直接作为 Widget tree |
| 销毁 | `destroyWidgetTree` + 明确 owned/external API |
| 线程 | UI 树单线程修改 |
| 历史文档 | `AYUI-v1-Design.md` 只读保留 |

## 16. 相关文档

- [README.md](README.md)
- [AYUI-v1-Design.md](AYUI-v1-Design.md)
- [AYRenderer README](../AYRenderer/README.md)
- [AYEntity design](../AYEntity/design.md)
