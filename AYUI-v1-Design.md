# AYUI v1 架构方案

> **Superseded for planning purposes by [`design.md`](design.md) (v1.3, 2026-07).**  
> This document remains as v1.2 reference; paths mentioning `AliyatRenderer` and unchecked phases may be outdated.

**版本：** v1.2
**日期：** 2026/05/28
**状态：** 核心完成，序列化/性能测试已添加

---

## 1. 概述

AYUI 是一个游戏引擎级别的 UI 框架，支持游戏内 UI 和编辑器 UI 两种场景。

### 设计目标

- **数据驱动** - UI 配置与逻辑分离，支持热重载
- **事件桥接** - 内部冒泡事件与外部引擎事件系统解耦
- **零依赖渲染** - 渲染层抽象，可绑定任意渲染器
- **最小编译冲击** - 基础层稳定，业务层可通过 JSON 热重载

---

## 2. 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                     JSON 配置文件                           │
│              (可热重载，无需编译)                            │
├─────────────────────────────────────────────────────────────┤
│                   UILayoutLoader                            │
│              (解析配置，构建 Widget 树)                      │
├─────────────────────────────────────────────────────────────┤
│                  Widget Factory                             │
│              (控件注册与创建)                               │
├─────────────────────────────────────────────────────────────┤
│                      Widget 树                              │
│               (C++ 对象，运行使用)                           │
├─────────────────────────────────────────────────────────────┤
│                    事件系统                                  │
│  ┌─────────────────┐     ┌─────────────────────────────┐   │
│  │   内部事件      │     │        外部事件              │   │
│  │   (冒泡传播)    │ ←→  │   (EventBridge 桥接)        │   │
│  └─────────────────┘     └─────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    渲染抽象层                               │
│                 (绑定 bgfx)                                 │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. 配置文件方案

### 3.1 方案对比

| 方案 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| **AliyatRenderer AYConfigWrapper** | 与引擎统一，可复用配置系统 | 依赖引擎 | ⭐⭐⭐ |
| **Boost.PropertyTree** | 轻量，跨平台，解析速度快 | 额外依赖 | ⭐⭐ |
| **RapidJSON** | 高性能，支持 JSON Schema | 手写解析 | ⭐⭐ |
| **nlohmann/json** | 现代 C++，易用性好 | 编译慢 | ⭐⭐⭐ |

### 3.1 已有库分析

| 库 | 用途 | AYUI 可用性 |
|----|------|-------------|
| **nlohmann/json** | 序列化 | ✅ 直接使用（无依赖耦合） |
| **Boost.PropertyTree** | 配置管理 | ⚠️ 需通过引擎获取，增加耦合 |
| **FreeType** | 字体渲染 | ✅ 直接使用（UI 需要） |
| **glm** | 数学 | ✅ AYMath 已集成 |
| **bgfx** | 渲染 | ✅ 目标渲染后端 |
| **spdlog** | 日志 | ✅ 可直接使用 |

---

---

### 3.2 推荐方案

**采用 nlohmann/json**，理由：

1. 项目已有，无需额外依赖
2. JSON 人类可读，易于调试和版本控制
3. 工具链完善，支持 Schema 验证
4. 现代 C++ 接口，易用性好
5. UI 配置非高频操作，解析性能足够

### 3.3 配置文件格式

```json
{
    "type": "Window",
    "id": "pause_menu",
    "position": { "x": 100, "y": 100 },
    "size": { "w": 400, "h": 300 },
    "title": "暂停",
    "closable": true,
    "children": [
        {
            "type": "VBox",
            "spacing": 10,
            "children": [
                {
                    "type": "Button",
                    "id": "btn_resume",
                    "text": "继续游戏",
                    "size": { "w": 200, "h": 40 },
                    "onClick": "resume_game"
                }
            ]
        }
    ]
}
```

---

## 4. 事件系统

### 4.1 设计原则

**AYUI 内部事件稳定，外部桥接灵活适配。**

- AYUI 内部事件（冒泡传播）是核心，与引擎解耦
- 外部引擎事件系统是演化中的，EventBridge 需要灵活适配
- 后续 AYUI 全面替换时，引擎事件可能重新设计以更好适配

### 4.2 双层事件架构

```
┌─────────────────────────────────────────────────────────────┐
│                  AliyatRenderer EventSystem                 │
│        (演化中的事件系统，会随 AYUI 实现调整)                  │
│                                                             │
│   Event_MouseButtonDown / Event_UIMouseClick               │
└─────────────────────────────────────────────────────────────┘
                            │
                            ↓ EventBridge (灵活适配层)
                            │
┌─────────────────────────────────────────────────────────────┐
│                    内部 UI 事件                             │
│                 (冒泡传播，子→父)                          │
│                                                             │
│   ClickEvent / HoverEvent / FocusEvent / SizeChangedEvent   │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 内部事件（冒泡传播）

| 事件类型 | 说明 | 传播方式 |
|----------|------|----------|
| ClickEvent | 鼠标点击 | 冒泡到根 |
| HoverEvent | 鼠标进入/离开 | 冒泡到根 |
| FocusEvent | 焦点变化 | 冒泡到根 |
| SizeChangedEvent | 尺寸变化 | 向下传播 |
| VisibilityChangedEvent | 显示/隐藏 | 向下传播 |

### 4.3 外部事件（引擎事件系统）

| 事件类型 | 来源 | 用途 |
|----------|------|------|
| Event_MouseButtonDown | 输入系统 | 触发 UI 命中测试 |
| Event_MouseMove | 输入系统 | 触发 Hover 事件 |
| Event_KeyDown/Up | 输入系统 | 键盘导航 |
| Event_UIMouseClick | 引擎 UI 层 | 直接派发到控件 |

### 4.4 EventBridge 实现

```cpp
class EventBridge {
public:
    EventBridge(EventSystem* engineEvents, UIManager* ui);

    // 引擎事件 → UI 事件（适配层，灵活处理引擎事件变化）
    void onEngineMouseClick(Event_UIMouseClick& e);
    void onEngineMouseMove(Event_MouseMove& e);

    // UI 事件 → 引擎事件（可选，用于日志/调试）
    void onUIClicked(Widget* target, const Float2& pos);

private:
    EventSystem* m_engineEvents;
    UIManager* m_ui;
};
```

### 4.5 演进说明

- AliyatRenderer 现有 UI 事件系统与 AYUI 耦合实现
- AYUI 稳定后，将全面替换现有实现
- 引擎事件系统可能随之调整以更好适配 AYUI

---

## 5. 核心组件

### 5.1 控件层

```
Widget (基类)
├── CompoundWidget (容器基类)
│   ├── Window
│   ├── VBox / HBox
│   └── UserWidget (可数据驱动)
├── Button
├── TextLabel
├── Image
├── SpatialWidget (3D 空间控件基类)
│   └── UIPlane (3D 面片 UI)
```

### 5.2 布局系统

| 布局器 | 说明 |
|--------|------|
| VBox | 垂直布局 |
| HBox | 水平布局 |
| GridPanel | 网格布局 |
| RelativeLayout | 相对定位 |

### 5.3 渲染层（IRenderBackend）

**设计原则：UI 调用 RenderBackend，而非 RenderBackend 调用 UI。**

```
┌─────────────────┐
│   UI 控件层      │  ← Widget 调用 RenderBackend
├─────────────────┤
│  IRenderBackend │  ← 渲染器接口（接口由 UI 定义）
├─────────────────┤
│ AYUIRenderBackend │  ← AliyatRenderer 实现
├─────────────────┤
│      bgfx        │  ← 实际渲染后端
└─────────────────┘
```

**接口能力分层（18个类别）：**

| 类别 | 方法 | 说明 |
|------|------|------|
| 1. 状态管理 | beginFrame/endFrame, beginCanvas/endCanvas, setBlendMode | 帧控制和混合模式 |
| 2. 变换与裁剪 | pushTransform/popTransform, pushClip/popClip, setScissor | 嵌套控件的旋转变换和裁剪区域 |
| 3. 基础绘制 | drawRect(纯色), drawRect(纹理) | 基础图元绘制 |
| 4. 文字渲染 | drawText, TextStyle | 描边、阴影、多行对齐 |
| 5. 九宫格 | drawNinePatch | 可拉伸背景图 |
| 6. 渐变 | drawGradientRect | 线性/径向渐变 |
| 7. 边框与圆角 | BorderStyle, drawRect, drawBorderRect | 圆角边框效果 |
| 8. 阴影 | ShadowStyle, drawRectShadow | 卡片/弹窗投影 |
| 9. 遮罩与混合 | pushMask/popMask, drawWithAlpha | 圆形头像等效果 |
| 10. 图集/精灵 | drawSprite | 精灵图绘制 |
| 11. 路径裁剪 | PathHandle, createPath, addPath*, drawPath, pushPathClip | 任意形状裁剪/描边/填充 |
| 12. 模糊效果 | drawRectBlur, drawRectBlurWithMask | 高斯/盒式/运动模糊 |
| 13. 粒子系统 | ParticleHandle, createParticleSystem, addParticle, drawParticleSystem | 内置粒子效果 |
| 14. 渲染目标 | RenderTargetHandle, createRenderTarget, bindRenderTarget | 离线渲染到纹理 |
| 15. 动画系统 | AnimationHandle, createAnimation*, updateAnimation, getAnimationValue* | 内置过渡动画 |
| 16. 批处理优化 | flush, getDrawCallCount, getTriangleCount, getVertexCount | 性能优化支持 |
| 17. 字体管理 | FontHandle, loadFont, releaseFont, getFontHandle, registerFontFromMemory | 字体资源管理 |
| 18. 度量与调试 | measureText, getFontMetrics, getAvailableVideoMemory, getDriverVersion | 文本度量和调试信息 |

```cpp
class IRenderBackend {
public:
    //========================================
    // 1. 渲染状态管理
    //========================================
    virtual void beginFrame() {}
    virtual void endFrame() {}
    virtual void beginCanvas(const math::FRectangle& viewport) {}
    virtual void endCanvas() {}
    enum class BlendMode { Normal, Additive, Multiply, Screen };
    virtual void setBlendMode(BlendMode mode) {}

    //========================================
    // 2. 变换与裁剪
    //========================================
    virtual void pushTransform(const math::Float4x4& transform) {}
    virtual void popTransform() {}
    virtual void pushClip(const math::FRectangle& bounds) {}
    virtual void popClip() {}
    virtual void setScissor(const math::FRectangle& bounds) {}

    //========================================
    // 3. 基础绘制原语
    //========================================
    virtual void drawRect(const math::FRectangle& bounds, const math::FVector4& color) = 0;
    virtual void drawRect(const math::FRectangle& bounds, void* textureHandle, const math::FRectangle& uv) = 0;

    //========================================
    // 4. 文字渲染
    //========================================
    struct TextStyle {
        math::FVector4 color = math::FVector4(1, 1, 1, 1);
        math::FVector4 outlineColor;
        float outlineWidth = 0;
        math::FVector4 shadowColor;
        math::FVector2 shadowOffset;
        float shadowBlurRadius = 0;
        int letterSpacing = 0;
        int lineSpacing = 0;
        enum class Align { Left, Center, Right };
        Align align = Align::Left;
    };
    virtual void drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize, const FVector4& color) = 0;
    virtual void drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize, const TextStyle& style);

    //========================================
    // 5. 9-patch（九宫格）
    //========================================
    virtual void drawNinePatch(const math::FRectangle& bounds, void* textureHandle,
                               const math::FRectangle& uvRegion, const math::FVector4& padding) {}

    //========================================
    // 6. 渐变
    //========================================
    enum class GradientType { Linear, Radial };
    virtual void drawGradientRect(const math::FRectangle& bounds,
                                  const math::FVector4& topLeft, const math::FVector4& topRight,
                                  const math::FVector4& bottomLeft, const math::FVector4& bottomRight) {}
    virtual void drawGradientRect(const math::FRectangle& bounds,
                                  const math::FVector4& topColor, const math::FVector4& bottomColor);

    //========================================
    // 7. 边框与圆角
    //========================================
    struct BorderStyle {
        math::FVector4 color = math::FVector4(1, 1, 1, 1);
        float width = 1.0f;
        float cornerRadius = 0;
        enum class Position { Outside, Inside, Center };
        Position position = Position::Outside;
    };
    virtual void drawRect(const math::FRectangle& bounds, const BorderStyle& border);
    virtual void drawBorderRect(const math::FRectangle& bounds, const math::FVector4& color, float borderWidth, float cornerRadius = 0);

    //========================================
    // 8. 阴影
    //========================================
    struct ShadowStyle {
        math::FVector4 color = math::FVector4(0, 0, 0, 0.5f);
        math::FVector2 offset = math::FVector2(2, 2);
        float blurRadius = 4.0f;
        float cornerRadius = 0;
    };
    virtual void drawRectShadow(const math::FRectangle& bounds, const ShadowStyle& shadow);

    //========================================
    // 9. 遮罩与混合
    //========================================
    virtual void pushMask() {}
    virtual void popMask() {}
    virtual void drawWithAlpha(const math::FRectangle& bounds, void* textureHandle, float alpha) = 0;

    //========================================
    // 10. 图集/精灵
    //========================================
    virtual void drawSprite(const math::FRectangle& bounds, void* atlasTexture, const wchar_t* spriteName) {}

    //========================================
    // 11. 批处理优化（内部使用）
    //========================================
    virtual void flush() {}
    virtual int getDrawCallCount() const { return 0; }

    //========================================
    // 12. 字体管理
    //========================================
    struct FontHandle { int id = -1; };
    virtual FontHandle loadFont(const wchar_t* path, int baseSize) { return FontHandle{-1}; }
    virtual void releaseFont(FontHandle font) {}

    //========================================
    // 13. 路径与蒙版（高级）
    //========================================
    struct PathHandle { int id = -1; };
    virtual PathHandle createPath() { return PathHandle{-1}; }
    virtual void addPathLine(PathHandle path, const math::FVector2& from, const math::FVector2& to) {}
    virtual void addPathRect(PathHandle path, const math::FRectangle& bounds, float cornerRadius = 0) {}
    virtual void addPathCircle(PathHandle path, const math::FVector2& center, float radius) {}
    virtual void closePath(PathHandle path) {}
    virtual void usePathAsClip(PathHandle path) {}
    virtual void deletePath(PathHandle path) {}

    //========================================
    // 14. 模糊效果
    //========================================
    virtual void drawRectGaussianBlur(const math::FRectangle& bounds, float radius, int passes = 1) {}

    //========================================
    // 15. 动画系统
    //========================================
    struct AnimationHandle { int id = -1; };
    virtual AnimationHandle createAnimation(const std::string& name, float duration) { return AnimationHandle{-1}; }
    virtual void animateFloat(AnimationHandle anim, const char* property, float from, float to) {}
    virtual void animateColor(AnimationHandle anim, const char* property, const math::FVector4& from, const math::FVector4& to) {}
    virtual void onAnimationComplete(AnimationHandle anim, std::function<void()> callback) {}
    virtual void playAnimation(AnimationHandle anim) {}
    virtual void stopAnimation(AnimationHandle anim) {}

    //========================================
    // 16. 渲染目标管理
    //========================================
    struct RenderTargetHandle { int id = -1; };
    virtual RenderTargetHandle createRenderTarget(int width, int height, bool hasDepth = false) { return RenderTargetHandle{-1}; }
    virtual void setRenderTarget(RenderTargetHandle target) {}
    virtual void clearRenderTarget(const math::FVector4& color) {}
    virtual void deleteRenderTarget(RenderTargetHandle target) {}
    virtual TextureHandle getTargetTexture(RenderTargetHandle target) { return TextureHandle{-1}; }

    //========================================
    // 17. 渲染状态重置
    //========================================
    virtual void resetState() {}
};
```

**Widget 渲染流程：**
```cpp
void Widget::render(IRenderBackend& renderer) {
    if (!_visible) return;
    onRender(renderer);         // 子类实现具体绘制
    for (Widget* child : _children) {
        child->render(renderer); // 递归绘制子控件
    }
}

void Button::onRender(IRenderBackend& renderer) {
    // 组合多个绘制调用实现丰富样式
    renderer.drawRectShadow(getBounds(), _shadowStyle);
    renderer.drawRect(getBounds(), _borderStyle);
    renderer.drawText(getTextBounds(), _text.c_str(), _fontSize, _textColor);
}
```

**实现位置：**
- `AYUI/Style/AYIRenderBackend.h` - 接口定义
- `AYUI/Style/AYMockRenderer.h/cpp` - 测试用 mock 实现
- `AliyatRenderer/.../AYUIRenderBackend.h/cpp` - 实际渲染器实现（绑定到 UIRenderer）

---

## 6. 3D 空间 UI 集成

### 6.1 设计理念

AYUI 采用**分离式架构**，UI 渲染独立于 3D 场景，但可以灵活借用 3D 渲染资源。

```
┌────────────────────────────────────────────────────────────┐
│                 AYUI (独立 UI 渲染)                      │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐             │
│  │ Widgets │───▶│UIRenderer│───▶│ UITexture│             │
│  │  Tree   │    │         │    │ (输出)   │             │
│  └─────────┘    └─────────┘    └────┬────┘             │
│       │               │             │                   │
│       ▼               ▼             ▼                   │
│  ┌─────────┐    ┌─────────┐   ┌─────────┐              │
│  │Backend  │    │ BGFX/   │   │ 3D Scene│              │
│  │(抽象)   │    │ D3D11   │   │ Composte│              │
│  └─────────┘    └─────────┘   └─────────┘              │
└────────────────────────────────────────────────────────────┘
       │               │             │
       └───────────────┴─────────────┘
              共享渲染后端 (bgfx/D3D11)
```

### 6.2 资源共享

| 资源 | 3D 借用 UI | UI 借用 3D |
|------|-----------|------------|
| 纹理 | ✓ (3D 模型用 UI 贴图) | ✓ (UI 用 3D 生成的纹理) |
| 字体 | ✗ | ✓ (可复用 3D 字体渲染) |
| 着色器 | ✓ | ✓ (UI 特效借用 3D Shader) |
| 渲染状态 | ✓ | ✓ |

### 6.3 3D Spatial Widget

```cpp
// 3D 空间中的控件基类
class SpatialWidget : public Widget {
public:
    math::Vector3 worldPosition;
    math::Quaternion rotation;
    float scale = 1.0f;

    void attachTo(Transform* target);
    virtual void onTransformChanged();
};

// UI 面片 - 在 3D 空间显示 UI
class UIPlane : public SpatialWidget {
public:
    math::Vector2 size;           // 3D 尺寸（米）
    math::Vector2 resolution;     // 渲染分辨率

    Widget* content;             // UI 内容
    Texture2D* renderTarget;     // 渲染结果的纹理

    bool faceCamera;             // 是否始终朝向相机
    bool lockedToWorld;          // 锁定后不跟随玩家

    void setContent(Widget* widget);
    virtual void onTransformChanged() override;
};

// 世界空间 UI 管理器
class WorldUI {
public:
    void addPlane(UIPlane* plane);
    void removePlane(UIPlane* plane);
    void update(const Camera& camera);

    // 将屏幕坐标投射到 3D 空间
    Vector3 screenToWorld(const Vector2& screenPos, float depth);

    // 射线检测（用于点击 3D UI）
    Widget* hitTest(const Ray& ray);
};
```

### 6.4 使用示例

```cpp
// 创建跟随玩家头部的 UI
UIPlane* healthBar = new UIPlane();
healthBar->setSize(Vector2(0.5f, 0.1f));  // 50cm x 10cm
healthBar->setResolution(Vector2(256, 64));
healthBar->content = createHealthBarUI();
healthBar->faceCamera = true;
healthBar->attachTo(player->headBone);
worldUI.addPlane(healthBar);

// 创建固定在世界中的 UI 面片
UIPlane* sign = new UIPlane();
sign->setPosition(Vector3(10.0f, 2.0f, 0.0f));
sign->setRotation(Quaternion::euler(0, 90, 0));
sign->setSize(Vector2(2.0f, 1.0f));
sign->content = createSignUI();
sign->lockedToWorld = true;
worldUI.addPlane(sign);
```

### 6.5 渲染流程

```
┌────────────────────────────────────────────────────────────┐
│                    Render Pipeline                        │
├────────────────────────────────────────────────────────────┤
│                                                             │
│  1. UI Render Pass                                         │
│     ┌──────────┐    ┌──────────┐    ┌──────────────┐     │
│     │ Widgets  │───▶│UIRenderer│───▶│ RenderTarget │     │
│     │(Button..)│    │          │    │  (512x512)   │     │
│     └──────────┘    └──────────┘    └──────┬───────┘     │
│                                             │              │
│  2. 3D Scene Pass                          │              │
│     ┌──────────┐    ┌──────────┐           │ texture      │
│     │  3D Model│───▶│  Scene   │           │              │
│     └──────────┘    └────┬─────┘           ▼              │
│                          │    ┌──────────────┐            │
│                          └────│   Composite │            │
│                               └──────────────┘            │
│                                                             │
└────────────────────────────────────────────────────────────┘
```

---

## 7. 开发计划

### Phase 1: 核心框架
- [x] Widget 基类与控件树
- [x] 基础控件实现（Button, TextLabel, Image, Window）
- [x] VBox/HBox 布局器
- [x] 内部事件冒泡机制
- [x] MockRenderer 用于测试
- [x] MouseEvent 简化（widgetPos 由控件自己计算）
- [x] Hover/Leave 事件跟踪机制
- [x] AYFont 字体接口层

### Phase 1.5: 字体系统
- [x] AYFont 静态库（FreeType + HarfBuzz 实现）
- [x] IRenderBackend 字体管理接口（loadFont/releaseFont）
- [x] TextLabel 字体渲染集成（_fontFamily, onRender）

### Phase 2: 配置系统 + 国际化
- [x] nlohmann/json 集成
- [x] UILayoutLoader 实现
- [x] WidgetFactory 注册系统
- [x] 样式系统（混合方案）
- [x] 国际化系统（i18n）
- [x] 热重载机制（isReloadNeeded/tryReload）
- [x] Widget 序列化/反序列化（WidgetSerializer）

### Phase 3: 事件桥接
- [ ] EventBridge 实现
- [ ] 绑定 AliyatRenderer 事件系统
- [ ] 输入事件分发

### Phase 4: 渲染集成
- [x] IRenderBackend 接口定义（扩展完善）
- [x] MockRenderer 实现（测试用）
- [x] Widget 支持 IRenderBackend 调用
- [x] AYFont 字体接口层（IFontManager/Font/IGlyphRenderer）
- [ ] AYUIRenderBackend 实现（绑定 AliyatRenderer UIRenderer）
- [ ] FreeTypeFontManager 实现（Ayfont/IFontManager）
- [ ] FreeTypeGlyphRenderer 实现（Ayfont/IGlyphRenderer）
- [ ] 纹理/图集支持
- [ ] 路径裁剪支持
- [ ] 模糊效果
- [ ] 动画系统
- [ ] 渲染目标管理

### Phase 5: 3D 空间 UI
- [ ] SpatialWidget 基类
- [ ] UIPlane 实现
- [ ] WorldUI 管理器
- [ ] 3D 命中测试（射线检测）
- [ ] 相机跟随功能

---

## 8. AYFont 字体管理层

### 8.1 设计理念

AYFont 提供字体管理接口层，不包含实际渲染实现。渲染由 `IGlyphRenderer` 接口负责。

```
┌─────────────────────────────────────────────────────────────┐
│                     AYFont (接口层)                        │
│  ┌─────────────────┐  ┌─────────────────┐  ┌────────────┐ │
│  │  IFontManager   │  │      Font       │  │IGlyphRenderer│ │
│  │  (字体管理器)   │  │   (字体对象)    │  │ (字形渲染)  │ │
│  └────────┬────────┘  └────────┬────────┘  └─────┬──────┘ │
│           │                    │                   │        │
│           ▼                    ▼                   ▼        │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              实际实现 (在 AliyatRenderer)               │ │
│  │  FreeTypeFontManager  │  FreeTypeGlyphRenderer (GL/BGFX) │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### 8.2 接口层次

| 接口 | 职责 | 方法 |
|------|------|------|
| `IFontManager` | 字体注册、查找、生命周期 | `registerFont()`, `getFont()`, `releaseFont()` |
| `Font` | 字形缓存、度量查询 | `getGlyph()`, `measureText()`, `getMetrics()` |
| `IGlyphRenderer` | 字形光栅化、纹理图集管理 | `loadGlyph()`, `getAtlasTexture()` |

### 8.3 AYFont 与 AYUI 关系

- AYUI 的 `IRenderBackend` 使用 `ayt::font::FontHandle`
- `TextStyle` 可指定 `FontHandle` 实现每个 Text 独立字体
- 实际字体加载通过 `IFontManager` 实现

### 8.4 目录结构

```
AYFont/
├── AYFont.h                    # 主头文件
├── IFontManager.h              # 字体管理器接口
├── Font.h                      # 字体对象接口
├── IGlyphRenderer.h            # 字形渲染器接口
├── AYMockFontManager.h/cpp     # Mock 实现（测试用）
└── unittest/
```

---

## 9. 目录结构

```
AYUI/
├── AYUI.h                    # 主头文件
├── Controls/
│   ├── AYWidget.h/cpp         # 基类（含 render/IRenderBackend）
│   ├── AYButton.h/cpp
│   ├── AYTextLabel.h/cpp
│   ├── AYImage.h/cpp
│   ├── AYWindow.h/cpp
│   └── AYCompoundWidget.h
├── Layout/
│   └── AYBox.h/cpp            # VBox/HBox
├── Spatial/
│   ├── AYSpatialWidget.h/cpp  # 3D 空间控件基类
│   └── AYUIPlane.h/cpp        # 3D 面片 UI
├── Style/
│   ├── AYIRenderBackend.h     # 渲染器接口（UI 定义）
│   ├── AYMockRenderer.h/cpp   # 测试用 mock 实现
│   └── AYStyle.h/cpp          # 样式系统（混合方案）
├── Events/
│   ├── AYEventBridge.h/cpp    # 事件桥接
│   └── AYUIEvent.h            # UI 内部事件定义
├── Loader/
│   ├── AYLayoutLoader.h/cpp  # JSON 加载器
│   ├── AYWidgetFactory.h      # 控件工厂注册
│   └── AYWidgetSerializer.h/cpp # Widget 序列化/反序列化
├── i18n/
│   ├── AYI18n.h/cpp           # 国际化系统
│   └── LangTable.json         # 语言表
└── unittest/

AliyatRenderer/
└── src/Core/Renderer/
    └── include/BaseRendering/UI/
        └── AYUIRenderBackend.h/cpp  # 实际渲染器实现
```

---

## 9. 待决策项

### 9.1 已确认

| 决策项 | 结论 |
|--------|------|
| 配置文件格式 | JSON |
| JSON 解析库 | nlohmann/json |
| 事件架构 | 双层（内部冒泡 + 外部桥接） |
| 数学库 | 使用 AYMath |
| 渲染抽象 | IRenderBackend 接口 |
| 国际化 | v1 支持（key 引用机制） |
| 样式系统 | 混合方案（代码定义 + JSON 覆盖） |
| UI 架构 | 分离式（UI 独立，3D 借用渲染资源） |
| MouseEvent 设计 | 简化为单坐标（局部坐标由控件自己计算） |

### 9.2 像素级透明遮罩命中测试 (Pixel Mask Hit Test)

**概念：** 根据图片的 alpha 通道生成遮罩，使得只有图片有像素（alpha > 阈值）的区域才能响应鼠标事件。

**应用场景：**
- 不规则形状按钮（如圆角、自定义形状）
- 带透明背景的图标按钮
- 复杂轮廓的 UI 元素（如树叶、魔法效果等不规则形状）

**设计思路：**

```cpp
class Image : public CompoundWidget {
public:
    void setMaskMode(MaskMode mode);
    void setMaskThreshold(float threshold);  // alpha 阈值，默认 0.1f
    Widget* hitTest(const FVector2& worldPos) override;
    bool isPointInMask(float localX, float localY);

private:
    MaskMode _maskMode;
    float _maskThreshold;
};

enum class MaskMode {
    None,           // 无遮罩，矩形命中
    AlphaChannel,   // 使用纹理 alpha 通道
    CustomMask,      // 使用单独的遮罩纹理
};
```

### 9.3 样式系统详细设计

**代码定义基础样式：**
```cpp
// AYStyle.cpp
WidgetStyle StyleBuilder::makeButton() {
    WidgetStyle style = makeDefault();
    style.backgroundColor = Float4(0.3f, 0.3f, 0.3f, 1.0f);
    style.border.cornerRadius = 4.0f;
    style.padding = Float4(8.0f, 4.0f, 8.0f, 4.0f);
    return style;
}
```

**JSON 覆盖/引用：**
```json
{
    "type": "Button",
    "id": "btn_confirm",
    "style": "button_default",
    "text": "ui.btn.confirm"
}
```

**样式继承链：**
```
Global Style → Widget Type Default → Named Style → Inline Style
```

### 9.4 国际化（i18n）设计

**文本 Key 格式：** `ui.{section}.{key}` （如 `ui.menu.resume`）

**语言表结构：**
```json
{
    "ui.menu.resume": { "zh": "继续游戏", "en": "Resume" },
    "ui.menu.settings": { "zh": "设置", "en": "Settings" }
}
```

**加载流程：**
```
JSON 配置 → 检测文本是否以 "ui." 开头
    ↓ 是
国际化系统 → 查找当前语言对应的文本
    ↓ 否
直接使用原文
```

**语言切换：** 支持运行时切换，无需重新加载 UI

---

## 10. 参考

- Unreal Engine Slate 架构
- Dear ImGui 事件模型
- Egui (Rust) 数据驱动设计
- Unity UIElements 渲染架构