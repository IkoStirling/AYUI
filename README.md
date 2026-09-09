# AYUI

AYUI 是 AliyatEngine 的保留模式（retained-mode）2D UI 模块，覆盖控件树、布局、输入与焦点、JSON 布局、主题/i18n、弹层、Docking、动画，以及渲染后端抽象。

- CMake 目标版本：`1.0.0`
- 当前功能里程碑：v1.6 已实现
- 最近全模块审计：2026-09-08
- 权威架构文档：[design.md](design.md)
- 变更记录：[CHANGELOG.md](CHANGELOG.md)
- 历史方案：[AYUI-v1-Design.md](AYUI-v1-Design.md)（仅供追溯，不代表当前实现）

## 当前状态

AYUI 已接入根工程，`CMakeLists.txt` 会加入 `AYRuntime/AYUI`。AYRenderer 提供 `UIRenderBackend` 实现，AYUI 本身只依赖 `IRenderBackend`。

已实现的主要能力：

- Widget 树、命中测试、事件冒泡、焦点、鼠标捕获、拖放和多窗口 `UIManager`
- VBox/HBox、GridPanel、ScrollView、Splitter、自由布局 Anchor/Offset/Pivot 与约束辅助
- Button、输入框、ListView、虚拟化 TileView、树、菜单、工具栏、状态栏、Tab、Modal、Tooltip 等控件
- 通用 `ColorPicker`：HSV/透明度选择、十六进制输入、命名记忆色组及宿主拾色回调
- DockArea/DockCard/DockOverlay、嵌套 dock tree、浮动卡片与布局持久化
- JSON 布局加载、WidgetFactory、WidgetSerializer、文件热重载
- 42 个注册类型的 Serializer wire contract，含 ColorPicker、Grid cell、虚拟化 TileView、复合内容、Menu/Dock/Modal 专用结构
- StyleSheet/Theme、控件级 token override、I18n、UTF-8 文本往返
- 逻辑 DIP 坐标、独立 DPI/UI scale、物理输入换算与按缩放倍率栅格化字体
- 平台无关无障碍语义树、稳定节点 ID、角色/状态/动作推断、增量 diff 及 Serializer 元数据
- Windows UI Automation 原生 Fragment provider，覆盖 Invoke/Toggle/RangeValue/ExpandCollapse/SelectionItem
- Theme 命名父级继承和控件树 token override 级联
- TabStrip Scroll/Compress/Clip overflow；RichText 字素簇、双向文本、Unicode 断行和真实字体 shaping
- RichText run 级 font family/weight/italic/language，以及 AYRenderer 多 face、多 atlas 一致测量/绘制
- Win32、macOS、Wayland 和 X11 Clipboard 后端
- 脏标记、世界坐标缓存、颜色/透明度/位置动画和滚动惯性
- 多轨 Timeline/Keyframe、Sequence 串联、repeat/yoyo、物理弹簧、完成/取消/暂停控制、全局动画倍率与 reduced-motion
- 声明式 normal/hovered/pressed/disabled Style 状态色和 backgroundColor transition
- 默认启用 Widget-local retained display-list，保留即时绘制兜底
- 后端无关的 retained vector-path recipe，以及 AYRenderer 的凹多边形/曲线 tessellation、孔洞与 stencil path clip
- `IRenderBackend` 的 RenderTarget/UI Layer 生命周期、DPI、damage 与合成契约
- 可选的 Production root UI Layer：静态主树复用离屏像素，overlay/drag visual 保持即时绘制
- AYRenderer 共享 RenderTargetPool：FrameGraph 与 UI Layer 复用同一套 FBO 生命周期和预算，窗口
  resize 与运行时 MSAA 切换都会先失效租约再执行 bgfx reset
- Gallery，以及由独立工具和 AYEditor 独立 Designer 窗口共享的 Layout Editor core；Designer
  已覆盖常用叶控件、集合、Tab、容器和 Modal，支持结构化集合/树/Tab/RichText 编辑、可搜索纹理
  资源浏览、分辨率/DPI/Safe Area 设备预览、可回滚交互预览及声明式 controller/event 元数据；
  Authoring Quality 层进一步提供稳定资源键/本机预览路径隔离、多选公共几何与 Style 编辑、
  Style 来源和 Normal/Hovered/Pressed/Disabled 状态预览，以及可点击定位的布局诊断列表；
  Reuse & Responsive 层提供文档内可复用 Widget block、Compact/Medium/Wide 断点预览、
  断点可见性和锚点覆盖；Animation Authoring 层提供按稳定 Widget ID 绑定的 opacity/position/size
  轨道、关键帧/曲线、repeat/yoyo/reduced-motion importance，以及不污染源数据的时间点 scrub 预览

2026-09-09 Insider Windows Debug 当前基线为 `5510 / 5510` 条断言通过，Layout Editor headless
round-trip 同步通过。2026-08-29 审计快照为
`4643 / 4643`；旧基线中的循环内重复 `CHECK` 已改为循环累计失败数、循环结束统一判断，
测试用例和输入迭代覆盖没有减少。

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
loader.bindControllerEvent("PauseController", "confirm", [] {
    // Handle the action in host code.
});

ayt::ui::Widget* root = loader.loadFromFile("menu.ui.json");
// The returned factory-built tree is owned by the caller.
ayt::ui::destroyWidgetTree(root);
```

JSON 中不保存或执行代码。Widget 只持久化 `controller` 与 `events` 中的处理器名字；Loader 按
“`bindEvent(widgetId, eventName)` 旧式精确绑定 → `bindControllerEvent(controller, handler)` →
`bindHandler(handler)` 全局命名绑定”的顺序解析为宿主提供的 C++ 回调。

动画策略和时间线同样从 `AYUI.h` 暴露：

```cpp
AnimationSettings::get().setDurationScale(1.0f);
AnimationSettings::get().setReducedMotion(userPrefersReducedMotion);

AnimationTimeline fade;
fade.addFloatTrack({{0, 0}, {160, 1, AnimationCurve::EaseOut}},
                   [&panel](float alpha) { panel.setOpacity(alpha); });
AnimationTimeline slide;
slide.addVec2Track({{0, {0, -8}}, {160, {0, 0}, AnimationCurve::EaseOut}},
                   [&panel](const math::FVector2& p) { panel.setPosition(p); });

SpringParameters spring;
spring.stiffness = 120.0f;
spring.damping = 13.0f;
spring.clampOvershoot = true;
AnimationTimeline pulse;
pulse.addFloatTrack(
    {AnimationKeyframe<float>(0, 0), AnimationKeyframe<float>(650, 1, spring)},
    [&progress](float value) { progress.setValue(value); });
pulse.setRepeatCount(3).setYoyo(true); // 首轮 + 3 次，方向交替

AnimationSequence intro;
intro.append(std::move(fade)).then(std::move(slide));
intro.play();
// 在宿主或 Widget tick 中调用 intro.tick(deltaSeconds)。
```

`repeatCount` 表示首轮之后的额外次数；`AnimationTimeline::RepeatForever` 表示持续播放，直到取消。
时间线以总播放时钟求当前轮次，因此一次 `tick` 跨越多个周期不会丢时间；Sequence 也会把跨步骤终点的
剩余帧时间继续交给下一步。带 `SpringParameters` 的关键帧使用 mass/stiffness/damping/initialVelocity
二阶响应，`clampOvershoot` 可将插值因子限制在 `[0, 1]`。未提供参数的 `AnimationCurve::Spring`
继续保留原来的轻量曲线，Widget tween 和 renderer 兼容路径不受影响。

Widget 的简单属性动画仍可直接使用 `animateOpacity/animatePositionTo`；传入 `AnimationOptions` 可设置
完成/取消回调、importance，并通过 `pauseAnimations/resumeAnimations/cancelAnimations` 控制播放。

Designer 保存的文档级动画由 `UILayoutLoader` 一并读取。运行时按 clip 名创建绑定到本次加载树的
时间线；时间线不能晚于对应 Widget 树销毁：

```cpp
UILayoutLoader loader;
Widget* root = loader.loadFromFile("hud.ui.json");
AnimationTimeline intro = loader.createAnimationTimeline("Intro");
intro.play();
// 每帧 intro.tick(deltaSeconds)
```

大数据网格使用 `TileView`。控件只分配“可见行 + overscan”的 `TileCell`，滚动时重绑逻辑索引；
宿主通过 binder 把自己的数据模型投影到这些临时 cell，不应保存某个 index 对应的 `TileCell*`：

```cpp
TileView tiles;
tiles.setTileSize({104.0f, 150.0f});
tiles.setInfoStripHeight(16.0f);
tiles.setCornerMarkerSize(12.0f);
tiles.setThumbnailAspectRatio(1.0f);              // width / height
tiles.setSelectionMode(TileView::SelectionMode::Extended);
tiles.setItems(assetDisplayNames);               // 1 万项也只保留少量 cell
tiles.setCellBinder([&](TileCell& cell, int index, const std::wstring&) {
    const auto presentation = assetPresenter.present(index);
    cell.setText(presentation.name);
    cell.setInfoStrip(presentation.type, presentation.categoryColor,
                      math::FVector4(1, 1, 1, 1));
    cell.setCornerMarkerVisible(presentation.marked);
    cell.setAccentColor(presentation.categoryColor);
});
tiles.setOnRenameRequested([](int index) { /* F2 */ });
tiles.setOnItemDoubleClicked([](int index, TileCell::HitRegion region) {
    // region 精确区分 Thumbnail / Label / Body。
});
```

方向键按当前列数导航，Home/End/PageUp/PageDown、Ctrl+A、Shift 范围选择和 Ctrl 焦点移动均由
TileView 处理。拖拽默认产生 `AYUI.TileItems` payload；文件浏览器等宿主可用
`setDragPayloadBuilder()` 替换 payload。拖拽靠近视口上下边缘时会自动滚动。

`InfoStrip` 与 `CornerMarker` 是无业务语义的展示接口：AYUI 不解释横条文本、类别或标记含义。
横条固定在缩略图与文件名之间，角标用 vector path 绘制在缩略图右上角；二者均不引入新的
`HitRegion`。虚拟池每次重绑会在调用 binder 前清除横条、角标、缩略图、Badge 和辅助文字，
因此 binder 只需设置当前 item 实际拥有的状态，不能保存 `TileCell*`。

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
          "controller": "PauseController",
          "events": {
            "onClick": "resumeGame"
          }
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
- `controller` 和 `events` 是可往返的声明式名字，不是脚本；当前内置接线覆盖 `onClick`、
  `onToggled`、`onValueChanged`、`onTextChanged`、`onSubmit`、`onSelectionChanged`、
  `onItemActivated` 与 `onClose`；RadioButton 可接 `onToggled`，TextArea 可接 `onTextChanged`。
  旧顶层 `onClick` 仍可读取并迁移到 `events.onClick`。
- Dock 布局通过 `UILayoutLoader::saveLayout*` 与 `loadFrom*` 持久化。

## Layout Editor

独立 `AYUI_LayoutEditor` 与 AYEditor 的独立 Designer 窗口共用 `LayoutEditorSession`。Widget Library
以可拖拽、带类型 SVG 的分类列表提供：Button/Text/RichText/Input/TextArea/CheckBox/RadioButton、
Slider/ProgressBar/Spinner/Image，ComboBox/ListView/TileView/TreeView，TabStrip/TabControl，以及
Panel/VBox/HBox/GridPanel/ScrollView/Separator/Tooltip/Window/Modal/ModalDialog。List/Tile/Combo、完整
Tree source、Tab page 和 RichText run 使用统一的 Structured Content Inspector 增删、重命名和排序；
Tree 可增加子节点，RichText run 可编辑字号、颜色和粗体/斜体/下划线，不再把复杂模型压平成
`|` 分隔字符串。折叠的 Tree 后代仍参与保存，TabControl page 的移动保持 page ownership 与选择状态。

选择 Image 后可编辑纹理名字，也可使用 Browse/Clear。Session 只定义两个宿主回调：路径选择器
返回要持久化的纹理名字，preview loader 把它解析为当前后端的临时 `ImageTextureHandle`。独立工具
使用 AYRenderer 上传 PNG/JPEG/BMP/TGA，AYEditor 子窗使用自己的 GDI 预览纹理；JSON 只写
`textureName`，不写 GPU/GDI 句柄。生产宿主应让路径选择器返回可由其资源系统解析的相对路径或
资源键，而不是依赖某台机器的绝对路径。

宿主也可通过 `setTextureResourceProvider()` 提供项目/引擎纹理目录。Inspector 内的资源浏览器支持
搜索、刷新、选中赋值、尺寸状态和 missing/invalid 警告；资源键与后端 preview handle 分离，目录扫描
和资源身份仍属于宿主。standalone 默认扫描常见 assets 与 AYRenderer core textures，AYEditor 默认
组合 Project/Assets 和 EngineAssets。

View 菜单与 Canvas header 提供 Document、Desktop、HiDPI Desktop、Phone、Tablet 预设以及自定义
物理宽高、DPI scale 和 Safe Area inset。画布把物理尺寸换算为逻辑 DIP，并以 editor-only overlay
显示安全区；预览尺寸不进入 undo snapshot 或 `.ui.json`，保存时仍写 authored root 尺寸。F6 或
Interact 按钮进入运行时交互预览：编辑选择框/拖放暂时关闭，控件恢复原始 enabled/read-only 状态；
退出预览时整棵文档从进入前 snapshot 恢复，避免试点按钮、输入或选择状态污染设计稿。

Inspector 只为当前类型显示有效事件；Controller 和处理器字段只创作声明式元数据。游戏/工具宿主
仍负责注册真正的 controller 回调。ScrollView content、TabControl page、Modal content/body 在
Outline 中是固定结构槽：可以把控件拖入这些槽，但不能把槽根当作普通 child 拖走，避免控件内部
内容引用与可见树分离。结构槽中的未挂载 Tab page 同样参与 ID 唯一性检查；保存后由生产
`UILayoutLoader` 按专用 payload 重建，并把深层 ID 注册到 `findWidgetById()`。

自由布局控件支持 4×4 Anchor preset：横轴 Left/Center/Right/Stretch 与纵轴
Top/Center/Bottom/Stretch 的全部组合。锚点使用归一化 `anchorMin/anchorMax`，边界由
`parentSize * anchor + offset` 决定；切换 preset 会保留控件当前屏幕矩形。Inspector 可继续编辑
Min/Max、两组 Offset 与 Pivot，也可切回 Absolute。画布以琥珀色范围框和锚点显示约束；这些操作
参与 undo/redo 并随 JSON 往返。Anchor 只适用于 Panel 等自由布局父级，Grid/VBox/HBox 子项仍由
各自 slot/cell 布局管理。对齐按钮要求至少两个同父级自由控件，分布按钮要求至少三个，不满足时
自动禁用。父级 `setSize()` 会立即重算锚定后代，不依赖 viewport resize 或下一次全树 layout；
在 preset 上按住 Ctrl 点击还会立即把控件吸附到固定锚点，Stretch 轴则贴合父级两边。Document
root 的位置固定，但 Width/Height 始终可在 Inspector 编辑，也可拖动右边、下边或右下角手柄改变
预览尺寸；切换选中项时 Inspector 自动回到首行，避免旧滚动偏移把根尺寸字段移出裁剪区。

Pan/Zoom 由 editor-only `CanvasViewport` 处理，只改变画布视图 transform 和输入坐标映射，不修改
文档 Widget 几何、dirty 状态、undo snapshot 或序列化结果。ID 提交会检查格式、编辑器保留前缀与
全树唯一性；保存使用同目录临时文件原子替换，Paste 优先读取当前系统剪贴板，Duplicate 不覆盖系统
剪贴板。带 `textureName` 的 Image 在打开、undo/redo 和粘贴后由宿主 preview loader 恢复预览句柄。

Authoring 代码位于独立静态库 `AYUILayoutEditorCore`，不进入游戏只需链接的 AYUI runtime。核心把
`LayoutDocumentModel`、`LayoutSelectionModel`、`LayoutCommandStack` 与 `LayoutCanvasViewport`
从宿主 Session 中拆开；standalone 与 AYEditor 只负责窗口、backend 和资源选择器。Palette、类型图标、
默认尺寸/初始化与 Inspector schema 统一来自 `WidgetAuthoringRegistry`，新增类型不再需要同步修改多张
硬编码表。`PropertySchema` 按字段/section 生成属性行显隐；命令栈已记录 Property/Insert/Delete/Reorder/
Transform/Clipboard 等类型化 edit intent，同时暂时保留完整 JSON snapshot 作为可靠 undo/redo 兜底。

Authoring Quality 使用 `LayoutTextureResource::key` 作为可序列化的稳定身份，并把本机文件位置放在
仅用于解码预览的 `previewPath`；打开文件、Undo/Redo 和复制后都只恢复临时句柄，不把绝对路径写回
布局。Inspector 多选时用 `—` 表示不一致的 X/Y/Width/Height，提交数值或 Style 会在一个 undo 事务
内写入全部选中项。Style 状态预览显示来源、继承/本地 token override 数和四种交互背景色。
`LayoutValidationModel` 对作者语义树检查空/非法/重复 ID、非有限或非正尺寸、缺失 Style/纹理、
无 Controller 的事件、无可访问名称的交互控件、非法 Anchor、越界控件与 Grid slot 重叠；诊断行可
直接定位控件，且不会把列表虚拟行、Tab 内部节点或编辑器 overlay 当成用户文档。

Reuse & Responsive 阶段加入文档内 `LayoutReuseLibrary`。选中的完整 Widget 子树可定义或更新为
命名 block，并可反复插入；每次插入都会展开成独立 Widget 树并重新生成 ID，因此实例后续可以单独
修改，运行时也不需要链接模板服务。含 block 的布局采用 `{ format, version, reusable, root }` 文档
信封；没有 block 时继续输出旧版 root-only JSON。`UILayoutLoader` 与 `WidgetSerializer` 均可直接读取
信封并只装入 `root`，定义本身仅属于 authoring 文档。

响应式规则以直接父容器的逻辑 DIP 宽度求值。Designer 提供 Compact（`<600`）、Medium
（`600–1023`）和 Wide（`>=1024`）三个标准区间，可预览对应宽度、覆盖可见性，并为自由布局控件
捕获断点专用 Anchor。离开区间后恢复 authored visibility 和基础 Anchor，不会把预览结果写回源属性；
VBox/HBox/Grid 等结构化父级继续拥有几何布局权，但响应式隐藏会退出布局占位并触发重排。规则随
Widget JSON 往返，非法区间、重叠区间和没有基础 Anchor 的覆盖会进入 Validation 诊断。

Animation Authoring 在同一版本化文档信封中保存 `animations`。每个 clip 由稳定 Widget ID、
`opacity/position/size` 属性轨道和按时间排序的关键帧组成，支持 Linear/Ease/Spring 曲线及
repeat/yoyo/Decorative/Essential 播放元数据。Designer 的 Scrub 先保存 authored snapshot，再按时间点
采样，Reset 或下一次编辑会恢复源值；预览结果不进入 dirty 文档。缺失目标、空/单关键帧轨道以及与
Anchor、VBox/HBox/Grid 几何权属冲突的轨道都会进入 Validation；重命名 Widget ID 会原子更新轨道引用。
当前面板是结构化轨道/关键帧编辑，
连续播放 transport、拖拽式时间尺和曲线图仍属于后续交互增强。

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
- `LayoutEditor/`：可复用的 authoring model、registry、schema、command stack 与 Session
- `i18n/`：语言表
- `unittest/`：模块回归测试
- `demo/`：Gallery 与 Layout Editor standalone/round-trip 宿主

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
- `WidgetSerializer` 已覆盖全部 41 个公共注册类型；Grid、ScrollView、TileView、Menu、StatusBar、Tab、
  Modal 和 Dock 使用各自的结构化 payload。Controller/event 处理器名字属于持久化元数据；真正的
  回调函数、焦点/hover、拖拽会话和 `DockTabGroup` 等运行时状态不属于持久化格式。
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
