#pragma once

#include "AYUI/IRenderBackend.h"
#include <vector>
#include <unordered_map>

namespace ayt::ui {

// Mock renderer for testing UI logic without actual rendering
// Mock渲染器 - 用于在不进行实际渲染的情况下测试UI逻辑
class MockRenderer : public IRenderBackend {
public:
    struct DrawCall {
        // AYUI-Audit-2026-08-26: added `Card` to support drawCard()
        // round-trip assertions. Card rides the same enum so existing
        // aggregate-free initializers continue to compile (Card is
        // appended after Blur, not inserted in the middle).
        enum Type { Rect, Text, Image, Path, Particle, Blur, Card };
        Type type;
        math::FRectangle bounds;
        math::FVector4 color;
        std::wstring text;
        void* texture;
        int fontSize;
        TextStyle textStyle;
        float floatParam1;
        float floatParam2;
        int intParam1;
        PathHandle pathHandle;
        ParticleHandle particleHandle;
        BlurType blurType;
        // P1 additions (appended so existing aggregate-free initializers
        // keep compiling): blend mode active when the call was recorded,
        // and per-corner gradient colors in param order TL TR BL BR.
        BlendMode blendMode = BlendMode::Normal;
        math::FVector4 cornerColors[4];
        // AYUI-Audit-2026-08-26: full TextStyle recording. The styled
        // drawText() override stores every TextStyle field here so tests
        // can assert outline / shadow / wrap / valign were passed through
        // instead of being silently dropped by the base overload.
        math::FVector4 outlineColor = math::FVector4(0, 0, 0, 0);
        float          outlineWidth = 0.0f;
        math::FVector4 shadowColor = math::FVector4(0, 0, 0, 0);
        math::FVector2 shadowOffset = math::FVector2(0, 0);
        float          shadowBlurRadius = 0.0f;
        int            letterSpacing = 0;
        int            lineSpacing = 0;
        bool           wrapToBounds = false;
        TextStyle::Align  align = TextStyle::Align::Left;
        TextStyle::VAlign valign = TextStyle::VAlign::Middle;
        // AYUI-Audit-2026-08-26: CardStyle recording for drawCard() tests.
        // Card payload rides alongside the existing fields — fillColor is
        // reused as the layer-fill color, borderWidth/Color as the ring,
        // shadow* as the drop shadow, and CornerRadii slots replace the
        // per-corner gradient palette.
        math::FVector4  cardFillColor   = math::FVector4(0, 0, 0, 0);
        math::FVector4  cardBorderColor = math::FVector4(0, 0, 0, 0);
        float           cardBorderWidth = 0.0f;
        BorderStyle::Position cardBorderPosition = BorderStyle::Position::Center;
        math::FVector4  cardShadowColor = math::FVector4(0, 0, 0, 0);
        math::FVector2  cardShadowOffset = math::FVector2(0, 0);
        float           cardShadowBlurRadius = 0.0f;
        // Per-corner radii in order TL TR BR BL. We reuse the same 4-wide
        // slot the gradient used to claim, so the storage footprint stays
        // unchanged.
        float           cardRadii[4] = {0, 0, 0, 0};
    };

    MockRenderer();
    ~MockRenderer();

    // Recording / 录制
    void clear();
    const std::vector<DrawCall>& getDrawCalls() const { return _drawCalls; }

    // IRenderBackend implementations - pure virtual (= 0) / 纯虚函数实现
    void drawRect(const math::FRectangle& bounds, const math::FVector4& color) override;
    void drawRect(const math::FRectangle& bounds, void* textureHandle, const math::FRectangle& uv) override;
    void drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize, const math::FVector4& color) override;
    void drawWithAlpha(const math::FRectangle& bounds, void* textureHandle, float alpha) override;

    // IRenderBackend implementations - virtual with default impl / 虚函数实现
    void setBlendMode(BlendMode mode) override;
    void pushOpacity(float alpha) override;
    void popOpacity() override;
    void drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize, const TextStyle& style) override;
    void drawGradientRect(const math::FRectangle& bounds,
                         const math::FVector4& topLeft, const math::FVector4& topRight,
                         const math::FVector4& bottomLeft, const math::FVector4& bottomRight) override;
    void drawGradientRect(const math::FRectangle& bounds,
                         const math::FVector4& topColor, const math::FVector4& bottomColor) override;
    void drawRect(const math::FRectangle& bounds, const BorderStyle& border) override;
    void drawBorderRect(const math::FRectangle& bounds, const math::FVector4& color, float borderWidth, float cornerRadius) override;
    void drawRoundedRect(const math::FRectangle& bounds, const math::FVector4& color, float cornerRadius) override;
    void drawRectShadow(const math::FRectangle& bounds, const ShadowStyle& shadow) override;

    // AYUI-Audit-2026-08-26: drawCard override. Records a single Card
    // DrawCall carrying the full CardStyle so tests can assert the
    // shadow+fill+stroke payload round-trips into the recorded call
    // instead of expanding into the default IRenderBackend shadow→
    // fill→border sub-cascade. Tests assert against dc.type ==
    // DrawCall::Card and the recorded card* fields.
    void drawCard(const math::FRectangle& bounds, const CardStyle& style) override;

    // Path methods / 路径方法
    PathHandle createPath() override;
    void releasePath(PathHandle path) override;
    void addPathRect(PathHandle path, const math::FRectangle& bounds, PathWinding winding = PathWinding::CounterClockwise) override;
    void addPathRoundedRect(PathHandle path, const math::FRectangle& bounds, float cornerRadius, PathWinding winding = PathWinding::CounterClockwise) override;
    void addPathEllipse(PathHandle path, const math::FVector2& center, float radiusX, float radiusY, PathWinding winding = PathWinding::CounterClockwise) override;
    void addPathLine(PathHandle path, const math::FVector2& start, const math::FVector2& end) override;
    void addPathBezier(PathHandle path, const math::FVector2& start, const math::FVector2& control1, const math::FVector2& control2, const math::FVector2& end) override;
    void addPathArc(PathHandle path, const math::FVector2& center, float radius, float startAngle, float endAngle, PathWinding winding = PathWinding::CounterClockwise) override;
    void addPathPolygon(PathHandle path, const math::FVector2* points, int count, PathWinding winding = PathWinding::CounterClockwise) override;
    void setPathFillColor(PathHandle path, const math::FVector4& color) override;
    void setPathStrokeColor(PathHandle path, const math::FVector4& color) override;
    void setPathStrokeWidth(PathHandle path, float width) override;
    void drawPath(PathHandle path, PathFillMode mode = PathFillMode::Fill) override;
    void pushPathClip(PathHandle path) override;

    // Blur methods / 模糊方法
    void drawRectBlur(const math::FRectangle& bounds, float blurRadius, BlurType type = BlurType::Gaussian) override;
    void drawRectBlurWithMask(const math::FRectangle& bounds, PathHandle maskPath, float blurRadius, BlurType type = BlurType::Gaussian) override;

    // Particle system methods / 粒子系统方法
    ParticleHandle createParticleSystem(int maxParticles) override;
    void releaseParticleSystem(ParticleHandle system) override;
    void setParticleStyle(ParticleHandle system, const ParticleStyle& style) override;
    void addParticle(ParticleHandle system, const math::FVector2& position, const math::FVector2& velocity, float life, float size) override;
    void updateParticleSystem(ParticleHandle system, float deltaTime) override;
    void drawParticleSystem(ParticleHandle system) override;

    // Render target methods / 渲染目标方法
    RenderTargetHandle createRenderTarget(int width, int height, bool hasAlpha = true) override;
    void releaseRenderTarget(RenderTargetHandle target) override;
    void bindRenderTarget(RenderTargetHandle target) override;
    void* getRenderTargetTexture(RenderTargetHandle target) override;
    void blitRenderTarget(RenderTargetHandle source, const math::FRectangle& destBounds) override;

    // Animation methods / 动画方法
    AnimationHandle createAnimation(float from, float to, float duration, AnimationCurve curve = AnimationCurve::EaseOut) override;
    AnimationHandle createAnimationVec2(const math::FVector2& from, const math::FVector2& to, float duration, AnimationCurve curve = AnimationCurve::EaseOut) override;
    AnimationHandle createAnimationVec4(const math::FVector4& from, const math::FVector4& to, float duration, AnimationCurve curve = AnimationCurve::EaseOut) override;
    void releaseAnimation(AnimationHandle anim) override;
    void setAnimationFlags(AnimationHandle anim, AnimationFlags flags) override;
    bool updateAnimation(AnimationHandle anim, float deltaTime) override;
    float getAnimationValue(AnimationHandle anim) const override;
    math::FVector2 getAnimationValueVec2(AnimationHandle anim) const override;
    math::FVector4 getAnimationValueVec4(AnimationHandle anim) const override;

    // Font methods / 字体方法
    FontHandle loadFont(const wchar_t* path, int baseSize) override;
    void releaseFont(FontHandle font) override;
    FontHandle getFontHandle(const wchar_t* familyName, int baseSize) override;
    FontHandle registerFontFromMemory(const void* data, size_t dataSize, int baseSize) override;

    // Metrics methods / 度量方法
    TextMetrics measureText(const std::wstring& text, int fontSize, float maxWidth = 0.0f) const override;
    FontMetrics getFontMetrics(FontHandle font) const override;
    size_t getAvailableVideoMemory() const override;
    std::string getDriverVersion() const override;
    std::string getBackendName() const override;

    // Stats / 统计
    int getDrawCallCount() const override { return static_cast<int>(_drawCalls.size()); }
    int getTriangleCount() const override { return _triangleCount; }
    int getVertexCount() const override { return _vertexCount; }

    // PR-Container-Contract-Cut2: clip-stack recording. pushClip/popClip
    // are virtual-with-default `{}` in IRenderBackend; we override them
    // so tests can assert container renderChildren implementations keep
    // their push/pop balanced. Pre-PR cut2 these were silent no-ops —
    // nothing could detect a forgotten popClip until the next push
    // silently overwrote the prior stack frame.
    void pushClip(const math::FRectangle& bounds) override;
    void popClip() override;

    // Clip stack introspection (for tests).
    struct ClipEvent {
        math::FRectangle bounds;
    };
    int  getClipDepth() const { return static_cast<int>(_clipStack.size()); }
    bool isClipStackBalanced() const { return _clipStack.empty(); }
    const std::vector<ClipEvent>& getClipStack() const { return _clipStack; }

    void pushPerformanceMarker(const char* name) override;
    void popPerformanceMarker() override;

private:
    std::vector<DrawCall> _drawCalls;
    BlendMode _currentBlend = BlendMode::Normal;
    int _triangleCount = 0;
    int _vertexCount = 0;
    int _nextPathId = 1;
    int _nextParticleId = 1;
    int _nextAnimId = 1;
    int _nextFontId = 1;
    int _nextTargetId = 1;

    // PR-anim: stacked opacity (LIFO, same shape as the clip stack).
    // Every color-emitting draw multiplies its alpha by the top frame;
    // the stack base is always 1.0 (no-op) so default rendering is
    // byte-identical to pre-opacity behavior.
    std::vector<float> _opacityStack = {1.0f};

    // Multiply a color's alpha by the current opacity frame. RGB stays
    // untouched — opacity fades the whole draw, it does not tint it.
    math::FVector4 tintWithOpacity(const math::FVector4& color) const {
        return math::FVector4(color.x, color.y, color.z, color.w * _opacityStack.back());
    }

    struct PathData {
        math::FRectangle bounds;
        math::FVector4 fillColor;
        math::FVector4 strokeColor;
        float strokeWidth = 1.0f;
    };
    std::unordered_map<int, PathData> _paths;

    struct AnimationData {
        float from, to, current;
        float duration;
        float elapsed;
        AnimationCurve curve;
        AnimationFlags flags;
        enum Type { Float, Vec2, Vec4 } type;
        math::FVector2 vec2From, vec2To, vec2Current;
        math::FVector4 vec4From, vec4To, vec4Current;
    };
    std::unordered_map<int, AnimationData> _animations;

    // PR-Container-Contract-Cut2: clip stack frames (bounds per push).
    std::vector<ClipEvent> _clipStack;
};

} // namespace ayt::ui
