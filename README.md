# AYUI

AYUI 是 AliyatEngine 的保留模式（retained-mode）2D UI 模块，覆盖控件树、布局、输入与焦点、JSON 布局、主题/i18n、弹层、Docking、动画，以及渲染后端抽象。

- CMake 目标版本：`1.0.0`
- 当前功能里程碑：v1.5 已实现
- 最近全模块审计：2026-08-28
- 权威架构文档：[design.md](design.md)
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
- 平台无关无障碍语义树、稳定节点 ID、角色/状态/动作推断及 Serializer 元数据
- Theme 命名父级继承和控件树 token override 级联
- TabStrip Scroll/Compress/Clip overflow 与 RichText 多段落完整布局
- Win32、macOS、Wayland 和 X11 Clipboard 后端
- 脏标记、世界坐标缓存、颜色/透明度/位置动画和滚动惯性
- 默认启用 Widget-local retained display-list，保留即时绘制兜底
- 后端无关的 retained vector-path recipe，以及 AYRenderer 的凹多边形/曲线 tessellation、孔洞与 stencil path clip
- `IRenderBackend` 的 RenderTarget/UI Layer 生命周期、DPI、damage 与合成契约
- Gallery 与独立 Layout Editor

2026-08-28 Windows Debug 基线为 `4436 / 4436` 条断言通过。旧基线中的循环内重复
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

## 快速接入

常用入口是聚合头 `AYUI.h`：

```cpp
#include "AYUI.h"

ayt::ui::UIManager ui;
ui.initialize(renderBackend);       // renderBackend implements IRenderBackend
ui.setClientSize(1280.0f, 720.0f);  // physical framebuffer pixels
ui.setDpiScale(windowDpi / 96.0f);  // OS monitor scale
ui.setUiScale(userPreference);      // independent in-app zoom

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

`buildAccessibilityTree()` 返回可映射到 UI Automation、AT-SPI 或 NSAccessibility 的快照，
`performAccessibilityAction()` 用节点 ID 把 Press/Toggle/Select/Increment 等动作路由回 Widget。
AYUI 不直接链接某个桌面无障碍框架，原生宿主桥负责发布快照和转发动作。

`TabStrip` 默认使用水平 Scroll overflow，滚轮可移动视口且选中项自动进入可见区；
Compress 会从 `minTabWidth` 向 24 DIP 的交互硬下限压缩，Clip 保留旧策略。`RichText` 支持 Word/Character
换行、显式段落、四种水平对齐、三种垂直对齐、行高/行距、最大行数与省略号、run 字号/
颜色/bold/italic/underline/strike/letter-spacing/baseline-shift，以及测量、命中和 caret 几何。

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

- UI Layer / RenderTarget 契约和 MockRenderer 生命周期已经落地；AYRenderer 暂时报告不支持，
  生产 bgfx FBO、纹理回收及子树离屏缓存仍是下一阶段，因此当前优化减少的是 CPU 侧控件
  命令构建，不会把静态 UI 变成只提交一次。
- vector path 已进入通用 display-list；AYRenderer 支持简单凹多边形、圆角矩形、椭圆、圆弧、
  cubic Bezier、miter stroke、显式 winding 孔洞和嵌套 stencil path clip。自相交路径、布尔运算、
  fill-rule 选择和独立边缘 AA fringe 尚未实现。粒子、后端资源生命周期和显式 pass 仍走即时兜底。
- POSIX Clipboard 按 macOS `pbcopy/pbpaste`、Wayland `wl-copy/wl-paste`、X11 `xclip/xsel`
  的顺序选择可用后端；无可用 helper 或 headless session 时返回 `false`，不会阻塞或回退到私有剪贴板。
- `WidgetSerializer` 已覆盖全部 40 个公共注册类型；Grid、ScrollView、Menu、StatusBar、Tab、
  Modal 和 Dock 使用各自的结构化 payload。回调、焦点/hover、拖拽会话和 `DockTabGroup` 等运行时
  临时状态不属于持久化格式。
- 无障碍语义和动作层已经稳定，UI Automation/AT-SPI/NSAccessibility 的事件发布、增量树同步与
  平台生命周期仍由宿主 adapter 实现。
- RichText 的段落布局已完整落地；Unicode 字素簇级 caret/断行、双向段落编辑、内联图片和
  AYRenderer 字体家族的真实粗体/斜体 face 选择仍属于后续字体系统工作。
- 3D spatial UI、像素遮罩命中测试和高级特效不在当前范围。

## 相关模块

- [AYRenderer](../AYRenderer/README.md)：`UIRenderBackend` 与 UI pass
- [AYEntity](../AYEntity/design.md)：引擎子系统和启动流程
