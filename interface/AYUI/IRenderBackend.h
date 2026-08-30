#pragma once

#include "AYMath/MathTypes.h"
#include "AYMath/MathUtils.h"
#include "AYFont.h"
#include "AYUI/UnicodeText.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ayt::ui {

    using namespace ayt::font;

// =============================================================================
// R-9 (R12 fix): enums lifted to namespace scope
// -----------------------------------------------------------------------------
// BlendMode / PathFillMode / AnimationCurve used to be nested inside
// IRenderBackend. That forced call sites to spell out the full qualification
// (`IRenderBackend::BlendMode::Normal`) even when they were not invoking a
// backend method. R-9 promotes them to the `ayt::ui` namespace so the
// common call site reads `BlendMode::Normal` directly.
//
// Backward compatibility: each enum keeps a `using` alias inside
// IRenderBackend so the old `IRenderBackend::BlendMode::Normal` syntax
// still compiles. New code should prefer the namespace-scope form.
// =============================================================================

enum class BlendMode {
    Normal,    // Alpha blending
    Additive,  // SRC * SRC_ALPHA + DST * 1
    Multiply,  // SRC * DST
    Screen     // 1 - (1 - SRC) * (1 - DST)
};

enum class GradientType {
    Linear,  // colors interpolate along a line
    Radial   // colors radiate from the center
};

enum class PathFillMode { Fill, Stroke, FillAndStroke };

enum class PathWinding { CounterClockwise, Clockwise };

// Stroke geometry is part of the backend-independent path recipe.  SVG icon
// sets (including Tabler) rely on round caps/joins; leaving those choices in
// an individual backend changes the authored silhouette.
enum class PathStrokeCap { Butt, Round, Square };

enum class PathStrokeJoin { Miter, Round, Bevel };

enum class BlurType { Gaussian, Box, Motion };

enum class AnimationCurve { Linear, EaseIn, EaseOut, EaseInOut, Spring };

enum class AnimationFlags { None = 0, Reverse = 1, Loop = 2, PingPong = 4 };

// =============================================================================
// IRenderBackend - Industrial-Grade UI Rendering Backend Interface
// IRenderBackend - 工业级UI渲染后端接口
// =============================================================================
// This interface defines all rendering capabilities required for a complete UI system.
// All methods use a clean 2D coordinate system where Y increases downward.
// The backend handles batching, state management, and primitive optimization.
// 此接口定义了完整UI系统所需的所有渲染能力。所有方法使用Y轴向下的2D坐标系。
// 后端负责批处理、状态管理和图元优化。

class IRenderBackend {
public:
    // R-9: enums moved to namespace scope (see top of file). The aliases
    // below keep the old `IRenderBackend::BlendMode::Normal` syntax
    // compiling so existing call sites don't break.
    using BlendMode = ::ayt::ui::BlendMode;
    using GradientType = ::ayt::ui::GradientType;
    using PathFillMode = ::ayt::ui::PathFillMode;
    using PathWinding = ::ayt::ui::PathWinding;
    using PathStrokeCap = ::ayt::ui::PathStrokeCap;
    using PathStrokeJoin = ::ayt::ui::PathStrokeJoin;
    using BlurType = ::ayt::ui::BlurType;
    using AnimationCurve = ::ayt::ui::AnimationCurve;
    using AnimationFlags = ::ayt::ui::AnimationFlags;

    virtual ~IRenderBackend() = default;

    // =============================================================================
    // Category 1: Render State Management / 渲染状态管理
    // =============================================================================

    /*
       @name: beginFrame
       @func: 开始帧渲染 - 标记新帧开始，用于初始化渲染状态和清除累积数据
       @note: 每帧渲染前必须调用；绘制提交是 frame-local，调用方必须在
              每帧重放全部可见内容，不能假设上一帧指令或像素仍然有效
    */
    virtual void beginFrame() {}

    /*
       @name: endFrame
       @func: 结束帧渲染 - 标记帧渲染完成，用于提交批量渲染和资源回收
       @note: 每帧渲染后必须调用，用于执行待处理的渲染操作
    */
    virtual void endFrame() {}

    // Logical-DIP to framebuffer-pixel ratio for the current canvas.
    // Backends that rasterize directly in logical coordinates may keep the
    // default no-op. Production pixel backends should scale geometry and
    // font rasterization while keeping measureText results in logical DIP.
    virtual void setUiScale(float scale) { AYUNREFERENCED_PARAM(scale); }
    virtual float getUiScale() const { return 1.0f; }

    /*
       @name: beginCanvas
       @func: 开始画布渲染 - 设置视口区域，开始一个新的渲染目标
       @param viewport: 视口区域定义（单位：像素）
       @note: 所有后续渲染操作仅在指定视口区域内有效
    */
    virtual void beginCanvas(const math::FRectangle& viewport) {}

    /*
       @name: endCanvas
       @func: 结束画布渲染 - 关闭当前视口，完成当前渲染目标的绘制
       @note: 与beginCanvas配对使用
    */
    virtual void endCanvas() {}

    /*
       @name: setBlendMode
       @func: 设置混合模式 - 改变后续渲染的混合算法
       @param mode: 混合模式枚举值
       @note: 默认混合模式为Normal
    */
    virtual void setBlendMode(BlendMode mode) {}

    /*
       @name: pushOpacity
       @func: 压入全局不透明度 - 之后所有带颜色的绘制（矩形/文本/渐变/SDF）
              alpha 通道都乘以栈顶值。树状淡入淡出：父控件 push 0.5 后
              子控件再 push 0.5，最终 alpha = 原始 alpha × 0.25。
       @param alpha: 0.0（全透明）~ 1.0（不透明）。0/负值不按透明处理
                     （clamp 到 [0,1]，1.0 = 无效果 no-op）
       @note: 栈式（LIFO），与 pushClip 同构；必须与 popOpacity 成对调用
       @note: 默认栈底为 1.0（无效果），未实现的后端默认 no-op
    */
    virtual void pushOpacity(float alpha) {}

    /*
       @name: popOpacity
       @func: 弹出全局不透明度 - 恢复之前保存的不透明度状态
       @note: 必须与 pushOpacity 成对调用
    */
    virtual void popOpacity() {}

    // =============================================================================
    // Category 2: Transform and Clipping / 变换与裁剪
    // =============================================================================

    /*
       @name: pushTransform
       @func: 压入变换矩阵 - 保存当前变换状态并应用新变换
       @param transform: 4x4变换矩阵（通常为3D变换，但UI仅使用x,y坐标）
       @note: 变换栈为LIFO结构，支持嵌套变换
       @note: 典型用途：实现控件缩放、旋转、父子控件空间变换
    */
    virtual void pushTransform(const math::Float4x4& transform) {}

    /*
       @name: popTransform
       @func: 弹出变换矩阵 - 恢复之前保存的变换状态
       @note: 必须与pushTransform成对调用
    */
    virtual void popTransform() {}

    /*
       @name: pushClip
       @func: 压入裁剪区域 - 保存当前裁剪状态并设置新的裁剪区域
       @param bounds: 裁剪边界矩形（屏幕坐标）
       @note: 裁剪区域外的渲染内容将被丢弃
       @note: 支持裁剪区域嵌套（clip inside clip）
    */
    virtual void pushClip(const math::FRectangle& bounds) {}

    /*
       @name: popClip
       @func: 弹出裁剪区域 - 恢复之前保存的裁剪状态
       @note: 必须与pushClip成对调用
    */
    virtual void popClip() {}

    /*
       @name: setScissor
       @func: 设置剪刀裁剪 - 设置矩形剪刀区域（与pushClip不同，这是立即模式）
       @param bounds: 剪刀边界矩形，设置为全零可禁用剪刀测试
       @note: 剪刀测试比裁剪更高效，但仅支持矩形
    */
    virtual void setScissor(const math::FRectangle& bounds) {}

    // =============================================================================
    // Category 3: Basic Drawing Primitives / 基础绘制图元
    // =============================================================================

    /*
       @name: drawRect (colored)
       @func: 绘制纯色矩形 - 在指定位置绘制填充矩形
       @param bounds: 矩形边界（left, top, right, bottom）
       @param color: 填充颜色（RGBA，范围0-1）
       @note: 纯色矩形是最基础的图元，用于背景、边框等
    */
    virtual void drawRect(const math::FRectangle& bounds, const math::FVector4& color) = 0;

    /*
       @name: drawRect (textured)
       @func: 绘制纹理矩形 - 在指定位置绘制带纹理的矩形
       @param bounds: 矩形边界
       @param textureHandle: 纹理句柄（平台相关，如OpenGL texture ID）
       @param uv: 纹理坐标区域（left, top, right, bottom in [0,1]）
       @note: uv参数支持纹理图集，可以只绘制纹理的指定区域
    */
    virtual void drawRect(const math::FRectangle& bounds, void* textureHandle, const math::FRectangle& uv) = 0;

    // =============================================================================
    // Category 4: Text Rendering / 文字渲染
    // =============================================================================

    /*
       @name: TextStyle
       @func: 文字样式结构体 - 定义文字渲染的所有样式参数
       @param color: 文字颜色（RGBA）
       @param outlineColor: 描边颜色（全0 = 无描边）
       @param outlineWidth: 描边宽度（像素）
       @param shadowColor: 阴影颜色（全0 = 无阴影）
       @param shadowOffset: 阴影偏移量（像素，x向右，y向下）
       @param shadowBlurRadius: 阴影模糊半径（像素）
       @param letterSpacing: 字间距调整（像素，正值增大间距）
       @param lineSpacing: 行间距调整（像素，正值增大间距）
       @param align: 文字水平对齐方式
       @param valign: 文字垂直对齐方式（Top/Middle/Bottom；Middle 与
                      简单 drawText 的居中行为一致）
    */
    struct TextStyle {
        math::FVector4 color = math::FVector4(1, 1, 1, 1);  // 文字颜色 / Text color
        math::FVector4 outlineColor;                          // 描边颜色 / Outline color
        float outlineWidth = 0;                              // 描边宽度 / Outline width
        math::FVector4 shadowColor;                          // 阴影颜色 / Shadow color
        math::FVector2 shadowOffset;                        // 阴影偏移 / Shadow offset
        float shadowBlurRadius = 0;                         // 阴影模糊半径 / Shadow blur radius
        int letterSpacing = 0;                              // 字间距调整 / Letter spacing adjustment
        int lineSpacing = 0;                                // 行间距调整 / Line spacing adjustment
        bool bold = false;                                  // 字重语义 / Bold semantic
        bool italic = false;                                // 斜体语义 / Italic semantic
        std::wstring fontFamily;                            // 空 = 后端默认字体 / backend default
        int fontWeight = 400;                               // CSS/OpenType 100..900
        TextDirection direction = TextDirection::Auto;      // shaping / visual direction
        std::string language;                               // optional BCP-47 shaping hint
        // 多行换行 / Multi-line wrapping: when true the styled drawText
        // wraps text to the bounds width (greedy word wrap, same rules as
        // measureText) and lays lines out at lineHeight + lineSpacing;
        // VAlign then positions the whole block. Default false keeps the
        // legacy single-line behavior (wider text clips at the bounds).
        bool wrapToBounds = false;
        enum class Align { Left, Center, Right };
        Align align = Align::Left;                          // 文字水平对齐 / Text horizontal alignment
        enum class VAlign { Top, Middle, Bottom };
        VAlign valign = VAlign::Middle;                     // 文字垂直对齐 / Text vertical alignment
    };

    /*
       @name: drawText (simple)
       @func: 绘制简单文字 - 使用基础颜色绘制单一样式文字
       @param bounds: 文字边界框（用于定位和对齐）
       @param text: 待绘制的Unicode字符串
       @param fontSize: 字体大小（像素高度）
       @param color: 文字颜色（RGBA）
    */
    virtual void drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize, const math::FVector4& color) = 0;

    /*
       @name: drawText (styled)
       @func: 绘制样式化文字 - 使用完整样式配置绘制文字
       @param bounds: 文字边界框（用于定位和对齐）
       @param text: 待绘制的Unicode字符串
       @param fontSize: 字体大小（像素高度）
       @param style: 文字样式配置（颜色、描边、阴影等）
       @note: 支持描边和阴影效果，可组合使用
    */
    virtual void drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize, const TextStyle& style);

    // =============================================================================
    // Category 5: Nine-Patch Rendering / 九宫格渲染
    // =============================================================================

    /*
       @name: drawNinePatch
       @func: 绘制九宫格图片 - 拉伸时保持边角的图片格式
       @param bounds: 目标绘制区域
       @param textureHandle: 纹理句柄
       @param uvRegion: 纹理中九宫格区域定义
       @param padding: 内边距（定义九宫格的边角区域大小）
       @note: 九宫格将图片分为9个区域，4个角保持原尺寸，4条边单方向拉伸，中心双方向拉伸
       @note: 常用于按钮、窗口等需要适应不同尺寸的UI元素
    */
    virtual void drawNinePatch(const math::FRectangle& bounds, void* textureHandle,
                               const math::FRectangle& uvRegion, const math::FVector4& padding) {}

    // =============================================================================
    // Category 6: Gradient Rendering / 渐变渲染
    // =============================================================================

    /*
       @name: drawGradientRect (4-color)
       @func: 绘制四色渐变矩形 - 每个角单独指定颜色
       @param bounds: 矩形边界
       @param topLeft: 左上角颜色
       @param topRight: 右上角颜色
       @param bottomLeft: 左下角颜色
       @param bottomRight: 右下角颜色
       @note: 颜色在矩形内双线性插值混合
    */
    virtual void drawGradientRect(const math::FRectangle& bounds,
                                  const math::FVector4& topLeft, const math::FVector4& topRight,
                                  const math::FVector4& bottomLeft, const math::FVector4& bottomRight) {}

    /*
       @name: drawGradientRect (2-color vertical)
       @func: 绘制垂直双色渐变矩形 - 上下分别指定颜色
       @param bounds: 矩形边界
       @param topColor: 顶部颜色
       @param bottomColor: 底部颜色
       @note: 颜色在矩形内垂直线性插值
    */
    virtual void drawGradientRect(const math::FRectangle& bounds,
                                  const math::FVector4& topColor, const math::FVector4& bottomColor);

    // =============================================================================
    // Category 7: Border and Corner Radius / 边框与圆角
    // =============================================================================

    /*
       @name: BorderStyle
       @func: 边框样式结构体
       @param color: 边框颜色
       @param width: 边框宽度（像素）
       @param cornerRadius: 圆角半径（像素，0=直角）
       @param position: 边框位置（Outside=向外扩展，Inside=向内收缩，Center=居中）
    */
    struct BorderStyle {
        math::FVector4 color = math::FVector4(1, 1, 1, 1);  // 边框颜色 / Border color
        float width = 1.0f;                                  // 边框宽度 / Border width
        float cornerRadius = 0;                               // 圆角半径 / Corner radius
        enum class Position { Outside, Inside, Center };
        Position position = Position::Outside;               // 边框位置 / Border position
    };

    /*
       @name: CornerRadii
       @func: 四角独立圆角半径结构体
       @param topLeft/topRight/bottomRight/bottomLeft: 各角半径（像素，0=直角）
       @note: 顺序 TL TR BR BL，与 shader u_radius 逐位对应；构造时可用
              CornerRadii(r) 取统一半径。非实现后端退化近似为最大角半径。
    */
    struct CornerRadii {
        float topLeft = 0;
        float topRight = 0;
        float bottomRight = 0;
        float bottomLeft = 0;
        CornerRadii() = default;
        CornerRadii(float uniformRadius)
            : topLeft(uniformRadius)
            , topRight(uniformRadius)
            , bottomRight(uniformRadius)
            , bottomLeft(uniformRadius)
        {
        }
        CornerRadii(float tl, float tr, float br, float bl)
            : topLeft(tl)
            , topRight(tr)
            , bottomRight(br)
            , bottomLeft(bl)
        {
        }
        bool isUniform() const
        {
            return topLeft == topRight && topRight == bottomRight
                && bottomRight == bottomLeft;
        }
        float maxRadius() const
        {
            return std::max(std::max(topLeft, topRight),
                            std::max(bottomRight, bottomLeft));
        }
    };

    /*
       @name: CardStyle
       @func: 卡片样式 - shadow+fill+stroke 三层一次提交
       @param fillColor: 填充颜色（a=0 无填充层）
       @param cornerRadius: 四角圆角半径（四层共享同一轮廓）
       @param borderColor: 描边颜色（a=0 无描边层）
       @param borderWidth: 描边宽度（像素，≤0 无描边层）
       @param borderPosition: 描边位置（Outside=环在矩形边缘外，Center=骑跨，
                              Inside=向内；默认 Center 与 drawBorderRect 视觉一致）
       @param shadowColor: 阴影颜色（a=0 无阴影层）
       @param shadowOffset: 阴影偏移（像素）
       @param shadowBlurRadius: 阴影模糊半径（像素）
       @note: 支持该 API 的后端一次 draw call 合成三层（SDF shader 单 pass）；
              默认实现按 shadow→fill→border 三次调用保持层序正确。
    */
    struct CardStyle {
        math::FVector4 fillColor = math::FVector4(0, 0, 0, 0);
        CornerRadii   cornerRadius;
        math::FVector4 borderColor = math::FVector4(0, 0, 0, 0);
        float          borderWidth = 0;
        BorderStyle::Position borderPosition = BorderStyle::Position::Center;
        math::FVector4 shadowColor = math::FVector4(0, 0, 0, 0);
        math::FVector2 shadowOffset = math::FVector2(0, 0);
        float          shadowBlurRadius = 0;
    };

    /*
       @name: drawRect (border)
       @func: 绘制带边框矩形 - 使用边框样式绘制矩形
       @param bounds: 矩形边界
       @param border: 边框样式配置
       @note: 边框位置影响bounds的实际覆盖区域
    */
    virtual void drawRect(const math::FRectangle& bounds, const BorderStyle& border);

    /*
       @name: drawBorderRect
       @func: 绘制简单边框矩形 - 快速绘制单色边框
       @param bounds: 矩形边界
       @param color: 边框颜色
       @param borderWidth: 边框宽度（像素）
       @param cornerRadius: 圆角半径（像素）
    */
    virtual void drawBorderRect(const math::FRectangle& bounds, const math::FVector4& color, float borderWidth, float cornerRadius = 0);

    /*
       @name: drawRoundedRect
       @func: 绘制圆角实心矩形（SDF 填充；cornerRadius=0 时退化为 drawRect）
       @param bounds: 矩形边界
       @param color: 填充颜色
       @param cornerRadius: 圆角半径（像素）
    */
    virtual void drawRoundedRect(const math::FRectangle& bounds, const math::FVector4& color,
                                 float cornerRadius = 0);

    /*
       @name: drawRoundedRect (per-corner)
       @func: 四角独立圆角半径的实心矩形（shader 原生四角；四角全 0 退化为 drawRect）
       @param radii: 各角半径（TL TR BR BL，像素）
       @note: 非实现后端退化为统一最大角半径
    */
    virtual void drawRoundedRect(const math::FRectangle& bounds, const math::FVector4& color,
                                 const CornerRadii& radii);

    /*
       @name: drawBorderRect (per-corner)
       @func: 四角独立圆角半径的描边矩形（描边环位于矩形边缘中央，与标量版一致）
       @param radii: 各角半径（TL TR BR BL，像素）
    */
    virtual void drawBorderRect(const math::FRectangle& bounds, const math::FVector4& color,
                                float borderWidth, const CornerRadii& radii);

    /*
       @name: drawCard
       @func: 卡片绘制 - shadow+fill+stroke 三层合并为一次提交
       @param bounds: 卡片边界
       @param style: 卡片样式（四角半径、描边位置、阴影模糊全打包）
       @note: 后端支持时单次 draw call；层序 shadow→fill→stroke 由 shader 合成
    */
    virtual void drawCard(const math::FRectangle& bounds, const CardStyle& style);

    // =============================================================================
    // Category 8: Shadow Rendering / 阴影渲染
    // =============================================================================

    /*
       @name: ShadowStyle
       @func: 阴影样式结构体
       @param color: 阴影颜色（通常带alpha）
       @param offset: 阴影偏移（像素）
       @param blurRadius: 模糊半径（像素，0=硬阴影）
       @param cornerRadius: 圆角半径（像素）
    */
    struct ShadowStyle {
        math::FVector4 color = math::FVector4(0, 0, 0, 0.5f);  // 阴影颜色 / Shadow color
        math::FVector2 offset = math::FVector2(2, 2);           // 阴影偏移 / Shadow offset
        float blurRadius = 4.0f;                                 // 模糊半径 / Blur radius
        float cornerRadius = 0;                                  // 圆角半径 / Corner radius
    };

    /*
       @name: drawRectShadow
       @func: 绘制矩形阴影 - 在矩形后方绘制阴影效果
       @param bounds: 矩形边界（阴影将绘制在此区域之外）
       @param shadow: 阴影样式配置
       @note: 阴影通常先于主体绘制，以保证层次正确
    */
    virtual void drawRectShadow(const math::FRectangle& bounds, const ShadowStyle& shadow);

    // =============================================================================
    // Category 9: Mask and Blending / 遮罩与混合
    // =============================================================================

    /*
       @name: pushMask
       @func: 压入遮罩 - 开始定义渲染遮罩区域
       @note: 之后所有渲染操作将仅在遮罩区域内可见
       @note: 用于实现复杂形状的UI元素（如圆形按钮）
    */
    virtual void pushMask() {}

    /*
       @name: popMask
       @func: 弹出遮罩 - 结束当前遮罩定义
       @note: 必须与pushMask成对调用
    */
    virtual void popMask() {}

    /*
       @name: drawWithAlpha
       @func: 绘制带透明度纹理 - 以指定alpha值绘制纹理
       @param bounds: 目标区域
       @param textureHandle: 纹理句柄
       @param alpha: 透明度值（0-1）
       @note: alpha值与纹理alpha通道相乘
    */
    virtual void drawWithAlpha(const math::FRectangle& bounds, void* textureHandle, float alpha) = 0;

    // =============================================================================
    // Category 10: Sprite / Atlas Rendering / 图集/精灵渲染
    // =============================================================================

    /*
       @name: drawSprite
       @func: 绘制精灵图 - 从图集中绘制指定精灵
       @param bounds: 目标绘制区域
       @param atlasTexture: 图集纹理句柄
       @param spriteName: 精灵名称（图集中的子图名称）
       @note: 图集将多个小图片打包成一个大纹理，减少draw call
    */
    virtual void drawSprite(const math::FRectangle& bounds, void* atlasTexture, const wchar_t* spriteName) {}

    // =============================================================================
    // Category 11: Path Clipping / 路径裁剪
    // =============================================================================

    /*
       @name: PathHandle
       @func: 路径句柄 - 用于引用后端路径资源；实现可以采用 CPU tessellation 或 GPU path
    */
    struct PathHandle { int id = -1; };

    /*
       @name: createPath
       @func: 创建空路径 - 分配一个新的路径对象
       @return: 路径句柄，用于后续路径操作
    */
    virtual PathHandle createPath() { return PathHandle{-1}; }

    /*
       @name: releasePath
       @func: 释放路径对象 - 销毁指定路径并释放资源
       @param path: 要释放的路径句柄
    */
    virtual void releasePath(PathHandle path) {}

    /*
       @name: addPathRect
       @func: 添加矩形到路径 - 将矩形添加到当前路径
       @param path: 目标路径句柄
       @param bounds: 矩形边界
       @param winding: 缠绕方向（影响空洞处理）
    */
    virtual void addPathRect(PathHandle path, const math::FRectangle& bounds, PathWinding winding = PathWinding::CounterClockwise) {}

    /*
       @name: addPathRoundedRect
       @func: 添加圆角矩形到路径
       @param path: 目标路径句柄
       @param bounds: 矩形边界
       @param cornerRadius: 圆角半径
       @param winding: 缠绕方向
    */
    virtual void addPathRoundedRect(PathHandle path, const math::FRectangle& bounds, float cornerRadius, PathWinding winding = PathWinding::CounterClockwise) {}

    /*
       @name: addPathEllipse
       @func: 添加椭圆到路径
       @param path: 目标路径句柄
       @param center: 椭圆中心点
       @param radiusX: X轴半径
       @param radiusY: Y轴半径
       @param winding: 缠绕方向
    */
    virtual void addPathEllipse(PathHandle path, const math::FVector2& center, float radiusX, float radiusY, PathWinding winding = PathWinding::CounterClockwise) {}

    /*
       @name: addPathLine
       @func: 添加线段到路径
       @param path: 目标路径句柄
       @param start: 线段起点
       @param end: 线段终点
    */
    virtual void addPathLine(PathHandle path, const math::FVector2& start, const math::FVector2& end) {}

    /*
       @name: addPathBezier
       @func: 添加贝塞尔曲线到路径
       @param path: 目标路径句柄
       @param start: 曲线起点
       @param control1: 第一个控制点
       @param control2: 第二个控制点
       @param end: 曲线终点
    */
    virtual void addPathBezier(PathHandle path, const math::FVector2& start, const math::FVector2& control1, const math::FVector2& control2, const math::FVector2& end) {}

    /*
       @name: addPathArc
       @func: 添加圆弧到路径
       @param path: 目标路径句柄
       @param center: 圆弧中心
       @param radius: 圆弧半径
       @param startAngle: 起始角度（弧度）
       @param endAngle: 终止角度（弧度）
       @param winding: 缠绕方向
    */
    virtual void addPathArc(PathHandle path, const math::FVector2& center, float radius, float startAngle, float endAngle, PathWinding winding = PathWinding::CounterClockwise) {}

    /*
       @name: addPathPolygon
       @func: 添加多边形到路径
       @param path: 目标路径句柄
       @param points: 多边形顶点数组
       @param count: 顶点数量
       @param winding: 缠绕方向
    */
    virtual void addPathPolygon(PathHandle path, const math::FVector2* points, int count, PathWinding winding = PathWinding::CounterClockwise) {}

    /*
       @name: addPathContour
       @func: 添加连续轮廓 - 与逐段 addPathLine 不同，所有点共享连接样式
       @param path: 目标路径句柄
       @param points: 连续轮廓顶点
       @param count: 顶点数量
       @param closed: 是否闭合最后一点与第一点
       @param winding: 闭合轮廓的填充方向
       @note: SVG/path flattening 应使用该入口以保留 join/cap 语义
    */
    virtual void addPathContour(PathHandle path, const math::FVector2* points,
                                int count, bool closed,
                                PathWinding winding = PathWinding::CounterClockwise) {
        if (points == nullptr || count < 2) return;
        if (closed && count >= 3) {
            addPathPolygon(path, points, count, winding);
            return;
        }
        for (int i = 1; i < count; ++i) {
            addPathLine(path, points[i - 1], points[i]);
        }
    }

    /*
       @name: setPathFillColor
       @func: 设置路径填充颜色
       @param path: 目标路径句柄
       @param color: 填充颜色
    */
    virtual void setPathFillColor(PathHandle path, const math::FVector4& color) {}

    /*
       @name: setPathStrokeColor
       @func: 设置路径描边颜色
       @param path: 目标路径句柄
       @param color: 描边颜色
    */
    virtual void setPathStrokeColor(PathHandle path, const math::FVector4& color) {}

    /*
       @name: setPathStrokeWidth
       @func: 设置路径描边宽度
       @param path: 目标路径句柄
       @param width: 描边宽度（像素）
    */
    virtual void setPathStrokeWidth(PathHandle path, float width) {}

    /*
       @name: setPathStrokeStyle
       @func: 设置连续轮廓的端点、连接和斜接限制
       @param miterLimit: 相对半描边宽度的最大斜接长度，最小为 1
    */
    virtual void setPathStrokeStyle(PathHandle path, PathStrokeCap cap,
                                    PathStrokeJoin join,
                                    float miterLimit = 4.0f) {}

    /*
       @name: drawPath
       @func: 绘制路径 - 执行路径填充/描边
       @param path: 要绘制的路径句柄
       @param mode: 填充模式
    */
    virtual void drawPath(PathHandle path, PathFillMode mode = PathFillMode::Fill) {}

    /*
       @name: pushPathClip
       @func: 使用路径作为裁剪区域
       @param path: 用作裁剪的路径句柄
       @note: 裁剪区域内的内容可见，区域外的内容被丢弃
    */
    virtual void pushPathClip(PathHandle path) {}

    // =============================================================================
    // Category 12: Blur Effect / 模糊效果
    // =============================================================================

    /*
       @name: drawRectBlur
       @func: 绘制模糊矩形 - 对指定区域应用模糊效果
       @param bounds: 模糊区域边界
       @param blurRadius: 模糊半径（像素）
       @param type: 模糊类型
       @note: 高斯模糊通常需要多次pass（downsample + blur + upscale）
    */
    virtual void drawRectBlur(const math::FRectangle& bounds, float blurRadius, BlurType type = BlurType::Gaussian) {}

    /*
       @name: drawRectBlurWithMask
       @func: 绘制带遮罩的模糊效果 - 对遮罩定义的区域应用模糊
       @param bounds: 模糊区域边界
       @param maskPath: 定义模糊区域的路径句柄
       @param blurRadius: 模糊半径（像素）
       @param type: 模糊类型
       @note: 可实现边缘清晰的模糊效果（模糊内部但保持边界锐利）
    */
    virtual void drawRectBlurWithMask(const math::FRectangle& bounds, PathHandle maskPath, float blurRadius, BlurType type = BlurType::Gaussian) {}

    // =============================================================================
    // Category 13: Particle System / 粒子系统
    // =============================================================================

    /*
       @name: ParticleHandle
       @func: 粒子系统句柄
    */
    struct ParticleHandle { int id = -1; };

    /*
       @name: ParticleStyle
       @func: 粒子样式结构体
       @param texture: 粒子纹理句柄
       @param color: 粒子颜色
       @param blendMode: 混合模式
    */
    struct ParticleStyle {
        void* texture = nullptr;                    // 粒子纹理 / Particle texture
        math::FVector4 color = math::FVector4(1,1,1,1);  // 粒子颜色 / Particle color
        BlendMode blendMode = BlendMode::Normal;    // 混合模式 / Blend mode
    };

    /*
       @name: createParticleSystem
       @func: 创建粒子系统
       @param maxParticles: 最大粒子数量
       @return: 粒子系统句柄
    */
    virtual ParticleHandle createParticleSystem(int maxParticles) { return ParticleHandle{-1}; }

    /*
       @name: releaseParticleSystem
       @func: 释放粒子系统
       @param system: 要释放的粒子系统句柄
    */
    virtual void releaseParticleSystem(ParticleHandle system) {}

    /*
       @name: setParticleStyle
       @func: 设置粒子样式
       @param system: 粒子系统句柄
       @param style: 粒子样式配置
    */
    virtual void setParticleStyle(ParticleHandle system, const ParticleStyle& style) {}

    /*
       @name: addParticle
       @func: 添加粒子到系统
       @param system: 粒子系统句柄
       @param position: 粒子位置
       @param velocity: 粒子速度
       @param life: 粒子寿命（秒）
       @param size: 粒子大小
       @note: 粒子将在后续帧中自动更新和渲染
    */
    virtual void addParticle(ParticleHandle system, const math::FVector2& position, const math::FVector2& velocity, float life, float size) {}

    /*
       @name: updateParticleSystem
       @func: 更新粒子系统状态
       @param system: 粒子系统句柄
       @param deltaTime: 帧间隔时间（秒）
       @note: 通常在beginFrame后调用
    */
    virtual void updateParticleSystem(ParticleHandle system, float deltaTime) {}

    /*
       @name: drawParticleSystem
       @func: 绘制粒子系统
       @param system: 粒子系统句柄
       @note: 绘制所有活跃粒子
    */
    virtual void drawParticleSystem(ParticleHandle system) {}

    // =============================================================================
    // Category 14: Render Target Management / 渲染目标管理
    // =============================================================================

    /*
       @name: RenderTargetHandle
       @func: 渲染目标句柄 - 引用离屏渲染缓冲区
    */
    struct RenderTargetHandle {
        int id = -1;
        bool isValid() const { return id >= 0; }
    };

    // Stable description used by UI layers. width/height are physical
    // pixels; logical coordinates remain in AYUI's screen-space units and
    // are related through dpiScale. preserveContents is a capability hint:
    // a backend may return false from resizeRenderTarget when it cannot keep
    // the old pixels and the caller must repaint the layer in full.
    struct RenderTargetDesc {
        int width = 0;
        int height = 0;
        float dpiScale = 1.0f;
        bool hasAlpha = true;
        bool preserveContents = false;
    };

    virtual bool supportsRenderTargets() const { return false; }

    /*
       @name: createRenderTarget
       @func: 创建渲染目标 - 分配离屏渲染缓冲区
       @param width: 渲染目标宽度（像素）
       @param height: 渲染目标高度（像素）
       @param hasAlpha: 是否包含Alpha通道
       @return: 渲染目标句柄
       @note: 渲染目标可用于实现多窗口、后期处理等
    */
    virtual RenderTargetHandle createRenderTarget(int width, int height, bool hasAlpha = true) { return RenderTargetHandle{-1}; }

    virtual RenderTargetHandle createRenderTarget(const RenderTargetDesc& desc) {
        return createRenderTarget(desc.width, desc.height, desc.hasAlpha);
    }

    // Returns true when the target now matches desc. Returning false leaves
    // the old target valid and asks the caller to release/recreate it.
    virtual bool resizeRenderTarget(RenderTargetHandle target, const RenderTargetDesc& desc) {
        AYUNREFERENCED_PARAM(target);
        AYUNREFERENCED_PARAM(desc);
        return false;
    }

    /*
       @name: releaseRenderTarget
       @func: 释放渲染目标
       @param target: 要释放的渲染目标句柄
    */
    virtual void releaseRenderTarget(RenderTargetHandle target) {}

    /*
       @name: bindRenderTarget
       @func: 绑定渲染目标 - 将其设为当前渲染目标
       @param target: 渲染目标句柄（设为无效句柄则绑定到默认帧缓冲）
       @note: 绑定后所有渲染操作将在目标纹理上执行
    */
    virtual void bindRenderTarget(RenderTargetHandle target) {}

    /*
       @name: getRenderTargetTexture
       @func: 获取渲染目标纹理句柄
       @param target: 渲染目标句柄
       @return: 纹理句柄，可用于在其他地方渲染此目标内容
       @note: 返回的纹理包含渲染目标的所有历史内容
    */
    virtual void* getRenderTargetTexture(RenderTargetHandle target) { return nullptr; }

    /*
       @name: blitRenderTarget
       @func: 复制渲染目标 - 将源渲染目标内容复制到目标区域
       @param source: 源渲染目标
       @param destBounds: 目标区域
       @param filter: 采样过滤模式
       @note: 可用于实现渲染目标的缩放显示
    */
    virtual void blitRenderTarget(RenderTargetHandle source, const math::FRectangle& destBounds) {}

    // ---------------------------------------------------------------------
    // UI Layer abstraction
    // ---------------------------------------------------------------------
    // A layer owns backend render-target state and can retain pixels across
    // frames. It is deliberately separate from Widget/display-list caching:
    // display lists retain commands and replay every frame; layers retain
    // pixels and repaint only after invalidation. Backends that do not
    // support layers return an invalid handle, and callers must use the
    // existing immediate path.
    struct LayerHandle {
        int id = -1;
        bool isValid() const { return id >= 0; }
    };

    enum class LayerClearMode : uint8_t {
        Preserve,
        Transparent,
        Color
    };

    struct LayerDesc {
        math::FRectangle logicalBounds;
        float dpiScale = 1.0f;
        bool hasAlpha = true;
        // Overlay layers must remain above normal tree content. The flag is
        // metadata for backends/schedulers; AYUI still controls painter order
        // by where compositeLayer is called.
        bool overlay = false;
        LayerClearMode clearMode = LayerClearMode::Transparent;
        math::FVector4 clearColor = math::FVector4(0, 0, 0, 0);
    };

    struct LayerPaint {
        // Empty damage with fullRedraw=false means there is no repaint work.
        math::FRectangle damage;
        bool fullRedraw = true;
    };

    // Cumulative production-layer telemetry. Backends may expose a shared
    // RenderTargetPool, so target memory/allocation fields can include
    // non-UI users while paint/composite counters remain UI-layer specific.
    // `cacheHits / composites` is the retained-pixel hit rate (guard the
    // zero-composite case in callers).
    struct LayerCacheStats {
        uint64_t layerCreates = 0;
        uint64_t layerReleases = 0;
        uint64_t fullPaints = 0;
        uint64_t partialPaints = 0;
        uint64_t composites = 0;
        uint64_t cacheHits = 0;
        uint64_t repaintPixelArea = 0;
        uint64_t allocationFailures = 0;
        uint64_t degradedLayers = 0;
        uint64_t targetAllocations = 0;
        uint64_t targetReuses = 0;
        uint64_t targetEvictions = 0;
        uint32_t liveLayers = 0;
        uint32_t liveTargetLeases = 0;
        uint32_t idleTargets = 0;
        size_t allocatedTargetBytes = 0;
        size_t targetBudgetBytes = 0;
    };

    virtual LayerHandle createLayer(const LayerDesc& desc) {
        AYUNREFERENCED_PARAM(desc);
        return LayerHandle{-1};
    }
    virtual void releaseLayer(LayerHandle layer) {
        AYUNREFERENCED_PARAM(layer);
    }
    // Resize/DPI/bounds changes may invalidate retained pixels. true means
    // the layer remains usable; callers should inspect their own change and
    // schedule a full repaint when logical content changed.
    virtual bool updateLayer(LayerHandle layer, const LayerDesc& desc) {
        AYUNREFERENCED_PARAM(layer);
        AYUNREFERENCED_PARAM(desc);
        return false;
    }
    // beginLayerPaint returns false when this backend cannot enter an
    // offscreen pass. In that case callers paint directly and must not call
    // endLayerPaint/compositeLayer for this attempt.
    virtual bool beginLayerPaint(LayerHandle layer, const LayerPaint& paint) {
        AYUNREFERENCED_PARAM(layer);
        AYUNREFERENCED_PARAM(paint);
        return false;
    }
    virtual void endLayerPaint(LayerHandle layer) {
        AYUNREFERENCED_PARAM(layer);
    }
    virtual void compositeLayer(LayerHandle layer,
                                const math::FRectangle& destBounds,
                                float opacity = 1.0f) {
        AYUNREFERENCED_PARAM(layer);
        AYUNREFERENCED_PARAM(destBounds);
        AYUNREFERENCED_PARAM(opacity);
    }
    virtual void invalidateLayer(LayerHandle layer,
                                 const math::FRectangle& damage = math::FRectangle()) {
        AYUNREFERENCED_PARAM(layer);
        AYUNREFERENCED_PARAM(damage);
    }
    // Capability-neutral dirty query used by production compositors after a
    // device reset or target-pool eviction. Unsupported/unknown layers are
    // conservatively dirty so callers repaint through their immediate path.
    virtual bool isLayerDirty(LayerHandle layer) const {
        AYUNREFERENCED_PARAM(layer);
        return true;
    }
    virtual LayerCacheStats getLayerCacheStats() const {
        return {};
    }
    // The budget is renderer-wide when UI and FrameGraph share a pool.
    // A zero budget disables new retained backing allocations; callers keep
    // their logical LayerHandle and fall back to immediate rendering.
    virtual void setLayerCacheBudgetBytes(size_t bytes) {
        AYUNREFERENCED_PARAM(bytes);
    }
    virtual void resetLayerCacheStats() {}

    // =============================================================================
    // Category 15: Animation System / 动画系统
    // =============================================================================

    /*
       @name: AnimationHandle
       @func: 动画句柄
    */
    struct AnimationHandle { int id = -1; };

    /*
       @name: createAnimation
       @func: 创建数值动画
       @param from: 起始值
       @param to: 目标值
       @param duration: 动画持续时间（秒）
       @param curve: 动画曲线
       @return: 动画句柄
    */
    virtual AnimationHandle createAnimation(float from, float to, float duration, AnimationCurve curve = AnimationCurve::EaseOut) { return AnimationHandle{-1}; }

    /*
       @name: createAnimationVec2
       @func: 创建二维向量动画
       @param from: 起始向量
       @param to: 目标向量
       @param duration: 动画持续时间（秒）
       @param curve: 动画曲线
       @return: 动画句柄
    */
    virtual AnimationHandle createAnimationVec2(const math::FVector2& from, const math::FVector2& to, float duration, AnimationCurve curve = AnimationCurve::EaseOut) { return AnimationHandle{-1}; }

    /*
       @name: createAnimationVec4
       @func: 创建四维向量动画（常用于颜色渐变）
       @param from: 起始向量
       @param to: 目标向量
       @param duration: 动画持续时间（秒）
       @param curve: 动画曲线
       @return: 动画句柄
    */
    virtual AnimationHandle createAnimationVec4(const math::FVector4& from, const math::FVector4& to, float duration, AnimationCurve curve = AnimationCurve::EaseOut) { return AnimationHandle{-1}; }

    /*
       @name: releaseAnimation
       @func: 释放动画对象
       @param anim: 要释放的动画句柄
    */
    virtual void releaseAnimation(AnimationHandle anim) {}

    /*
       @name: setAnimationFlags
       @func: 设置动画标志
       @param anim: 动画句柄
       @param flags: 动画标志位组合
    */
    virtual void setAnimationFlags(AnimationHandle anim, AnimationFlags flags) {}

    /*
       @name: updateAnimation
       @func: 更新动画状态
       @param anim: 动画句柄
       @param deltaTime: 帧间隔时间（秒）
       @return: 当前动画值是否仍在进行中
    */
    virtual bool updateAnimation(AnimationHandle anim, float deltaTime) { return false; }

    /*
       @name: getAnimationValue
       @func: 获取动画当前值
       @param anim: 动画句柄
       @return: 动画的当前插值结果
    */
    virtual float getAnimationValue(AnimationHandle anim) const { return 0.0f; }

    /*
       @name: getAnimationValueVec2
       @func: 获取二维向量动画当前值
       @param anim: 动画句柄
       @return: 当前向量值
    */
    virtual math::FVector2 getAnimationValueVec2(AnimationHandle anim) const { return math::FVector2(0, 0); }

    /*
       @name: getAnimationValueVec4
       @func: 获取四维向量动画当前值
       @param anim: 动画句柄
       @return: 当前向量值
    */
    virtual math::FVector4 getAnimationValueVec4(AnimationHandle anim) const { return math::FVector4(0, 0, 0, 0); }

    // =============================================================================
    // Category 16: Batch Optimization (Internal Use) / 批处理优化（内部使用）
    // =============================================================================

    /*
       @name: addColoredQuad
       @func: Append a flat colored quad to the active batch.
       @param bounds: quad bounds in screen space
       @param color:  RGBA tint
       @return true if the quad was retained in a backend-side batch;
               false if the backend has no batching and the default
               implementation fell back to immediate drawRect.
       @note: Backends that implement batching override this and return true;
               default implementation just calls drawRect and returns false.
               UIManager::render calls flushBatches() once at end of frame.
    */
    virtual bool addColoredQuad(const math::FRectangle& bounds, const math::FVector4& color) {
        drawRect(bounds, color);
        return false;
    }

    /*
       @name: addTexturedQuad
       @func: Append a flat textured quad to the active batch.
       @param bounds:        quad bounds in screen space
       @param textureHandle: backend texture handle
       @param uv:            uv region inside the texture
       @param tint:          per-quad RGBA tint (default white)
       @return true if the quad was retained in a backend-side batch;
               false if the backend has no batching and the default
               implementation fell back to immediate drawRect.
    */
    virtual bool addTexturedQuad(const math::FRectangle& bounds, void* textureHandle,
                                 const math::FRectangle& uv,
                                 const math::FVector4& tint = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)) {
        AYUNREFERENCED_PARAM(tint);
        drawRect(bounds, textureHandle, uv);
        return false;
    }

    /*
       @name: flushBatches
       @func: Submit all batched quads accumulated since the previous flush.
       @note: Called by UIManager::render once before endCanvas(). Default
               implementation is a no-op for backends without batching.
    */
    virtual void flushBatches() {}

    /*
       @name: flush
       @func: 刷新渲染队列 - 强制提交所有待处理的渲染命令
       @note: 通常自动在帧结束时调用，也可手动调用以确保渲染顺序
    */
    virtual void flush() {}

    /*
       @name: getDrawCallCount
       @func: 获取绘制调用计数 - 返回已提交的绘制调用数量
       @return: 当前帧的绘制调用总数
       @note: 用于性能分析，理想情况下应尽量减少此数值
    */
    virtual int getDrawCallCount() const { return 0; }

    /*
       @name: getTriangleCount
       @func: 获取三角形计数 - 返回已提交的三角形数量
       @return: 当前帧的三角形总数
    */
    virtual int getTriangleCount() const { return 0; }

    /*
       @name: getVertexCount
       @func: 获取顶点计数 - 返回已提交的顶点数量
       @return: 当前帧的顶点总数
    */
    virtual int getVertexCount() const { return 0; }

    /*
       @name: pushPerformanceMarker
       @func: 推送性能标记 - 用于GPU Profiling
       @param name: 标记名称
       @note: 必须与popPerformanceMarker配对
    */
    virtual void pushPerformanceMarker(const char* name) {}

    /*
       @name: popPerformanceMarker
       @func: 弹出性能标记
    */
    virtual void popPerformanceMarker() {}

    // =============================================================================
    // Category 17: Font Management / 字体管理
    // =============================================================================
    // Category 17: Font Management / 字体管理 (使用 ayt::font::FontHandle)
    // =============================================================================

    /*
       @name: loadFont
       @func: 加载字体 - 从文件加载字体到渲染系统
       @param path: 字体文件路径（如.ttf, .otf）
       @param baseSize: 基础字号（像素高度）
       @return: 字体句柄，加载失败返回 id=-1
       @note: 字体通常会被缓存，避免重复加载
       @note: 内部调用 IFontManager::registerFont
    */
    virtual FontHandle loadFont(const wchar_t* path, int baseSize) {
        AYUNREFERENCED_PARAM(path);
        AYUNREFERENCED_PARAM(baseSize);
        return FontHandle{-1};
    }

    /*
       @name: releaseFont
       @func: 释放字体 - 卸载指定字体并释放资源
       @param font: 要释放的字体句柄
       @note: 内部调用 IFontManager::releaseFont
    */
    virtual void releaseFont(FontHandle font) {
        AYUNREFERENCED_PARAM(font);
    }

    /*
       @name: getFontHandle
       @func: 获取已注册字体的句柄
       @param familyName: 字体家族名称
       @param baseSize: 基础字号
       @return: 字体句柄，如未加载则返回 id=-1
    */
    virtual FontHandle getFontHandle(const wchar_t* familyName, int baseSize) {
        AYUNREFERENCED_PARAM(familyName);
        AYUNREFERENCED_PARAM(baseSize);
        return FontHandle{-1};
    }

    /*
       @name: registerFontFromMemory
       @func: 从内存数据注册字体
       @param data: 字体数据指针
       @param dataSize: 数据大小（字节）
       @param baseSize: 基础字号
       @return: 字体句柄
       @note: 用于嵌入字体的场景
    */
    virtual FontHandle registerFontFromMemory(const void* data, size_t dataSize, int baseSize) {
        AYUNREFERENCED_PARAM(data);
        AYUNREFERENCED_PARAM(dataSize);
        AYUNREFERENCED_PARAM(baseSize);
        return FontHandle{-1};
    }

    // =============================================================================
    // Category 18: Metrics and Debug / 度量与调试
    // =============================================================================

    /*
       @name: MetricsHandle
       @func: 度量查询句柄
    */
    struct MetricsHandle { int id = -1; };

    /*
       @name: TextMetrics
       @func: 文字度量结构 - 包含文字渲染尺寸信息
       @param width: 文字宽度（像素）
       @param height: 文字高度（像素）
       @param ascent: 基线以上高度
       @param descent: 基线以下高度
    */
    struct TextMetrics {
        float width;
        float height;
        float ascent;
        float descent;
    };

    // Cluster positions from the same shaping path used for rendering.
    // xStart/xEnd are logical-DIP visual coordinates relative to the shaped
    // run origin; source offsets address std::wstring code units.
    struct ShapedTextCluster {
        size_t sourceStart = 0;
        size_t sourceLength = 0;
        float xStart = 0.0f;
        float xEnd = 0.0f;
        uint8_t bidiLevel = 0;
    };

    struct ShapedText {
        TextMetrics metrics{0, 0, 0, 0};
        std::vector<ShapedTextCluster> clusters;
        bool rightToLeft = false;
    };

    /*
       @name: measureText
       @func: 度量文字 - 计算文字渲染后的实际尺寸
       @param text: 要度量的文字
       @param fontSize: 字体大小
       @param maxWidth: 最大宽度限制（可选）
       @return: 包含文字尺寸的结构体
       @note: 用于文字排版和自动布局
       @note: 内部调用 Font::measureText
    */
    virtual TextMetrics measureText(const std::wstring& text, int fontSize, float maxWidth = 0.0f) const {
        AYUNREFERENCED_PARAM(text);
        AYUNREFERENCED_PARAM(fontSize);
        AYUNREFERENCED_PARAM(maxWidth);
        return TextMetrics{0, 0, 0, 0};
    }

    virtual TextMetrics measureText(const std::wstring& text, int fontSize,
                                    const TextStyle& style,
                                    float maxWidth = 0.0f) const {
        AYUNREFERENCED_PARAM(style);
        return measureText(text, fontSize, maxWidth);
    }

    virtual ShapedText shapeText(const std::wstring& text, int fontSize,
                                 const TextStyle& style) const;

    /*
       @name: getFontMetrics
       @func: 获取字体度量信息
       @param font: 字体句柄
       @return: 字体度量结构体
       @note: 内部调用 Font::getMetrics
    */
    virtual ayt::font::FontMetrics getFontMetrics(FontHandle font) const {
        AYUNREFERENCED_PARAM(font);
        return ayt::font::FontMetrics{0, 0, 0, 0, 0, 0};
    }

    /*
       @name: getAvailableVideoMemory
       @func: 获取可用显存
       @return: 可用显存大小（字节）
       @note: 用于动态调整纹理质量
    */
    virtual size_t getAvailableVideoMemory() const { return 0; }

    /*
       @name: getDriverVersion
       @func: 获取图形驱动版本
       @return: 版本字符串
    */
    virtual std::string getDriverVersion() const { return "Unknown"; }

    /*
       @name: getBackendName
       @func: 获取后端名称
       @return: 后端名称（如"OpenGL", "Vulkan", "DirectX12"）
    */
    virtual std::string getBackendName() const { return "None"; }
};

// =============================================================================
// Default Inline Implementations / 默认内联实现
// =============================================================================

inline void IRenderBackend::drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize, const TextStyle& style) {
    drawText(bounds, text, fontSize, style.color);
}

inline IRenderBackend::ShapedText IRenderBackend::shapeText(
    const std::wstring& text, int fontSize, const TextStyle& style) const {
    ShapedText out;
    out.metrics = measureText(text, fontSize, style);
    const UnicodeTextAnalysis analysis = analyzeUnicodeText(text, style.direction);
    out.rightToLeft = analysis.baseRightToLeft;
    const std::vector<size_t> visual = reorderUnicodeClusters(
        analysis, 0, analysis.clusters.size());
    TextStyle clusterStyle = style;
    clusterStyle.letterSpacing = 0;
    float x = 0.0f;
    out.clusters.reserve(visual.size());
    for (size_t index : visual) {
        const UnicodeTextCluster& source = analysis.clusters[index];
        const std::wstring clusterText = text.substr(source.textStart,
                                                     source.textLength);
        const float width = measureText(clusterText, fontSize, clusterStyle).width
            + static_cast<float>(style.letterSpacing);
        out.clusters.push_back({source.textStart, source.textLength,
                                x, x + std::max(0.0f, width),
                                source.bidiLevel});
        x += std::max(0.0f, width);
    }
    out.metrics.width = x;
    return out;
}

inline void IRenderBackend::drawGradientRect(const math::FRectangle& bounds,
                                             const math::FVector4& topColor, const math::FVector4& bottomColor) {
    drawGradientRect(bounds, topColor, topColor, bottomColor, bottomColor);
}

inline void IRenderBackend::drawRect(const math::FRectangle& bounds, const BorderStyle& border) {
    // Route through drawCard so Position reaches backends that support it
    // (default drawCard preserves the drawBorderRect visual: Center ring).
    CardStyle card;
    card.cornerRadius  = CornerRadii(border.cornerRadius);
    card.borderColor   = border.color;
    card.borderWidth   = border.width;
    card.borderPosition = border.position;
    drawCard(bounds, card);
}

inline void IRenderBackend::drawBorderRect(const math::FRectangle& bounds, const math::FVector4& color, float borderWidth, float cornerRadius) {
    if (borderWidth <= 0.0f) {
        return;
    }

    const float minX = bounds.minX;
    const float minY = bounds.minY;
    const float maxX = bounds.maxX;
    const float maxY = bounds.maxY;
    const float width  = maxX - minX;
    const float height = maxY - minY;

    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    const float w = std::min(borderWidth, std::min(width, height) * 0.5f);
    if (w <= 0.0f) {
        return;
    }

    if (width <= w * 2.0f || height <= w * 2.0f) {
        drawRect(bounds, color);
        return;
    }

    const float maxRadius = std::min(width, height) * 0.5f;
    const float r         = std::max(0.0f, std::min(cornerRadius, maxRadius - w));

    drawRect(math::FRectangle(minX + r, minY, maxX - r, minY + w), color);
    drawRect(math::FRectangle(minX + r, maxY - w, maxX - r, maxY), color);
    drawRect(math::FRectangle(minX, minY + r, minX + w, maxY - r), color);
    drawRect(math::FRectangle(maxX - w, minY + r, maxX, maxY - r), color);

    if (r > 0.0f) {
        drawRect(math::FRectangle(minX, minY, minX + r, minY + r), color);
        drawRect(math::FRectangle(maxX - r, minY, maxX, minY + r), color);
        drawRect(math::FRectangle(minX, maxY - r, minX + r, maxY), color);
        drawRect(math::FRectangle(maxX - r, maxY - r, maxX, maxY), color);
    }
}

inline void IRenderBackend::drawRoundedRect(const math::FRectangle& bounds,
                                            const math::FVector4& color,
                                            float cornerRadius) {
    (void)cornerRadius;
    drawRect(bounds, color);
}

inline void IRenderBackend::drawRoundedRect(const math::FRectangle& bounds,
                                            const math::FVector4& color,
                                            const CornerRadii& radii) {
    if (radii.isUniform()) {
        drawRoundedRect(bounds, color, radii.topLeft);
        return;
    }
    // Non-implementing backends approximate with the largest corner.
    drawRoundedRect(bounds, color, radii.maxRadius());
}

inline void IRenderBackend::drawBorderRect(const math::FRectangle& bounds,
                                           const math::FVector4& color, float borderWidth,
                                           const CornerRadii& radii) {
    if (radii.isUniform()) {
        drawBorderRect(bounds, color, borderWidth, radii.topLeft);
        return;
    }
    drawBorderRect(bounds, color, borderWidth, radii.maxRadius());
}

inline void IRenderBackend::drawCard(const math::FRectangle& bounds, const CardStyle& style) {
    // Layer order shadow → fill → stroke (stroke last so the ring sits on
    // top of the fill). Per-corner radii degrade to the scalar forms via
    // the CornerRadii overloads above.
    if (style.shadowColor.w > 0.0f) {
        ShadowStyle shadow;
        shadow.color        = style.shadowColor;
        shadow.offset       = style.shadowOffset;
        shadow.blurRadius   = style.shadowBlurRadius;
        shadow.cornerRadius = style.cornerRadius.topLeft;
        drawRectShadow(bounds, shadow);
    }
    if (style.fillColor.w > 0.0f) {
        drawRoundedRect(bounds, style.fillColor, style.cornerRadius);
    }
    if (style.borderWidth > 0.0f && style.borderColor.w > 0.0f) {
        drawBorderRect(bounds, style.borderColor, style.borderWidth, style.cornerRadius);
    }
}

inline void IRenderBackend::drawRectShadow(const math::FRectangle& bounds, const ShadowStyle& shadow) {
    drawRect(math::FRectangle(
        bounds.minX + shadow.offset.x,
        bounds.minY + shadow.offset.y,
        bounds.maxX + shadow.offset.x,
        bounds.maxY + shadow.offset.y
    ), shadow.color);
}

} // namespace ayt::ui
