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

### Fixed

- 文本编辑按 Unicode 字素边界处理 caret、选择和删除，并统一严格 UTF-8 转换。
- Theme 继承、命名纹理更新和 RichText 最后一行排版的缓存失效路径补齐。
- 复杂控件路径继续共用 retained display-list、合批与即时绘制兜底，不复制渲染实现。
- `ActiveScope` 只在仍持有全局 active slot 时恢复旧 manager，避免 shutdown/显式切换被作用域退出覆盖。
- 四角渐变在 damage/普通 clip 下按原 bounds 重映射颜色，局部重放不再重启或拉伸渐变。

### Validation

- Windows Debug：AYUI `4609 / 4609` 条断言通过。
- AYRenderer Noop：`3284 / 3284` 条断言通过（含局部 damage、采样键、224 次离屏 pass 边界、resize 与
  运行时 MSAA reset 后的 Layer 重绘）。
- AYFont `112 / 112` 条断言通过。
- Gallery 十条 GPU 路径在 `OrderedRuns` 与 `OverlapAware` 下逐像素一致。
- Layer 真实图像矩阵在 Auto 与显式 D3D11、1.0×/1.5× 下通过；相同 1280×720 DIP 画布使用
  1280×720/1920×1080 framebuffer，immediate/full/clean/partial 覆盖
  checker、alpha sprite、atlas、gradient、text 和 stencil path clip；语义差异不超过 1 RGBA8 LSB，
  full repaint 与 clean retained reuse 字节完全一致。

## v1.6 capability milestone - 2026-08-28

- Retained display-list 第一阶段、RenderTarget/Layer 接口与 Mock 生命周期完成。
- Serializer 覆盖 40 个注册控件类型；高级裁剪和矢量路径进入统一命令流。
- DPI/UI scale、无障碍语义、主题继承、Tab overflow、RichText 排版和 POSIX Clipboard 完成
  产品化第一阶段。
- Windows UI Automation、Unicode grapheme/Bidi/断行与 HarfBuzz 多字体 shaping 完成第二阶段。
