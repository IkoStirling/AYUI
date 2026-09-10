# AYUI Changelog

本文件记录面向使用者的能力、兼容性和验证基线。CMake 版本是发布版本；design 中的 v1.x
里程碑只描述能力完成度。

## Unreleased

### Changed

- 明确 1.x 源码/API、JSON wire contract 与 C++ ABI 的边界。
- 公共控件注释与当前 overlay、typeahead、IME、文本测量及 overflow 实现对齐。
- `UIManager` 可显式启用 Production root UI Layer；静态主树 clean 帧只做 layer composite，
  overlay 与 drag visual 保持即时绘制，并在 capability/paint 失败时同帧回退。
- Production Layer 状态采用 out-of-line sidecar，不改变 `UIManager` 对象布局；AYRenderer 的 UI Layer
  与 FrameGraph 共用 renderer-wide RenderTargetPool。
- 显式 `Widget::markDirty(rect)` 会合并 damage 并传播到根节点；Production root Layer 只清除和
  replay 受损区域。无参 dirty、首次绘制、resize、DPI 和 device reset 仍保守全量重绘。
- UI 离屏调度扩展到 view 26–249：每帧最多 224 次 pass，第 225 次确定回退，下一帧恢复。
- UI RenderTarget 采样方式进入共享 pool 精确键；同尺寸 Layer 使用 point sampling，FrameGraph 默认
  线性目标不受影响。Layer 透明复合改为正确 coverage alpha + premultiplied-over。
- `AnimationTimeline` 增加有限/永久 repeat、yoyo 和每关键帧物理弹簧参数；累计时钟保证大帧间隔
  跨周期稳定，`AnimationSequence` 会把跨步骤后的剩余帧时间继续交给下一步。
- Layout Editor `PropertySchema` 增加编辑器种类、数值约束和枚举选项；Inspector 新增 Slider/
  ProgressBar、Image tint/UV、集合布局、滚动条策略、Tab overflow、Grid 与 RichText 类型化属性。
- 全部 51 个普通 Inspector 属性行改由 schema 显示名、section、行高和控件契约自动生成；
  `Padding` 四分量复合行也由 schema 描述，chrome JSON 只保留资源浏览、结构化内容等工具面板。
  旧 chrome 中已有的同 ID 行仍会被兼容复用。
- Layout Editor 增加 authoring-only Color/Resource/Vector 字段控件：Color 使用共享 `ColorPicker`
  popup 并合并连续编辑事务，Resource 内建 Browse/Clear，Vector 为复合分量保留稳定子控件 ID。
- `UIManager::findById()` 在 Loader 索引未命中时查询实时 Widget 树，覆盖运行时挂载控件。
- public authoring 控件与 schema 扩展后源码 ABI 版本更新为 115。
- Layout Editor 常规属性使用稳定 Widget ID 的 typed undo/redo command；源码 ABI 更新为 116。
- Layout Editor 接受宿主 Controller/Handler contract，并显示可定位 Widget 的事件触发图；源码 ABI
  更新为 117。
- 文档动画增加 CSS-compatible Cubic Bezier 参数和完整物理 Spring 参数创作；Inspector、时间轴预览、
  JSON 往返与运行时 Timeline 共用同一求值路径。
- `AnimationSettings` 增加可注入 reduced-motion provider，并在 Windows 默认桥接系统动画偏好；
  公共动画类型扩展后源码 ABI 更新为 118。
- 增加 Application UI Flow 阶段一纯数据契约：`*.uiflow.json` 可描述 Screen、Layer、Slot、Scope、
  Context、Entry、Signal/Action、并行层级状态机和可扩展 Action Graph；自定义节点的递归 JSON 属性
  可无损往返，交叉引用、层级循环、类型默认值与数值范围具有统一诊断。源码 ABI 更新为 119。

### Fixed

- 文本编辑按 Unicode 字素边界处理 caret、选择和删除，并统一严格 UTF-8 转换。
- Theme 继承、命名纹理更新和 RichText 最后一行排版的缓存失效路径补齐。
- 复杂控件路径继续共用 retained display-list、合批与即时绘制兜底，不复制渲染实现。
- `ActiveScope` 只在仍持有全局 active slot 时恢复旧 manager，避免 shutdown/显式切换被作用域退出覆盖。
- 四角渐变在 damage/普通 clip 下按原 bounds 重映射颜色，局部重放不再重启或拉伸渐变。
- Image tint/UV 和 Grid spacing 补齐 `UILayoutLoader`/`WidgetSerializer` 对称往返；Grid Inspector
  拒绝会丢弃已占用 cell 的缩小操作。

### Validation

- Windows Debug：AYUI `5147 / 5147` 条断言通过。
- VS 2026 Insider Windows Debug：AYUI `5660 / 5660`、AYRenderer `4333 / 4333`、
  AYEditor `2549 / 2549`、Default Editor Module Assembly `13 / 13`；Layout Editor headless
  round-trip 与 AYEditor level-4（三帧 GPU UI 合成及完整 shutdown）通过。
- VS 2026 Insider Windows Debug：Application UI Flow 契约加入后 AYUI `5697 / 5697` 通过。
- AYRenderer Noop：`4333 / 4333` 条断言通过（含局部 damage、采样键、224 次离屏 pass 边界、resize 与
  运行时 MSAA reset 后的 Layer 重绘）。
- AYFont `112 / 112` 条断言通过。
- Gallery 十条 GPU 路径在 `OrderedRuns` 与 `OverlapAware` 下逐像素一致。
- Layer 真实图像矩阵在显式 D3D11、D3D12、Vulkan、OpenGL 的 1.0×/1.5× 下通过；相同 1280×720 DIP 画布使用
  1280×720/1920×1080 framebuffer，immediate/full/clean/partial 覆盖
  checker、alpha sprite、atlas、gradient、text 和 stencil path clip；语义差异不超过 1 RGBA8 LSB，
  RGBA8 group opacity 不超过 2 LSB，clean retained reuse、isolated blend 与 Preserve 字节完全一致；
  四后端 root 提交基线均为 `28 / 29 / 1 / 11`。
- 独立 Designer、Editor Shell，以及从 Editor 顶部 `UI` 工具入口创建独立 Designer 子窗的窗口级
  截图验收通过；三栏布局、Canvas 选择边框/缩放柄、Inspector 和宿主 UI/Scene 合成均完整。

## v1.6 capability milestone - 2026-08-28

- Retained display-list 第一阶段、RenderTarget/Layer 接口与 Mock 生命周期完成。
- Serializer 覆盖 40 个注册控件类型；高级裁剪和矢量路径进入统一命令流。
- DPI/UI scale、无障碍语义、主题继承、Tab overflow、RichText 排版和 POSIX Clipboard 完成
  产品化第一阶段。
- Windows UI Automation、Unicode grapheme/Bidi/断行与 HarfBuzz 多字体 shaping 完成第二阶段。
