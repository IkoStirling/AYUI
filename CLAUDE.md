# AYUI 项目 AI 工作注意事项

> **注意**：AYUI 是独立项目，不受 AYMath/CLAUDE.md 约束。本文件定义 AYUI 专用规则。AYTest 是独立测试框架库，位于 `AYTest/CLAUDE.md`。

## 重要规则

1. **生成代码时不要使用 GBK 中文注释**
   - 代码中不要包含 GBK 编码的中文注释（会导致编译错误）
   - 尽量不要有中文，仅在用户明确要求添加中文注释时才添加
   - 如需中文注释，确保是 UTF-8 编码

2. **使用 AYTest 框架进行单元测试**
   - AYUI 使用 AYTest 框架（位于 D:/Projects/AYTest/）
   - 测试文件包含 `AYTest.h`，使用 `TEST_SUITE`、`TEST_CASE`、`CHECK_*` 等宏
   - 测试文件放在 AYUI/unittest/ 目录下

3. **可主动构建 + 跑测试**
   - 写完代码后可以主动运行 `cmake` / `msbuild` / `ninja` 编译并跑测试
   - 不必等用户同意

4. **缩短思考时间，及时结束会话**
   - 完成代码修改并确认逻辑后立即结束
   - 在回复结尾明确提醒用户自行运行验证

5. **新增任意内容后，同步新增测试代码**
   - 所有新增模块都需要对应的 unittest

## 架构决策（v1）

### 配置系统
- JSON 配置格式（**复用 `UILayoutLoader`**，见 `design.md` §4）
- 解析库：nlohmann/json（CMake `find_package`）
- UI 布局热重载：扩展 `UILayoutLoader::isReloadNeeded`（U1）；v1 不依赖 AYConfig

### 事件系统
- 双层事件架构：内部冒泡 + 外部桥接
- 内部事件：**`UIEvent` / `UIEventType`**（`AYUI/Widget.h`）；`include/AYUI/Event.h` 待删除
- EventBridge（U3）负责引擎输入 → `UIEvent`，见 `design.md` §5

### 国际化（i18n）
- 文本 key 格式：`ui.{section}.{key}`
- 语言表格式：`{ "key": { "zh": "中文", "en": "English" } }`
- 使用 `isI18nKey()` 检测是否为国际化 key

### 样式系统
- 混合方案：代码定义基础样式 + JSON 覆盖
- 使用 `WidgetStyle` 结构
- 通过 `setStyleId()` 引用样式

## 文件命名规范

- 文件前缀：`AY`（如 `AYUI/Widget.h`、`AYLayoutLoader.cpp`）
- 类名：**不使用前缀**（如 `class Widget`，`class Button`）
- 命名空间：`ayt::ui`

## 目录结构

```
AYUI/
├── Controls/          # 控件基类与具体控件
├── Layout/            # 布局器（VBox/HBox）
├── Style/             # 样式系统
├── Events/            # 事件桥接
├── Loader/            # JSON 加载器、控件工厂
├── i18n/              # 国际化
├── thirdparty/        # 第三方库（nlohmann/json.hpp）
└── unittest/          # 单元测试
```

## 验证方式

用户需手动执行：
```bash
cmake --build build --target AYMath_unittest
./build/unittest/AYMath_unittest
```

## 新增模块要求

### 新增控件
1. 在 Controls/ 创建 AY{控件名}.h/cpp
2. 继承自 Widget 或 CompoundWidget
3. 在 AYWidgetFactory.cpp 中注册
4. 在 unittest/ 中添加测试

### 新增系统（如 i18n）
1. 创建独立目录（如 i18n/）
2. 在 AYUI.h 中 include
3. 在 unittest/ 中添加测试

## 参考文档

- [AYUI-v1-Design.md](AYUI-v1-Design.md) - 架构设计文档（v1.2 归档）
- [design.md](design.md) - **当前权威设计**（v1.3）