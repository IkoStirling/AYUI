#pragma once

#include "AYUI/IRenderBackend.h"

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

enum class DisplayListPolicy {
    Retained,
    Immediate
};

// Backend-independent retained command list. It owns authored UI commands
// (rect/text/style/state), never bgfx transient buffers or backend UiItems.
// Replaying still submits a complete frame to the active IRenderBackend.
class DisplayList {
public:
    using Command = std::function<void(IRenderBackend&)>;

    void clear() { _commands.clear(); }
    bool empty() const { return _commands.empty(); }
    size_t size() const { return _commands.size(); }
    void replay(IRenderBackend& renderer) const;

private:
    friend class DisplayListRecorder;
    std::vector<Command> _commands;
};

// Records one Widget::onRender call while forwarding it to the real backend,
// so the rebuild frame has exactly the same output as the immediate path.
// Resource mutation and backend-pass control are intentionally not retained;
// encountering either marks the candidate list uncacheable and Widget keeps
// using its legacy immediate fallback.
class DisplayListRecorder final : public IRenderBackend {
public:
    DisplayListRecorder(IRenderBackend& target, DisplayList& output);

    bool isCacheable() const { return _cacheable; }

    // Some complex containers render selected children inside onRender to
    // preserve background/content/chrome order. Keep that child traversal as
    // a dynamic command instead of flattening the child's current pixels or
    // commands into the parent's list.
    void recordAndForwardNested(DisplayList::Command command);

    void beginFrame() override;
    void endFrame() override;
    void beginCanvas(const math::FRectangle& viewport) override;
    void endCanvas() override;
    void setBlendMode(BlendMode mode) override;
    void pushOpacity(float alpha) override;
    void popOpacity() override;
    void pushTransform(const math::Float4x4& transform) override;
    void popTransform() override;
    void pushClip(const math::FRectangle& bounds) override;
    void popClip() override;
    void setScissor(const math::FRectangle& bounds) override;

    void drawRect(const math::FRectangle& bounds, const math::FVector4& color) override;
    void drawRect(const math::FRectangle& bounds, void* textureHandle,
                  const math::FRectangle& uv) override;
    void drawText(const math::FRectangle& bounds, const std::wstring& text,
                  int fontSize, const math::FVector4& color) override;
    void drawText(const math::FRectangle& bounds, const std::wstring& text,
                  int fontSize, const TextStyle& style) override;
    void drawNinePatch(const math::FRectangle& bounds, void* textureHandle,
                       const math::FRectangle& uvRegion,
                       const math::FVector4& padding) override;
    void drawGradientRect(const math::FRectangle& bounds,
                          const math::FVector4& topLeft,
                          const math::FVector4& topRight,
                          const math::FVector4& bottomLeft,
                          const math::FVector4& bottomRight) override;
    void drawGradientRect(const math::FRectangle& bounds,
                          const math::FVector4& topColor,
                          const math::FVector4& bottomColor) override;
    void drawRect(const math::FRectangle& bounds, const BorderStyle& border) override;
    void drawBorderRect(const math::FRectangle& bounds, const math::FVector4& color,
                        float borderWidth, float cornerRadius = 0) override;
    void drawRoundedRect(const math::FRectangle& bounds, const math::FVector4& color,
                         float cornerRadius = 0) override;
    void drawRoundedRect(const math::FRectangle& bounds, const math::FVector4& color,
                         const CornerRadii& radii) override;
    void drawBorderRect(const math::FRectangle& bounds, const math::FVector4& color,
                        float borderWidth, const CornerRadii& radii) override;
    void drawCard(const math::FRectangle& bounds, const CardStyle& style) override;
    void drawRectShadow(const math::FRectangle& bounds, const ShadowStyle& shadow) override;
    void pushMask() override;
    void popMask() override;
    void drawWithAlpha(const math::FRectangle& bounds, void* textureHandle,
                       float alpha) override;
    void drawSprite(const math::FRectangle& bounds, void* atlasTexture,
                    const wchar_t* spriteName) override;
    void drawRectBlur(const math::FRectangle& bounds, float blurRadius,
                      BlurType type = BlurType::Gaussian) override;

    // Backend/resource commands below are forwarded for the rebuild frame,
    // but make this candidate uncacheable because their handles or lifetime
    // cannot be copied safely into a backend-independent list yet.
    PathHandle createPath() override;
    void releasePath(PathHandle path) override;
    void addPathRect(PathHandle path, const math::FRectangle& bounds,
                     PathWinding winding = PathWinding::CounterClockwise) override;
    void addPathRoundedRect(PathHandle path, const math::FRectangle& bounds,
                            float cornerRadius,
                            PathWinding winding = PathWinding::CounterClockwise) override;
    void addPathEllipse(PathHandle path, const math::FVector2& center,
                        float radiusX, float radiusY,
                        PathWinding winding = PathWinding::CounterClockwise) override;
    void addPathLine(PathHandle path, const math::FVector2& start,
                     const math::FVector2& end) override;
    void addPathBezier(PathHandle path, const math::FVector2& start,
                       const math::FVector2& control1,
                       const math::FVector2& control2,
                       const math::FVector2& end) override;
    void addPathArc(PathHandle path, const math::FVector2& center, float radius,
                    float startAngle, float endAngle,
                    PathWinding winding = PathWinding::CounterClockwise) override;
    void addPathPolygon(PathHandle path, const math::FVector2* points, int count,
                        PathWinding winding = PathWinding::CounterClockwise) override;
    void setPathFillColor(PathHandle path, const math::FVector4& color) override;
    void setPathStrokeColor(PathHandle path, const math::FVector4& color) override;
    void setPathStrokeWidth(PathHandle path, float width) override;
    void drawPath(PathHandle path, PathFillMode mode = PathFillMode::Fill) override;
    void pushPathClip(PathHandle path) override;
    void drawRectBlurWithMask(const math::FRectangle& bounds, PathHandle maskPath,
                              float blurRadius,
                              BlurType type = BlurType::Gaussian) override;

    ParticleHandle createParticleSystem(int maxParticles) override;
    void releaseParticleSystem(ParticleHandle system) override;
    void setParticleStyle(ParticleHandle system, const ParticleStyle& style) override;
    void addParticle(ParticleHandle system, const math::FVector2& position,
                     const math::FVector2& velocity, float life, float size) override;
    void updateParticleSystem(ParticleHandle system, float deltaTime) override;
    void drawParticleSystem(ParticleHandle system) override;

    bool supportsRenderTargets() const override;
    RenderTargetHandle createRenderTarget(int width, int height,
                                          bool hasAlpha = true) override;
    RenderTargetHandle createRenderTarget(const RenderTargetDesc& desc) override;
    bool resizeRenderTarget(RenderTargetHandle target,
                            const RenderTargetDesc& desc) override;
    void releaseRenderTarget(RenderTargetHandle target) override;
    void bindRenderTarget(RenderTargetHandle target) override;
    void* getRenderTargetTexture(RenderTargetHandle target) override;
    void blitRenderTarget(RenderTargetHandle source,
                          const math::FRectangle& destBounds) override;

    LayerHandle createLayer(const LayerDesc& desc) override;
    void releaseLayer(LayerHandle layer) override;
    bool updateLayer(LayerHandle layer, const LayerDesc& desc) override;
    bool beginLayerPaint(LayerHandle layer, const LayerPaint& paint) override;
    void endLayerPaint(LayerHandle layer) override;
    void compositeLayer(LayerHandle layer, const math::FRectangle& destBounds,
                        float opacity = 1.0f) override;
    void invalidateLayer(LayerHandle layer,
                         const math::FRectangle& damage = math::FRectangle()) override;

    AnimationHandle createAnimation(float from, float to, float duration,
                                    AnimationCurve curve = AnimationCurve::EaseOut) override;
    AnimationHandle createAnimationVec2(const math::FVector2& from,
                                        const math::FVector2& to, float duration,
                                        AnimationCurve curve = AnimationCurve::EaseOut) override;
    AnimationHandle createAnimationVec4(const math::FVector4& from,
                                        const math::FVector4& to, float duration,
                                        AnimationCurve curve = AnimationCurve::EaseOut) override;
    void releaseAnimation(AnimationHandle anim) override;
    void setAnimationFlags(AnimationHandle anim, AnimationFlags flags) override;
    bool updateAnimation(AnimationHandle anim, float deltaTime) override;
    float getAnimationValue(AnimationHandle anim) const override;
    math::FVector2 getAnimationValueVec2(AnimationHandle anim) const override;
    math::FVector4 getAnimationValueVec4(AnimationHandle anim) const override;

    bool addColoredQuad(const math::FRectangle& bounds,
                        const math::FVector4& color) override;
    bool addTexturedQuad(const math::FRectangle& bounds, void* textureHandle,
                         const math::FRectangle& uv,
                         const math::FVector4& tint = math::FVector4(1, 1, 1, 1)) override;
    void flushBatches() override;
    void flush() override;
    int getDrawCallCount() const override;
    int getTriangleCount() const override;
    int getVertexCount() const override;
    void pushPerformanceMarker(const char* name) override;
    void popPerformanceMarker() override;

    FontHandle loadFont(const wchar_t* path, int baseSize) override;
    void releaseFont(FontHandle font) override;
    FontHandle getFontHandle(const wchar_t* familyName, int baseSize) override;
    FontHandle registerFontFromMemory(const void* data, size_t dataSize,
                                      int baseSize) override;
    TextMetrics measureText(const std::wstring& text, int fontSize,
                            float maxWidth = 0.0f) const override;
    ayt::font::FontMetrics getFontMetrics(FontHandle font) const override;
    size_t getAvailableVideoMemory() const override;
    std::string getDriverVersion() const override;
    std::string getBackendName() const override;

private:
    void append(DisplayList::Command command);
    void disableCaching() { _cacheable = false; }

    IRenderBackend& _target;
    DisplayList& _output;
    bool _cacheable = true;
};

} // namespace ayt::ui
