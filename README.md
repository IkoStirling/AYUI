# AYUI

AYUI 是 AliyatEngine 的保留模式（retained-mode）2D UI 模块，覆盖控件树、布局、输入与焦点、JSON 布局、主题/i18n、弹层、Docking、动画，以及渲染后端抽象。

- CMake 目标版本：`1.0.0`
- 当前功能里程碑：v1.6 已实现
- 最近全模块审计：2026-08-29
- 权威架构文档：[design.md](design.md)
- 变更记录：[CHANGELOG.md](CHANGELOG.md)
- 历史方案：[AYUI-v1-Design.md](AYUI-v1-Design.md)（仅供追溯，不代表当前实现）

## 当前状态

AYUI 已接入根工程，`CMakeLists.txt` 会加入 `AYRuntime/AYUI`。AYRenderer 提供 `UIRenderBackend` 实现，AYUI 本身只依赖 `IRenderBackend`。

已实现的主要能力：

- Widget 树、命中测试、事件冒泡、焦点、鼠标捕获、拖放和多窗口 `UIManager`
- VBox/HBox、GridPanel、ScrollView、Splitter 与约束辅助
- Button、输入框、列表、树、菜单、工具栏、状态栏、Tab、Modal、Tooltip 等控件
- DockArea/DockCard/DockOverlay、嵌套 dock tree、浮动卡片与布局持久化
- JSON 布局加载、WidgetFactory、WidgetSerializer、文件热重载
- 40 个注册类型的 Serializer wire contract，含 Grid cell、复合内容、Menu/Dock/Modal 专用结构
- StyleSheet/Theme、控件级 token override、I18n、UTF-8 文本往返
- 逻辑 DIP 坐标、独立 DPI/UI scale、物理输入换算与按缩放倍率栅格化字体
- 平台无关无障碍语义树、稳定节点 ID、角色/状态/动作推断、增量 diff 及 Serializer 元数据
- Windows UI Automation 原生 Fragment provider，覆盖 Invoke/Toggle/RangeValue/ExpandCollapse/SelectionItem
- Theme 命名父级继承和控件树 token override 级联
- TabStrip Scroll/Compress/Clip overflow；RichText 字素簇、双向文本、Unicode 断行和真实字体 shaping
- RichText run 级 font family/weight/italic/language，以及 AYRenderer 多 face、多 atlas 一致测量/绘制
- Win32、macOS、Wayland 和 X11 Clipboard 后端
- 脏标记、世界坐标缓存、颜色/透明度/位置动画和滚动惯性
- 默认启用 Widget-local retained display-list，保留即时绘制兜底
- 后端无关的 retained vector-path recipe，以及 AYRenderer 的凹多边形/曲线 tessellation、孔洞与 stencil path clip
- `IRenderBackend` 的 RenderTarget/UI Layer 生命周期、DPI、damage 与合成契约
- 可选的 Production root UI Layer：静态主树复用离屏像素，overlay/drag visual 保持即时绘制
- AYRenderer 共享 RenderTargetPool：FrameGraph 与 UI Layer 复用同一套 FBO 生命周期和预算，窗口
  resize 与运行时 MSAA 切换都会先失效租约再执行 bgfx reset
- Gallery 与独立 Layout Editor

2026-08-29 Windows Debug 基线为 `4643 / 4643` 条断言通过。旧基线中的循环内重复
`CHECK` 已改为循环累计失败数、循环结束统一判断；测试用例和输入迭代覆盖没有减少。

重要渲染契约：AYUI 现在默认保留每个 Widget 自己的高层 display-list。dirty Widget 调用
`onRender()` 重建本地命令；clean Widget 不再重跑控件绘制逻辑，而是按原 painter order 每帧向
即时后端 replay。这里保留的是矩形、文本、图片、clip 和 vector-path recipe 等后端无关命令，不是 bgfx 的
`UiItem`、transient buffer 或上一帧提交，因此 `UIRenderBackend::beginFrame()` 仍可安全清空
frame-local 数据，bgfx 也仍然每帧收到完整可见 UI。

子控件不进入父控件的 display-list，各自独立失效和 replay；父级 clip/opacity 在 replay 时应用。
path 以创建操作和绘制时快照保留，replay 时创建短生命周期后端路径，不缓存后端句柄。使用粒子、
后端资源创建/释放或 RenderTarget pass 等不能安全跨帧保留的操作时，Recorder 会自动放弃候选缓存并沿用旧即时路径。自定义控件也可显式调用
`setDisplayListPolicy(DisplayListPolicy::Immediate)` 作为诊断或兼容兜底。

需要进一步消除静态主树每帧的 display-list replay 时，可在支持 RenderTarget 的后端上显式开启
`UIManager::setRootLayerCachingEnabled(true)`。首次帧、设备失效或普通 `markDirty()` 会完整重绘透明
离屏层；显式 `markDirty(rect)` 会把最多 8 个逻辑 damage region 传播到根节点，重叠/相邻区域就地
合并，第 9 个区域退化为 union，累计面积达到 Layer 的 70% 时改走 full redraw；其余情况逐区清除和
replay；
clean 帧只提交一次 layer composite。Popup、Modal、Tooltip 和 drag ghost 始终在 composite 之后走
即时路径，因此不会被静态层吞掉。创建、resize、device reset 或 paint 失败时，同一帧自动回退到
普通即时绘制，不要求应用维护第二条 Widget 渲染路线。Gallery 已启用该模式作为生产集成样例。

局部稳定的复杂子树可使用 `Widget::setLayerCachePolicy()`：`Always` 强制建立子树 Layer，`Auto` 在
连续 3 个 clean frame、物理前逻辑面积至少 4096 且估算 display command 至少 12 条时晋升；连续
3 个 invalidated frame 自动降级并释放 backing。子树 Layer 仍调用同一个 `renderSubtreeContent()`，
失败立即回到 display-list/immediate 路径，不形成需要双线维护的控件 renderer。

`UIManager::getLayerCacheStats()` 暴露 full/partial paint、composite/cache hit、实际重绘像素面积、
分配失败/降级、pool allocation/reuse/eviction、live lease/idle target 与预算；
`setLayerCacheBudgetBytes()` 可设置 UI 严格预算，`resetLayerCacheStats()` 只重置计数不销毁缓存。

## 版本与兼容性

`project(VERSION 1.0.0)` 是 CMake 包/目标版本，`v1.6` 是当前能力里程碑；后者用于描述已经
落地的功能集合，不是第二个可独立发布的语义版本号。后续发布以 CMake 版本和
[CHANGELOG.md](CHANGELOG.md) 为准，能力里程碑仅用于设计追踪。

AYUI 在 1.x 内把 `AYUI.h` 聚合头、Widget/UIManager、内置控件、布局、Theme、Loader 和
`IRenderBackend` 的基础契约视为稳定源码 API；新增接口和 JSON 字段优先采用可选、向后兼容的
扩展。已注册的 JSON `type` 名及文档化持久字段属于兼容面，焦点、hover、拖放会话、缓存句柄等
运行时状态不属于 wire contract。

当前不承诺跨提交、跨编译器、跨构建选项的 C++ 二进制 ABI。AYUI 或公共头变化后，AYRenderer
及宿主应与其一起重新编译。高级 RenderTarget/Layer capability、平台原生无障碍适配器、
display-list 内部表示和具体渲染后端细节仍是演进接口；调用方应做 capability 检查，不能把内部
结构或后端句柄持久化。

## 快速接入

常用入口是聚合头 `AYUI.h`：

```cpp
#include "AYUI.h"

ayt::ui::UIManager ui;
ui.initialize(renderBackend);       // renderBackend implements IRenderBackend
ui.setClientSize(1280.0f, 720.0f);  // physical framebuffer pixels
ui.setDpiScale(windowDpi / 96.0f);  // OS monitor scale
ui.setUiScale(userPreference);      // independent in-app zoom
ui.setRootLayerCachingEnabled(true); // optional retained pixel layer

ui.loadLayout("assets/ui/main.ui.json");
ui.update(deltaSeconds);
ui.render();
```

只需要构建 JSON 控件树时，可以直接使用 `UILayoutLoader`：

```cpp
#include "AYUI.h"

ayt::ui::UILayoutLoader loader;
loader.bindEvent("btn_ok", "onClick", [] {
    // Handle the action in host code.
});

ayt::ui::Widget* root = loader.loadFromFile("menu.ui.json");
// The returned factory-built tree is owned by the caller.
ayt::ui::destroyWidgetTree(root);
```

JSON 中的可执行逻辑不会被反序列化；`onClick` 等字段只用于匹配宿主通过 `bindEvent` 注册的回调。

## 数据驱动约定

```json
{
  "type": "Window",
  "id": "pause_menu",
  "position": { "x": 100, "y": 100 },
  "size": { "w": 420, "h": 280 },
  "style": "panel.default",
  "text": "ui.pause.title",
  "children": [
    {
      "type": "VBox",
      "spacing": 8,
      "padding": { "left": 12, "top": 12, "right": 12, "bottom": 12 },
      "children": [
        {
          "type": "Button",
          "id": "btn_resume",
          "text": "ui.pause.resume",
          "onClick": "resume_game"
        }
      ]
    }
  ]
}
```

- 布局文件使用 `*.ui.json`。
- `text` 以 `ui.` 开头且 loader 已设置 `I18n` 时，会按当前语言解析。
- 所有 JSON 文本均按 UTF-8 处理。
- `style` 引用 StyleSheet；Theme token 可由控件局部 override。
- Theme JSON 可用 `"extends": "base-theme"` 继承 token 和 sheet；Widget 的 token override
  从父控件向后代级联，最近的 override 胜出。
- `accessibilityRole/Label/Description/Value/Hidden` 可覆盖自动推断语义，并随布局序列化。
- Dock 布局通过 `UILayoutLoader::saveLayout*` 与 `loadFrom*` 持久化。

`WidgetFactory` 是类型名到构造器的唯一注册点。内置控件由模块自动注册；宿主扩展控件可调用 `registerCreator` 或使用 `REGISTER_WIDGET`。

## 产品化接口

AYUI 的 Widget 几何统一使用 DIP。`setClientSize()` 接收物理像素，逻辑视口为
`physical / (dpiScale * uiScale)`；鼠标、滚轮和触摸入口同样接收物理坐标并在分发前换算。
宿主应在窗口跨屏或收到 DPI change 时更新 `setDpiScale()`，用户缩放偏好只更新
`setUiScale()`。`UIRenderBackend` 在最终顶点、SDF 参数和字体栅格尺寸处应用有效倍率，
scale=1 与旧行为一致。

`buildAccessibilityTree()` 返回平台无关快照，`performAccessibilityAction()` 用节点 ID 把
Press/Toggle/Select/Expand/RangeValue 等动作路由回 Widget。Windows 宿主可直接创建随模块提供的
UI Automation adapter；它发布 Fragment tree、屏幕 bounds、焦点/结构/属性事件，并把 UIA 工作线程
发起的动作封送回窗口 UI 线程：

```cpp
auto accessibility = ayt::ui::createNativeAccessibilityAdapter(ui, hwnd);

// WndProc 中应先交给 adapter；handled 时直接返回 result。
intptr_t result = 0;
if (accessibility->handleNativeMessage(message, wParam, lParam, result))
    return static_cast<LRESULT>(result);

// 每帧 update/layout 之后同步一次语义增量。
accessibility->update();
```

adapter 必须在 `UIManager::shutdown()` 前销毁。非 Windows 构建仍可使用同一对象取得 snapshot/diff；
AT-SPI 与 NSAccessibility 的原生发布端尚未接入。

`TabStrip` 默认使用水平 Scroll overflow，滚轮可移动视口且选中项自动进入可见区；
Compress 会从 `minTabWidth` 向 24 DIP 的交互硬下限压缩，Clip 保留旧策略。`RichText` 支持 Word/Character
换行、显式段落、四种水平对齐、三种垂直对齐、行高/行距、最大行数与省略号；Unicode 分析把扩展
字素簇作为最小 caret/换行单元，按双向 level 重排每行，并把 run 的 family/weight/italic/language/
direction 连同颜色、装饰、字距和 baseline shift 交给后端。AYRenderer 使用 HarfBuzz 的同一结果完成
测量、cluster 几何和绘制，字距只在 shaping cluster 结束处应用一次。

## 所有权规则

- `UILayoutLoader` / `WidgetSerializer` 返回的根树由调用方负责，使用 `destroyWidgetTree` 销毁。
- 普通 `addChild` 将节点纳入 `destroyWidgetTree` 的递归销毁范围；`Widget` 析构本身不删除 children。`addChildExternal` 用于宿主/栈对象持有的外部生命周期场景。
- Popup 由 `UIManager` 的 overlay 管理；关闭路径可能带淡出动画，关闭后不要继续解引用已销毁的 popup。
- `DockCard::setContent` 接管 content；`Modal::setDimmerOwned` 接管 dimmer，`setDimmer` 不接管。
- 模块是 UI 线程模型；不要从后台线程直接修改 Widget 树。

完整不变量见 [design.md](design.md#5-生命周期与所有权)。

## 图片与控件组合

`Image` 支持纯色回退、命名/匿名纹理句柄、UV 裁剪和整体透明度。控件叠图有两种常用结构：把 `Image` 作为 `Button` 的装饰子节点（按钮本身仍接收点击），或在 `Panel` 中先放背景 `Image`、再放文本和交互控件；同级子节点按插入顺序绘制，后加入者位于上层。

`AYUI_Gallery` 的 **Images** 页面提供共享纹理、UV crop、图片按钮、半透明图片层和图片背景上可点击控件的实机示例。**Backend** 页面还覆盖凹路径、描边、顺/逆时针孔洞、Bezier 和带孔 stencil 裁剪。示例纹理由 Gallery 运行时生成，不依赖外部图片文件。

## 构建与验证

在已配置的根工程 build 目录中：

```bat
cmake --build <build-dir> --target AYUI_UnitTests
<build-dir>\AYRuntime\AYUI\unittest\AYUI_UnitTests.exe

cmake --build <build-dir> --target AYUI_Gallery
cmake --build <build-dir> --target AYUI_LayoutEditor
```

测试程序在任一断言失败时返回非零退出码，可直接用于 CI。

需要验证生产 GPU 后端的 UI 合批绘制顺序时，可让 Gallery 在固定帧分别使用保守的
`OrderedRuns` 和默认的 `OverlapAware` 路径截图，并进行逐字节比较：

```powershell
& .\demo\RunBatchVisualRegression.ps1 `
    -GalleryExe <build-dir>\AYRuntime\AYUI\demo\AYUI_Gallery.exe `
    -OutputDir <output-dir>
```

脚本覆盖十条 Gallery 路径（包括图片叠加、输入、列表、布局、渐变、动画、产品化页以及带 dimmer
的 modal），要求两种模式的 1280×720 GPU 输出完全一致，同时要求优化路径的 draw call
不高于保守路径。截图和指标写入指定输出目录，不进入源码树。

Production root Layer 另有真实纹理和局部 damage 门禁：

```powershell
& .\demo\RunLayerVisualRegression.ps1 `
    -GalleryExe <build-dir>\AYRuntime\AYUI\demo\AYUI_Gallery.exe `
    -Backend d3d11 `
    -OutputDir <output-dir>
```

跨后端门禁可一次运行 D3D12、Vulkan、OpenGL（也可通过 `-Backends` 加入 D3D11）：

```powershell
& .\demo\RunLayerVisualRegressionMatrix.ps1 `
    -GalleryExe <build-dir>\AYRuntime\AYUI\demo\AYUI_Gallery.exe `
    -OutputRoot <output-root>
```

脚本运行 36 次独立 GPU capture。原复杂控件矩阵在 1.0×/1.5× 下生成即时、full、clean 和 partial
参考，两档保持 1280×720 DIP，framebuffer 为 1280×720/1920×1080，并覆盖非对称 checker、alpha
sprite、atlas、渐变、文本、边框及 stencil path clip。独立的透明矩阵在非对称主画布上验证透明
Layer、嵌套 opacity 与 Additive/Multiply/Screen 隔离组；生命周期矩阵验证 framebuffer resize、动态
DPI、device reset、MSAA reset 后的 pool lease 恢复，以及 Transparent/Color/Preserve 局部 clear。
除 RGBA8 group opacity 的双重量化上限为 2 LSB 外，其余即时语义对照最多 1 LSB；isolated blend、
Preserve 和 clean retained reuse 字节完全一致。D3D11、D3D12、Vulkan、OpenGL 已通过同一矩阵；各
后端 root 基线均为 immediate 30、full Layer 31、clean Layer 1、partial Layer 11 次 UI draw call。
OpenGL RenderTarget 读取按 `originBottomLeft` 翻转 V；point-sampled glyph quad 吸附物理像素网格，保证
默认 framebuffer 与 FBO 在 1.0×/1.5× 下使用一致覆盖。

## 目录

- `include/AYUI/`：公共 API
- `interface/AYUI/IRenderBackend.h`：渲染后端契约
- `Controls/`：控件、UIManager、Docking
- `Layout/`：Box、Grid、Constraint、Splitter
- `Loader/`：JSON loader、factory、serializer
- `Style/`：style/theme 与 MockRenderer
- `i18n/`：语言表
- `unittest/`：模块回归测试
- `demo/`：Gallery 与 Layout Editor

## 已知边界

- Production root UI Layer 已接入 AYRenderer，并与 FrameGraph 共用 renderer-wide RenderTargetPool。
  clean 主树稳定为一次 composite submit；resize、DPI、device reset 或无范围 dirty 会触发全量重绘；
  显式 dirty rect 保留最多 8 个 damage region，并逐区透明覆盖清除、clip 和局部 replay，区域外像素
  继续保留；累计面积达到 70% 或 region 溢出时确定性退化为 union/full redraw。Widget 子树可选择
  `Always`/`Auto` Layer，root、子树和 immediate 共用同一绘制实现。
  overlay/drag visual 仍即时绘制。离屏绘制使用 view 26–249，单个 UI 帧最多 224 次 layer paint；
  第 225 次或任一步失败会在同帧回退即时路径，下一帧 view 调度自动恢复。
- RenderTargetPool 当前只接受精确尺寸/格式/深度/采样方式键和 1× sample，默认两帧 quarantine、256 MiB
  预算。FrameGraph 申请保持 soft/best-effort；UI Layer 申请使用 strict budget，必要时淘汰 idle LRU，
  再撤销最久未合成的 Layer backing。逻辑 LayerHandle 保持有效并标脏；本帧无法取得 backing 时同帧
  immediate 降级，隔离期结束后可复用目标。该池与 UI backend 都限定在
  renderer thread。Noop 契约测试已覆盖复杂路径、嵌套 stencil clip、局部 damage、clean composite、
  224 次离屏 pass 边界、strict budget/LRU 压力降级和 reset 重绘；D3D11/D3D12/Vulkan/OpenGL 均已
  通过 36-capture 的真实纹理、透明/opacity/blend、
  Preserve/Transparent/Color、resize/DPI/device-reset/MSAA-reset 图像门禁。UI Layer 保持 1× sample，
  因而 MSAA 门禁比较“reset 后恢复的旧 Layer”与“reset 后新建 Layer”，不把 multisampled immediate
  边缘当作同一参考。
- vector path 已进入通用 display-list；AYRenderer 支持简单凹多边形、圆角矩形、椭圆、圆弧、
  cubic Bezier、miter stroke、显式 winding 孔洞和嵌套 stencil path clip。自相交路径、布尔运算、
  fill-rule 选择和独立边缘 AA fringe 尚未实现。粒子、后端资源生命周期和显式 pass 仍走即时兜底。
- POSIX Clipboard 按 macOS `pbcopy/pbpaste`、Wayland `wl-copy/wl-paste`、X11 `xclip/xsel`
  的顺序选择可用后端；无可用 helper 或 headless session 时返回 `false`，不会阻塞或回退到私有剪贴板。
- `WidgetSerializer` 已覆盖全部 40 个公共注册类型；Grid、ScrollView、Menu、StatusBar、Tab、
  Modal 和 Dock 使用各自的结构化 payload。回调、焦点/hover、拖拽会话和 `DockTabGroup` 等运行时
  临时状态不属于持久化格式。
- Windows UI Automation adapter 已实现 Fragment tree、常用 control pattern、跨线程动作封送和
  增量事件；AT-SPI/NSAccessibility 原生 provider 仍待实现。TextInput/TextArea 的 UIA TextPattern、
  原生 selection range 和 live-region 事件也尚未进入本阶段。
- RichText 已实现扩展字素 caret、双向可视顺序、Unicode 断行和 HarfBuzz shaping，并支持 Windows
  已登记 family 的 regular/bold/italic/bold-italic face。内联对象、跨字体 glyph fallback、可变字体、
  彩色 emoji、完整编辑 selection 和非 Windows 字体发现仍属于后续字体系统工作。
- 3D spatial UI、像素遮罩命中测试和高级特效不在当前范围。

## 相关模块

- [AYRenderer](../AYRenderer/README.md)：`UIRenderBackend` 与 UI pass
- [AYEntity](../AYEntity/design.md)：引擎子系统和启动流程
