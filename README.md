# AYUI

AYUI 是 AliyatEngine 的保留模式（retained-mode）2D UI 模块，覆盖控件树、布局、输入与焦点、JSON 布局、主题/i18n、弹层、Docking、动画，以及渲染后端抽象。

- CMake 目标版本：`1.0.0`
- 当前功能里程碑：v1.5 已实现
- 最近全模块审计：2026-08-27
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
- StyleSheet/Theme、控件级 token override、I18n、UTF-8 文本往返
- 脏标记、世界坐标缓存、颜色/透明度/位置动画和滚动惯性
- Gallery 与独立 Layout Editor

2026-08-28 修复逐帧提交契约后的 Windows Debug 基线为 `7405 / 7405` 条断言通过。

重要渲染契约：AYUI 保留 Widget 状态和树，`UIRenderBackend` 保持即时、逐帧提交。后端在 `beginFrame()` 清空上一帧命令，因此所有可见 Widget 必须每帧 replay；dirty 标记仅用于 presentation/cache invalidation，不能跳过当前帧提交。真正减少静态 UI 的 CPU 构建开销需要 retained display-list 或离屏层缓存。

## 快速接入

常用入口是聚合头 `AYUI.h`：

```cpp
#include "AYUI.h"

ayt::ui::UIManager ui;
ui.initialize(renderBackend);       // renderBackend implements IRenderBackend
ui.setClientSize(1280.0f, 720.0f);

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
- Dock 布局通过 `UILayoutLoader::saveLayout*` 与 `loadFrom*` 持久化。

`WidgetFactory` 是类型名到构造器的唯一注册点。内置控件由模块自动注册；宿主扩展控件可调用 `registerCreator` 或使用 `REGISTER_WIDGET`。

## 所有权规则

- `UILayoutLoader` / `WidgetSerializer` 返回的根树由调用方负责，使用 `destroyWidgetTree` 销毁。
- 普通 `addChild` 将节点纳入 `destroyWidgetTree` 的递归销毁范围；`Widget` 析构本身不删除 children。`addChildExternal` 用于宿主/栈对象持有的外部生命周期场景。
- Popup 由 `UIManager` 的 overlay 管理；关闭路径可能带淡出动画，关闭后不要继续解引用已销毁的 popup。
- `DockCard::setContent` 接管 content；`Modal::setDimmerOwned` 接管 dimmer，`setDimmer` 不接管。
- 模块是 UI 线程模型；不要从后台线程直接修改 Widget 树。

完整不变量见 [design.md](design.md#5-生命周期与所有权)。

## 图片与控件组合

`Image` 支持纯色回退、命名/匿名纹理句柄、UV 裁剪和整体透明度。控件叠图有两种常用结构：把 `Image` 作为 `Button` 的装饰子节点（按钮本身仍接收点击），或在 `Panel` 中先放背景 `Image`、再放文本和交互控件；同级子节点按插入顺序绘制，后加入者位于上层。

`AYUI_Gallery` 的 **Images** 页面提供共享纹理、UV crop、图片按钮、半透明图片层和图片背景上可点击控件的实机示例。示例纹理由 Gallery 运行时生成，不依赖外部图片文件。

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

脚本覆盖九条 Gallery 路径（包括图片叠加、输入、列表、布局、渐变、动画以及带 dimmer
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

- Linux/POSIX Clipboard 当前是安全的 no-op 实现；Windows 使用 Win32 clipboard。
- `WidgetSerializer` 对核心控件和 DockArea 持久化路径有覆盖，但并非所有运行时/内部控件都保证完整语义往返；详见 [design.md](design.md#8-jsonfactoryserializer-契约)。
- GridPanel 的 cell attachment、部分复合控件内部结构仍有专用加载路径，不能只靠通用 `children` 推断。
- TabStrip 溢出目前裁剪，不提供水平滚动；RichText 仍是轻量 runs 模型。
- 3D spatial UI、像素遮罩命中测试和高级特效不在当前范围。

## 相关模块

- [AYRenderer](../AYRenderer/README.md)：`UIRenderBackend` 与 UI pass
- [AYEntity](../AYEntity/design.md)：引擎子系统和启动流程
