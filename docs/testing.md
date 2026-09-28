# UI 测试运行与维护

测试源码按 `unittest/TestGroups.cmake` 的 8 个领域分别编译；新增 `Test_*.cpp`
必须且只能分配到一个领域，否则配置失败。不要恢复文本包含 `.cpp` 或 unity build。
公共创作控件另由 `AYUICurveEditorTest` 覆盖。

## 日常入口

在根仓配置 `cmake --preset windows-ui-debug`，随后执行：

```powershell
pwsh -NoProfile -File scripts/tests/run-ui-tests.ps1
pwsh -NoProfile -File scripts/tests/run-ui-tests.ps1 -Group Docking
pwsh -NoProfile -File scripts/tests/run-ui-tests.ps1 -Tier full
pwsh -NoProfile -File scripts/tests/verify-ui-test-inventory.ps1
```

默认构建目录不包含 Renderer/Editor；UI 测试使用 MockRenderer，不需要 GPU。
可用 `-BuildDirectory out/build/windows-debug` 复用现有完整构建；`-SkipBuild`
只运行已有程序，`-List` 只查看注册项。过滤没有匹配项必须报错，不算通过。

单用例使用对应领域程序的公共入口，例如：

```text
AYUI_DockingTests --list
AYUI_DockingTests --suite AYUI_DockTree --case NAME
AYUI_DockingTests --verbose
```

## 验证层级

- `fast`：日常快速无窗口回归，保留常规边界与生命周期断言。
- `integration`：真实文件监听与热重载。
- `stress`：大输入与性能阈值测试，不混入普通快速运行。
- `full`：以上三个层级加创作控件，全量用例恰好执行一次；提交前和完整 CI 使用。
- `visual`：沿用完整 client 构建中的 `UIProductionVerticalSliceGoldenRegression`，
  单独使用真实窗口/GPU，不用 MockRenderer 结果代替视觉验收。

CTest 标签使用 `ui-fast/ui-integration/ui-stress/ui-full` 以及领域标签；每项都有
超时。`windows-ui-fast/full/integration/stress` 测试预设提供相同的分层。
`AYUI_UnitTests` 作为兼容聚合程序保留，但不再额外注册到 CTest，避免重复执行。
清单核验会比较源码用例与完整层的实际注册结果，遗漏、重复、拼错筛选都失败。

套件、用例数和断言数分别统计，不能把 `Total` 断言数当作用例覆盖率。
性能比较应标注构建配置、机器、是否包含编译及应用日志；不以减少断言数作为优化目标。
