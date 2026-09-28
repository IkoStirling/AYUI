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
为固定行高的可见轨道区域，暂无专用垂直滚动、跨轨批量编辑、剪贴板或通用 Bezier 手柄。
控件不注册为运行时 JSON Widget 类型，也不要求普通游戏链接作者目标。

验证：`AYUICurveEditorTest` 不链接 AYEditor/AYAnimation，覆盖非动画 owner、任意事件 ID、
秒制变换、采样缓存、共享选择和事务取消；`AYEditor_UnitTests` 验证动画快照/Undo/
Quaternion 正式采样，`AYUI_UnitTests` 保持 Layout 时间轴既有回归。
