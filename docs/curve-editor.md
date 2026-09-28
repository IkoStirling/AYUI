# 公共曲线与时间轴作者控件

2026-09-28，第一版。入口位于 `AYUI/Authoring/`，命名空间为
`ayt::ui::authoring`。不建立额外 AYCurve 模块，也不新增资源格式。

## 层次与接入

| 层 | 职责 | 不负责 |
|---|---|---|
| AYMath `CurveMath.h` | Hermite 段数学 | 资源、时间单位、Quaternion 插值策略 |
| `AYUITimelineCore`（header-only） | 秒制 TimeViewport、不可变快照、选择、owner 接口与手势阶段 | UI、持久化、资源类型 |
| `AYUICurveEditor`（独立静态目标） | CurveCanvas / DopeSheet 的绘制、命中、缩放/平移、框选、拖动 | 动画骨骼、Notify 规则、验证、撤销和保存 |
| 页面 source 适配器 | 资源转换、正式采样、验证、时间吸附、事务与 ID 重映射 | 公共控件绘制 |

新页面实现 `ICurveEditorSource`，链接 `AYUICurveEditor`，为同一文档创建一个
shared source 并传给两种控件。需要无 UI 数据/变换时仅链接 `AYUITimelineCore`。
源接口的完整非动画示例是参与构建的 `unittest/CurveEditorTest.cpp`；动画适配样例
是 AYEditor 的 `src/AYEditorAnimationCurveSource.cpp`。

```cpp
#include <AYUI/Authoring/CurveCanvas.h>
#include <AYUI/Authoring/DopeSheet.h>

using namespace ayt::ui::authoring;

// source 由宿主的文档适配器实现，并由视图共享持有。
auto makeCurveViews(std::shared_ptr<ICurveEditorSource> source,
                    const std::string& trackId) {
    auto curve = std::make_shared<CurveCanvas>(source);
    auto sheet = std::make_shared<DopeSheet>(source);
    curve->setTrackId(trackId);
    // 宿主把控件加入布局，并连接下面的刷新/历史生命周期。
    return std::make_pair(curve, sheet);
}
```

控件不会自动订阅宿主文档。宿主在 owner 内容、选中状态或播放头变化后
`markDirty()` 相应视图；连接 `setOnEdited` / `setOnSelectionChanged` 刷新其他视图
和属性面板。回调捕获兄弟视图时使用 weak_ptr，避免互相持有造成环。
`selectionState()` 共享主关键帧、多选列表、轨道和分量。DopeSheet 的
`setSelection` 接收重复主选中值时保留多选，适用于属性面板同步刷新。

## 数据与编辑契约

- 所有公共时间均为秒。source 转换资源 ticks 或 UI 毫秒，不在控件中识别资源格式。
  track/key ID 对控件不透明；同一文档内 key ID 应唯一。keys 按时间递增，
  分量与切线长度一致，时间/值/采样结果有限，由 owner 保证。
- `curveTrack()` / `timelineSnapshot()` 返回缓存的不可变 shared_ptr。内容改变才
  增加 revision 并替换快照；seek/tick 不改变内容 revision。快照和采样闭包
  必须持有所需数据，允许在撤销或加载新 revision 后继续安全读取旧快照。
- `CurveTrack::sample` 必须遵守所属领域的正式播放语义。控件不复制轨道插值器；
  AYEditor 的 Float/Vector/Quaternion 使用 AYAnimation `KeySampler`，Quaternion
  不能当作四条独立标量插值。可见分量在快照或时间视区改变后采样，播放头刷新复用缓存。
- `editableTangents` 只表示可按 value/second 编辑 Hermite 斜率。Bezier 控制点与
  Spring 参数不是 Hermite 切线，保留专用编辑器，由 source 提供对应曲线采样。
- 一次拖动通过 `beginEdit` 开始，连续变更由 owner 验证，松开调用 `endEdit(false)`；
  捕获取消、切换轨道或销毁控件调用 `endEdit(true)` 恢复 owner 数据及拖动开始的选择。
  控件只取消自身成功开始的事务。owner 保证每次失败不产生部分修改。
- key 排序可能改变 ID；成功修改必须回传新 ID，批量变换保持列表长度和对应顺序。
  Notify/Event 的内容和 ID 规则只存在于适配器。拒绝 beginEdit 的只读 owner
  仍允许选中和播放头定位；未支持的编辑命令返回 false。
- source 控制吸附粒度和边界，控件只提出变更请求。保存、Undo/Redo、资源引用、
  baked/legacy 只读策略由宿主管理，不在公共控件中实现第二套历史或资源写入。

## 本次迁移与限制

### 大选择与播放重绘（2026-09-29）

DopeSheet 按不可变 TimelineSnapshot 身份缓存行→key 索引；同一快照播放头重绘
不会重建索引，`rowIndexBuildCount` 仅作诊断。owner 必须更换内容快照，不能原地改它。
选择去重/ID 重映射保持输入顺序，使用哈希避免大选择平方扫描；绘制建立选中 ID 集合。
CurveCanvas 的分量手势会先过滤到当前轨道可见 keys，避免跨行选择使其他轨道被隐式
修改；跨行时间移动使用 DopeSheet 的 owner 批量接口。动画 Clipboard/retime 是
领域核心能力，不移入通用 UI。5 万项选择/200 行回归检查索引次数而非耗时阈值。

### 稳定 ID 列表与拖放来源

`StableListRows<Id>` 保存 owner 范围内的可见行/ID 双向映射；过滤/重排后按选中 ID
恢复行位置，重复 ID 原子拒绝。映射不拥有文档，不把列表行号当作领域 ID。
`DragSourceList` 复用 ListView 的绘制、滚动条和键盘导航，阈值到达时才调用宿主
payload provider；刷新、provider 替换或捕获取消结束旧手势/拖放。选择/provider 回调
重建列表时旧手势失效。单选用途，Extended 多选拖放仍用专用列表。
骨骼列表和 GameFlow 动作面板已迁移；过滤文本、骨骼索引校验、动作类型/拖放目标仍归页面。
通过基类 ListView 引用换行时需先 cancelPendingDrag；正常类型化 setItems 自动取消。

### 后台任务状态展示与宿主绑定

`JobPresentation.h` 是 UI-free 的任务快照/展示绑定：显式 begin(generation)，忽略旧任务，
终态不得回退，每代完成通知只消费一次；reset 用于项目/来源关闭。进度按整数百分比
抑制重复刷新并限制在 0–100，非法进度显示为 0。`formatJobReport` 共用进度、消息与输出列表。
取消请求必须提供刚轮询的同代快照与宿主回调，不支持取消的任务不会获得取消能力。
骨骼烘焙与资产导入状态已接入；骨骼完成后的指纹校验、保存、导入后的索引更新仍由原宿主处理。
公共层不持有线程/future，不运行任务、不写入文件，失败/取消不冒充成功。

### 编辑器文本编码

界面到资源/文档的 UTF-8 转换共用 `AYUI/UnicodeText.h` 的 `encodeUtf8Text`，
与 `decodeUtf8Text` 配对。保留内嵌 NUL，UTF-16 代理对编码成四字节 UTF-8，
孤立代理项和超出 Unicode 范围的标量替换为 U+FFFD。不依赖系统代码页，不做归一化。
动画、骨骼、GameFlow、UIFlow、DSL、项目设置、主编辑器/子窗口及 Layout 作者页已迁移。

### 公共属性表单字段

`PropertyField` 提供文本/枚举属性行、稳定子控件 ID、静默刷新、只读/显隐、
可选失焦/枚举提交、owner 校验和提交回调。未列出的枚举值保留，隐藏字段保留草稿，
校验失败不调用提交，提交回调不能重入。它不解释领域 schema，不建立历史。
校验或提交回调关闭并销毁页面时，不再访问已释放的字段，也不会继续提交已关闭字段。
GameFlow 属性行已接入，原草稿整体校验与命令仍由页面拥有。完整表单可组合
`NumericFields`、`ResourceReferenceField` 与现有 Layout 专用字段；Layout schema 不改名或泛化。

### 通用诊断展示

`DiagnosticsPanel` 接受 owner 的 severity/code/message/opaque target，提供数量限制、
展开更多、级别筛选和 Locate 下拉；刷新不触发定位。定位是宿主回调，页面重新验证
目标有效性后只改变选择/显示。不会执行资源检查、重定向或烘焙。
动画诊断、骨骼 preflight、dry-run 所有问题/操作/依赖已接入（不再在页面截断报告）。
进度及模板说明使用 `setReport` 保留原文并隐藏结构化筛选；日志不是伪造的 issue。

### 选择操作与 Editor 通知桥接

`TimelineSelectionOps` 在原 `TimelineSelection` 上执行规范化单选、多选、清空和
位置对应的 ID 重映射：去重/过滤空 ID、保持主选中有效，失败不部分更改。
可选 changed 回调只在真实变更后调用；空选择也可通知。程序化视图同步仍静默。
CurveCanvas/DopeSheet 已迁移；取消手势恢复选择后通知宿主。未改变原数据/owner ABI。
`EditorAuthoringSelectionBridge` 将 owner 选择单向投射到现有 `EditorSelectionContext`。
受管理文档使用真实 workspace documentId，每次重新解析，关闭后不保留旧 context；
独立 view 使用本地 context。动画键/轨道和骨骼选择已接入并暴露 view.selectionContext。
不自动反向应用外部选择，不把骨骼/事件 ID 解释移入公共控件。

### AYEditor 命令按钮适配

`AYEditor/EditorCommandButtons.h` 为现有 `IEditorCommandTarget` 创建按钮绑定，
不注册另一套命令/历史；每次执行重新解析当前 target，并重新检查 handles/canExecute。
动画/骨骼页面保存、撤销、重做和动画曲线删除已使用相同的 view 命令入口供快捷键路由。
宿主在状态改变后 refresh，销毁控件树前 detach；保留的按钮闭包在 detach/析构后失效。
普通公共 AYUI 控件不依赖此适配器或 AYEditor。

### 预览视口基础

`PreviewViewport.h` 的 `PreviewOrbit` 管理旋转/缩放/重置/取消；`PreviewBounds`
收集有限世界点；`PreviewProjection` 按 owner 的视区和覆盖比例作正交投影。
`PreviewProjectionCache` 仅比较内容、姿势、视角和尺寸，显式失效可强制重建。
动画/骨骼 canvas 已共用这些基础，保留原覆盖比例、并排布局、蒙皮、绘制和拾取。
这不是运行时 Camera 或通用 GPU viewport，不持有文档或推进动画。
宿主在捕获取消和离开视区时结束旋转。测试见 `CurveEditorTest.cpp`。

### 数值与关键帧分量字段

`ResourceReferenceField` 共用资源引用输入/可选 Pick/Load/简短状态；传入宿主的
picker 和校验加载回调，不依赖原生对话框或资源系统。`setPath` 不发起加载，
取消 picker 保留原输入，失败不清空文本，只读禁止提交。详细失败信息由宿主显示。
动画预览绑定和骨骼目标/动画加载已接入，原输入别名与目标控件 ID 保留。

`NumericFields` 共用分量输入创建、格式、维数显隐、只读、unit 标签和提交回调。
`setValues` 是无提交的界面刷新；`readValues` 要求完整、有限的 Float 数值，失败时
不改输出数组。超出字段容量或包含非有限值的刷新整体拒绝，不留下半更新界面。
单位仅用于显示，不在控件中重标定 ticks、归一化 Quaternion 或验证资源。
动画 key value 与 in/out tangent 面板已迁移，保持原输入 ID 和保存/验证/Undo 行为。

Layout 的 `LayoutVectorPropertyEditor` 保持原类布局与接口，仅将创建逻辑委托给
`addNumericInputs`，保留 schema 字段 ID、style、尺寸及原字段回调。它不强制采用
动画面板的完整数值解析规则，避免改变既有 Layout 作者输入语义。
`AYUILayoutEditorCore` 链接作者控件目标；游戏侧 AYUI runtime 不新增该依赖。

### 独立播放控制

`IPlaybackSource` 与曲线 source 分离，返回秒制播放状态以及可选 loop/rate；
`PlaybackControls` 可按 options 组合播放按钮、seek、时间、状态、loop/rate。
它从不调用 tick，宿主保留唯一播放推进权；`refresh()` 抑制程序化赋值回调，
不会因刷新或范围变化再次 seek。宿主用 `setOnChanged` 刷新兄弟视图/预览。
与其他 AYUI 复合控件一样，通过 Widget 树正常销毁其孩子。

AYEditor 的动画页、骨骼页和上下文 Timeline tool 共用该控件；
`AYEditorTimelinePlaybackSource.h` 适配既有 `IEditorTimelineSource`，每次操作
获取 shared document，不改变公共 Editor 接口。上下文文档切换/关闭后重新解析
当前 source，缺少 source 时禁用控件；音频 clip、waveform 和编辑规则仍由文档拥有。

### 共享作者基础（第二阶段）

`AuthoringPrimitives.h` 属于 UI-free `AYUITimelineCore`：`timelineTicks` 按像素密度
生成有数量上限的 1/2/5 刻度，`formatTime` 支持秒/帧/自适应毫秒，
`snapTimeToInterval` 只计算吸附（边界仍由 owner 校验），`TimelineRowLayout`
共用行坐标和可见行范围。曲线/DopeSheet/Layout 时间轴已接入相应基础。

`EditGestureSession` 对 owner 的 begin/endEdit 做有所有权的生命周期管理，
只取消自身成功开始的事务，取消时恢复开始选择；endEdit 失败时保留所有权以便重试。
CurveCanvas 与 DopeSheet 共用它；不会建立另一套历史。
`AuthoringRefreshGate` 比较宿主提供的内容、选择、姿势版本和播放状态/位置，
不扫描资源、不自动订阅；动画/骨骼页已替换各自版本检测，播放刷新不重建作者面板。
`acknowledge` 用于显式全刷新之后同步基线；选择变化仍可由原 UI 回调立即处理。

AYEditor 的动画页已完整使用公共曲线画布/DopeSheet，并共享同一 source。
UI Layout 的现有 `LayoutAnimationTimelineView` 复用公共 TimeViewport 和手势阶段
顺序，保留毫秒 API、垂直滚动及现有 Bezier/Spring 参数编辑；并未强制替换成新控件。

第一版 CurveCanvas 展示最多四个分量（X/Y/Z/W），单轨框选/批量变换；DopeSheet
为固定行高的可见轨道区域，支持跨行框选/组移动和选中高亮，批量移动由 owner 的
`transformKeys` 原子执行；暂无专用垂直滚动、公共剪贴板或通用 Bezier 手柄。
控件不注册为运行时 JSON Widget 类型，也不要求普通游戏链接作者目标。

验证：`AYUICurveEditorTest` 不链接 AYEditor/AYAnimation，覆盖非动画 owner、任意事件 ID、
秒制变换、采样缓存、共享选择和事务取消；`AYEditor_UnitTests` 验证动画快照/Undo/
Quaternion 正式采样，`AYUI_UnitTests` 保持 Layout 时间轴既有回归。
