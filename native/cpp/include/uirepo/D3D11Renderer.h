#pragma once

#ifdef _WIN32

#include "uirepo/BoundedCache.h"
#include "uirepo/Core.h"

#include <d3d11.h>
#include <dwrite_2.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace uirepo {

class D3D11ImageCache;
class D3D11TextLayoutCache;
struct D3D11RendererTelemetryAccess;
struct D3D11RendererPipelineAccess;
struct D3D11RendererTestAccess;

class DirectWriteTextMeasurer final : public TextMeasurer {
public:
    DirectWriteTextMeasurer();

    [[nodiscard]] Size measure(std::string_view text, float fontSize) const noexcept override;
    [[nodiscard]] Size measure(
        std::string_view text,
        float fontSize,
        float maxWidth,
        TextWrapping wrapping) const noexcept override;

private:
    friend class D3D11Renderer;
    friend struct D3D11RendererTestAccess;

    [[nodiscard]] static std::wstring widen(std::string_view text);

    Microsoft::WRL::ComPtr<IDWriteFactory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory2> factory2_;
    std::wstring fontFamily_{L"Segoe UI"};
};

enum class D3D11TargetColorSpace : std::uint8_t {
    Unknown,
    Linear,
    Srgb,
};

struct D3D11RenderStats {
    std::uint32_t fillRects{};
    std::uint32_t textCommands{};
    std::uint32_t glyphRuns{};
    std::uint32_t imageCommands{};
    std::uint32_t videoCommands{};
    std::uint32_t clipPushes{};
    std::uint32_t clipPops{};
    std::uint32_t roundedRects{};
    std::uint32_t drawCalls{};
    std::uint32_t drawSegments{};
    std::uint32_t imageBudgetFlushes{};
    std::uint32_t commandCount{};
    std::uint32_t glyphCacheEntries{};
    std::size_t glyphCacheBytes{};
    std::uint64_t glyphCacheHits{};
    std::uint64_t glyphCacheMisses{};
    std::uint64_t glyphCacheUploads{};
    std::uint64_t glyphCacheEvictions{};
    std::uint64_t glyphCacheRejections{};
    std::uint32_t imageCacheEntries{};
    std::size_t imageCacheBytes{};
    std::uint64_t imageCacheHits{};
    std::uint64_t imageCacheMisses{};
    std::uint64_t imageCacheUploads{};
    std::uint64_t imageCacheEvictions{};
    std::uint64_t imageCacheRejections{};
    std::uint64_t imageDecodeFailures{};
    std::uint64_t imageNegativeHits{};
    std::uint32_t textLayoutCacheEntries{};
    std::size_t textLayoutCacheBytes{};
    std::uint64_t textLayoutCacheHits{};
    std::uint64_t textLayoutCacheMisses{};
    std::uint64_t textLayoutCacheEvictions{};
    std::uint64_t textLayoutCacheRejections{};
};

class D3D11Renderer final {
public:
    static constexpr std::size_t DefaultGlyphCacheBytes = 16u * 1024u * 1024u;
    static constexpr std::size_t DefaultGlyphCacheEntries = 2048u;
    static constexpr std::size_t DefaultImageCacheBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t DefaultImageCacheEntries = 256u;
    static constexpr std::size_t DefaultLiveImageBudgetBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t DefaultSingleImageBudgetBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t DefaultTextLayoutCacheBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t DefaultTextLayoutCacheEntries = 512u;

    D3D11Renderer(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        std::size_t glyphCacheBytes = DefaultGlyphCacheBytes,
        std::size_t glyphCacheEntries = DefaultGlyphCacheEntries,
        std::size_t imageCacheBytes = DefaultImageCacheBytes,
        std::size_t imageCacheEntries = DefaultImageCacheEntries,
        std::size_t liveImageBudgetBytes = DefaultLiveImageBudgetBytes,
        std::size_t singleImageBudgetBytes = DefaultSingleImageBudgetBytes,
        std::size_t textLayoutCacheBytes = DefaultTextLayoutCacheBytes,
        std::size_t textLayoutCacheEntries = DefaultTextLayoutCacheEntries);
    ~D3D11Renderer();

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    void setTarget(ID3D11RenderTargetView* target, Size targetSize) noexcept;

    [[nodiscard]] ID3D11RenderTargetView* target() const noexcept { return target_.Get(); }
    [[nodiscard]] Size targetSize() const noexcept { return targetSize_; }
    [[nodiscard]] DXGI_FORMAT targetFormat() const noexcept {
        if (!target_) {
            return DXGI_FORMAT_UNKNOWN;
        }
        D3D11_RENDER_TARGET_VIEW_DESC description{};
        target_->GetDesc(&description);
        return description.Format;
    }
    [[nodiscard]] static D3D11TargetColorSpace classifyTargetColorSpace(DXGI_FORMAT format) noexcept {
        switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            return D3D11TargetColorSpace::Linear;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return D3D11TargetColorSpace::Srgb;
        default:
            return D3D11TargetColorSpace::Unknown;
        }
    }
    [[nodiscard]] D3D11TargetColorSpace targetColorSpace() const noexcept {
        return classifyTargetColorSpace(targetFormat());
    }
    [[nodiscard]] static const char* targetColorSpaceName(D3D11TargetColorSpace colorSpace) noexcept {
        switch (colorSpace) {
        case D3D11TargetColorSpace::Linear:
            return "linear";
        case D3D11TargetColorSpace::Srgb:
            return "srgb";
        default:
            return "unknown";
        }
    }
    [[nodiscard]] DirectWriteTextMeasurer& textMeasurer() noexcept { return textMeasurer_; }
    [[nodiscard]] const DirectWriteTextMeasurer& textMeasurer() const noexcept { return textMeasurer_; }
    [[nodiscard]] const render::CacheStats& glyphCacheStats() const noexcept { return glyphCache_.stats(); }

    [[nodiscard]] bool render(
        std::span<const DrawCommand> commands,
        D3D11RenderStats* stats = nullptr) noexcept;

private:
    friend struct D3D11RendererTelemetryAccess;
    friend struct D3D11RendererPipelineAccess;
    friend struct D3D11RendererTestAccess;

    struct Vertex {
        float x;
        float y;
        float r;
        float g;
        float b;
        float a;
        float u;
        float v;
        float rectWidth;
        float rectHeight;
        float cornerRadius;
    };

    struct BatchSegment {
        D3D11_RECT scissor{};
        ID3D11PixelShader* shader{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource;
        bool textured{};
        UINT indexOffset{};
        UINT indexCount{};
    };

    struct GlyphRunKey {
        std::uintptr_t fontFaceIdentity{};
        std::uint32_t fontEmSizeBits{};
        std::uint32_t bidiLevel{};
        std::uint32_t renderingMode{};
        std::uint32_t measuringMode{};
        std::uint32_t textureType{};
        std::uint32_t fractionalOriginXBits{};
        std::uint32_t fractionalOriginYBits{};
        bool sideways{};
        bool hasExplicitAdvances{};
        std::vector<std::uint16_t> glyphIndices;
        std::vector<std::uint32_t> advanceBits;
        std::vector<std::uint32_t> advanceOffsetBits;
        std::vector<std::uint32_t> ascenderOffsetBits;

        friend bool operator==(const GlyphRunKey&, const GlyphRunKey&) noexcept = default;
    };

    struct GlyphRunKeyHash {
        [[nodiscard]] std::size_t operator()(const GlyphRunKey& key) const noexcept;
    };

    struct GlyphBitmap {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource;
        float offsetX{};
        float offsetY{};
        std::uint32_t width{};
        std::uint32_t height{};
        float u0{};
        float v0{};
        float u1{1.0f};
        float v1{1.0f};
        std::vector<std::uint8_t> pixels;
    };

    struct GlyphAtlas {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource;
        std::uint32_t cursorX{};
        std::uint32_t cursorY{};
        std::uint32_t rowHeight{};
    };

    using GlyphCache = render::BoundedLruCache<GlyphRunKey, GlyphBitmap, GlyphRunKeyHash>;

    void createPipeline();
    [[nodiscard]] bool drawFillRect(const FillRectCommand& command, Rect clip) noexcept;
    [[nodiscard]] bool drawText(const DrawTextCommand& command, Rect clip, std::uint32_t& glyphRuns) noexcept;
    [[nodiscard]] bool drawImage(
        const DrawImageCommand& command,
        Rect clip,
        D3D11RenderStats& stats) noexcept;
    [[nodiscard]] bool admitImageToBatch(
        ID3D11ShaderResourceView* resource,
        std::size_t bytes,
        D3D11RenderStats& stats) noexcept;
    [[nodiscard]] bool reserveLiveImageBudget(std::size_t bytes, D3D11RenderStats& stats) noexcept;
    [[nodiscard]] static bool beforeImageUpload(std::size_t bytes, void* context) noexcept;
    [[nodiscard]] bool drawGlyphBitmap(
        Rect bounds,
        Rect clip,
        Color color,
        float opacity,
        ID3D11ShaderResourceView* resource,
        float u0,
        float v0,
        float u1,
        float v1) noexcept;
    [[nodiscard]] bool drawTexturedQuad(
        Rect bounds,
        Rect clip,
        float opacity,
        ID3D11ShaderResourceView* resource,
        ID3D11PixelShader* shader) noexcept;
    [[nodiscard]] bool createGlyphBitmap(
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        const DWRITE_GLYPH_RUN& glyphRun,
        GlyphBitmap& bitmap,
        std::size_t& bytes) noexcept;
    [[nodiscard]] bool uploadGlyphBitmap(GlyphBitmap& bitmap) noexcept;
    [[nodiscard]] DWRITE_TEXTURE_TYPE glyphTextureType() const noexcept;
    [[nodiscard]] GlyphRunKey makeGlyphRunKey(
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        const DWRITE_GLYPH_RUN& glyphRun) const;
    [[nodiscard]] Rect currentClip() const noexcept;
    [[nodiscard]] static Rect intersect(Rect left, Rect right) noexcept;
    [[nodiscard]] static bool empty(Rect rect) noexcept;
    [[nodiscard]] D3D11_RECT scissorFromClip(Rect clip) const noexcept;
    void resetBatch() noexcept;
    void discardBatch() noexcept;
    void appendQuad(
        const std::array<Vertex, 4>& quad,
        const D3D11_RECT& scissor,
        ID3D11PixelShader* shader,
        ID3D11ShaderResourceView* resource,
        bool textured) noexcept;
    [[nodiscard]] bool ensureBatchBuffers(std::size_t vertexCount, std::size_t indexCount) noexcept;
    [[nodiscard]] bool flushBatch(std::uint32_t& drawSegments) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Size targetSize_{};

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> solidPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> textPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> imagePixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer_;
    std::size_t vertexCapacity_{};
    std::size_t indexCapacity_{};
    std::vector<Vertex> batchVertices_;
    std::vector<std::uint32_t> batchIndices_;
    std::vector<BatchSegment> batchSegments_;
    bool batchValid_{true};
    std::size_t liveImageBudgetBytes_{};
    std::vector<ID3D11ShaderResourceView*> liveImageSrvs_;
    std::size_t liveImageBytes_{};
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterState_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState_;

    DirectWriteTextMeasurer textMeasurer_;
    GlyphCache glyphCache_;
    GlyphAtlas glyphAtlas_;
    std::unique_ptr<D3D11ImageCache> imageCache_;
    std::unique_ptr<D3D11TextLayoutCache> textLayoutCache_;
    std::vector<Rect> clipStack_;
    std::uint64_t glyphUploads_{};
};

} // namespace uirepo

#endif
