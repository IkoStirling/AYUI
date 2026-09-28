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

套件、用例数和断言数分别统计。AYTest 1.1 以用例通过/失败/跳过作为主报告，
断言数为辅助统计；不能把旧 `Total` 断言数或 CTest 分区数当作用例覆盖率。
2026-09-28 整理基线为 1208 个 UI 用例和 19 个创作控件用例，共 1227 个用例，
13 个完整层 CTest 分区；用例清单核验无遗漏、无重复。历史断言基线不代表当前用例数。
性能比较应标注构建配置、机器、是否包含编译及应用日志；不以减少断言数作为优化目标。

## 内容整理规则

测试文件按功能命名，历史阶段编号保留在注释和稳定 suite/case 标识中，不再用于
文件分组。当前整理不删除任何用例；生命周期、资源释放、UTF-8、旧 wire 格式及
复杂容器结构等专门断言不能被“通用往返通过”替代。

`unittest/fixtures/WidgetRoundTrip.h` 仅复用序列化往返和 Widget 树 RAII 清理，
具体行为仍由原用例断言。所有内置类型的基础 wire/factory 检查集中在
`Test_WidgetSerializerTypes.cpp` 的 42 项数据表；同时检查恢复后的具体类型。
新增内置类型应补齐此表，并添加它独有的字段/交互/所有权回归。

## 隔离与并行

普通分区默认允许 4 路进程并行；进程内不并行访问 Widget/UIManager。
压力/性能分区保持 `RUN_SERIAL`，防止测量与其他测试竞争资源。
文件监听、持久化与打包测试使用 `AYTestFixtures.h` 的 `ScratchDirectory`：
只清理独占创建的子目录，不复用或删除别的进程留下的目录。无 CTest 环境时，
临时根也按进程隔离；同一分区的多次并发调用也不共享相对路径文件。
CTest 临时根使用短分区标识，可从注册项的 `AY_TEST_TMPDIR` 查询对应关系。
文件监听使用 `waitUntil` 的 2 秒截止时间，不依赖固定睡眠。
真正访问原生剪贴板、共享窗口或 GPU 的新测试必须显式声明资源锁或串行策略。
