#include "uirepo/D3D11Renderer.h"

#ifdef _WIN32

#include "D3D11ImageCache.h"
#include "uirepo/ImagePolicy.h"
#include "D3D11StateGuard.h"
#include "D3D11TextLayout.h"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace uirepo {
namespace {

constexpr char kShaderSource[] = R"(
Texture2D sampledTexture : register(t0);
SamplerState linearSampler : register(s0);

struct VSInput {
    float2 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 rect : TEXCOORD1;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 rect : TEXCOORD1;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.color = input.color;
    output.uv = input.uv;
    output.rect = input.rect;
    return output;
}

float4 PSSolid(VSOutput input) : SV_TARGET {
    if (input.rect.z <= 0.0f) {
        return input.color;
    }

    float2 halfSize = input.rect.xy * 0.5f;
    float radius = min(input.rect.z, min(halfSize.x, halfSize.y));
    float2 local = input.uv * input.rect.xy - halfSize;
    float2 q = abs(local) - (halfSize - radius);
    float distance = length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - radius;
    float antialias = max(fwidth(distance), 0.5f);
    float coverage = saturate(0.5f - distance / antialias);
    clip(coverage - (1.0f / 255.0f));
    return float4(input.color.rgb, input.color.a * coverage);
}

float4 PSText(VSOutput input) : SV_TARGET {
    float coverage = sampledTexture.Sample(linearSampler, input.uv).r;
    return float4(input.color.rgb, input.color.a * coverage);
}

float4 PSImage(VSOutput input) : SV_TARGET {
    return sampledTexture.Sample(linearSampler, input.uv) * input.color;
}
)";

constexpr DWRITE_RENDERING_MODE kGlyphRenderingMode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
constexpr DWRITE_MEASURING_MODE kGlyphMeasuringMode = DWRITE_MEASURING_MODE_NATURAL;
constexpr DWRITE_TEXTURE_TYPE kGlyphTextureType = DWRITE_TEXTURE_CLEARTYPE_3x1;
constexpr DWRITE_TEXTURE_TYPE kGlyphGrayscaleTextureType = DWRITE_TEXTURE_ALIASED_1x1;

void throwIfFailed(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::runtime_error(operation);
    }
}

[[nodiscard]] float clamp01(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] std::size_t minNonZero(std::size_t a, std::size_t b) noexcept {
    if (a == 0) {
        return b;
    }
    if (b == 0) {
        return a;
    }
    return a < b ? a : b;
}

[[nodiscard]] std::uint32_t floatBits(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] float fractionalPart(float value) noexcept {
    return value - std::floor(value);
}

void hashCombine(std::size_t& seed, std::size_t value) noexcept {
    seed ^= value + static_cast<std::size_t>(0x9e3779b9u) + (seed << 6u) + (seed >> 2u);
}

template <class T>
void hashValue(std::size_t& seed, const T& value) noexcept {
    hashCombine(seed, std::hash<T>{}(value));
}

template <class T>
void hashRange(std::size_t& seed, const std::vector<T>& values) noexcept {
    hashValue(seed, values.size());
    for (const auto& value : values) {
        hashValue(seed, value);
    }
}

struct GlyphDrawContext {
    void* owner{};
    Rect clip{};
    Color color{};
    float opacity{1.0f};
    std::uint32_t* glyphRuns{};
    HRESULT (*drawGlyphRun)(
        GlyphDrawContext&,
        FLOAT,
        FLOAT,
        const DWRITE_GLYPH_RUN*) noexcept {};
};

class TextRendererAdapter final : public IDWriteTextRenderer {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;

        if (iid == __uuidof(IUnknown) ||
            iid == __uuidof(IDWritePixelSnapping) ||
            iid == __uuidof(IDWriteTextRenderer)) {
            *object = static_cast<IDWriteTextRenderer*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(
        void*,
        BOOL* isDisabled) override {
        if (isDisabled == nullptr) {
            return E_POINTER;
        }
        *isDisabled = FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCurrentTransform(
        void*,
        DWRITE_MATRIX* transform) override {
        if (transform == nullptr) {
            return E_POINTER;
        }
        *transform = DWRITE_MATRIX{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(
        void*,
        FLOAT* pixelsPerDip) override {
        if (pixelsPerDip == nullptr) {
            return E_POINTER;
        }
        *pixelsPerDip = 1.0f;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(
        void* clientDrawingContext,
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        DWRITE_MEASURING_MODE,
        const DWRITE_GLYPH_RUN* glyphRun,
        const DWRITE_GLYPH_RUN_DESCRIPTION*,
        IUnknown*) override {
        if (clientDrawingContext == nullptr || glyphRun == nullptr) {
            return E_INVALIDARG;
        }

        auto& context = *static_cast<GlyphDrawContext*>(clientDrawingContext);
        if (context.drawGlyphRun == nullptr) {
            return E_FAIL;
        }
        return context.drawGlyphRun(context, baselineOriginX, baselineOriginY, glyphRun);
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(
        void*,
        FLOAT,
        FLOAT,
        const DWRITE_UNDERLINE*,
        IUnknown*) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawStrikethrough(
        void*,
        FLOAT,
        FLOAT,
        const DWRITE_STRIKETHROUGH*,
        IUnknown*) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawInlineObject(
        void*,
        FLOAT,
        FLOAT,
        IDWriteInlineObject*,
        BOOL,
        BOOL,
        IUnknown*) override {
        return E_NOTIMPL;
    }

private:
    std::atomic<ULONG> references_{1};
};

} // namespace

DirectWriteTextMeasurer::DirectWriteTextMeasurer() {
    throwIfFailed(
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory_.GetAddressOf())),
        "DWriteCreateFactory failed");
    static_cast<void>(factory_.As(&factory2_));
}

std::wstring DirectWriteTextMeasurer::widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        required);
    if (converted != required) {
        return {};
    }
    return result;
}

Size DirectWriteTextMeasurer::measure(std::string_view text, float fontSize) const noexcept {
    if (!factory_ || !std::isfinite(fontSize) || fontSize <= 0.0f) {
        return Size::Zero();
    }

    try {
        const std::wstring wide = widen(text);
        if (wide.empty() && !text.empty()) {
            return Size::Zero();
        }

        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        if (FAILED(factory_->CreateTextFormat(
                fontFamily_.c_str(),
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                fontSize,
                L"",
                format.GetAddressOf()))) {
            return Size::Zero();
        }

        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(factory_->CreateTextLayout(
                wide.c_str(),
                static_cast<UINT32>(wide.size()),
                format.Get(),
                100000.0f,
                100000.0f,
                layout.GetAddressOf()))) {
            return Size::Zero();
        }

        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics))) {
            return Size::Zero();
        }

        return {
            std::max(0.0f, metrics.widthIncludingTrailingWhitespace),
            std::max(0.0f, metrics.height),
        };
    } catch (...) {
        return Size::Zero();
    }
}

D3D11Renderer::D3D11Renderer(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    std::size_t glyphCacheBytes,
    std::size_t glyphCacheEntries,
    std::size_t imageCacheBytes,
    std::size_t imageCacheEntries,
    std::size_t liveImageBudgetBytes,
    std::size_t singleImageBudgetBytes,
    std::size_t textLayoutCacheBytes,
    std::size_t textLayoutCacheEntries)
    : device_(device),
      context_(context),
      glyphCache_(glyphCacheBytes, glyphCacheEntries),
      imageCache_(std::make_unique<D3D11ImageCache>(
          device,
          imageCacheBytes,
          imageCacheEntries,
          minNonZero(singleImageBudgetBytes, liveImageBudgetBytes))),
      textLayoutCache_(std::make_unique<D3D11TextLayoutCache>(
          textMeasurer_.factory_.Get(),
          textMeasurer_.fontFamily_,
          textLayoutCacheBytes,
          textLayoutCacheEntries)),
      liveImageBudgetBytes_(liveImageBudgetBytes) {
    if (!device_ || !context_) {
        throw std::invalid_argument("D3D11Renderer requires a device and immediate context");
    }

    clipStack_.reserve(16);
    createPipeline();
}

D3D11Renderer::~D3D11Renderer() = default;

void D3D11Renderer::setTarget(ID3D11RenderTargetView* target, Size targetSize) noexcept {
    target_ = target;
    targetSize_ = targetSize;
}

std::size_t D3D11Renderer::GlyphRunKeyHash::operator()(const GlyphRunKey& key) const noexcept {
    std::size_t seed{};
    hashValue(seed, key.fontFaceIdentity);
    hashValue(seed, key.fontEmSizeBits);
    hashValue(seed, key.bidiLevel);
    hashValue(seed, key.renderingMode);
    hashValue(seed, key.measuringMode);
    hashValue(seed, key.textureType);
    hashValue(seed, key.fractionalOriginXBits);
    hashValue(seed, key.fractionalOriginYBits);
    hashValue(seed, key.sideways);
    hashValue(seed, key.hasExplicitAdvances);
    hashRange(seed, key.glyphIndices);
    hashRange(seed, key.advanceBits);
    hashRange(seed, key.advanceOffsetBits);
    hashRange(seed, key.ascenderOffsetBits);
    return seed;
}

DWRITE_TEXTURE_TYPE D3D11Renderer::glyphTextureType() const noexcept {
    return textMeasurer_.factory2_ != nullptr ? kGlyphGrayscaleTextureType : kGlyphTextureType;
}

D3D11Renderer::GlyphRunKey D3D11Renderer::makeGlyphRunKey(
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    const DWRITE_GLYPH_RUN& glyphRun) const {
    GlyphRunKey key{};
    key.fontFaceIdentity = reinterpret_cast<std::uintptr_t>(glyphRun.fontFace);
    key.fontEmSizeBits = floatBits(glyphRun.fontEmSize);
    key.bidiLevel = glyphRun.bidiLevel;
    key.renderingMode = static_cast<std::uint32_t>(kGlyphRenderingMode);
    key.measuringMode = static_cast<std::uint32_t>(kGlyphMeasuringMode);
    key.textureType = static_cast<std::uint32_t>(glyphTextureType());
    key.fractionalOriginXBits = floatBits(fractionalPart(baselineOriginX));
    key.fractionalOriginYBits = floatBits(fractionalPart(baselineOriginY));
    key.sideways = glyphRun.isSideways != FALSE;
    key.hasExplicitAdvances = glyphRun.glyphAdvances != nullptr;

    const std::size_t glyphCount = glyphRun.glyphCount;
    if (glyphCount != 0 && glyphRun.glyphIndices == nullptr) {
        return {};
    }
    if (glyphCount != 0) {
        key.glyphIndices.assign(glyphRun.glyphIndices, glyphRun.glyphIndices + glyphCount);
    }
    key.advanceBits.reserve(glyphCount);
    key.advanceOffsetBits.reserve(glyphCount);
    key.ascenderOffsetBits.reserve(glyphCount);
    for (std::size_t index = 0; index < glyphCount; ++index) {
        const float advance = glyphRun.glyphAdvances != nullptr ? glyphRun.glyphAdvances[index] : 0.0f;
        const DWRITE_GLYPH_OFFSET offset = glyphRun.glyphOffsets != nullptr
            ? glyphRun.glyphOffsets[index]
            : DWRITE_GLYPH_OFFSET{};
        key.advanceBits.push_back(floatBits(advance));
        key.advanceOffsetBits.push_back(floatBits(offset.advanceOffset));
        key.ascenderOffsetBits.push_back(floatBits(offset.ascenderOffset));
    }
    return key;
}

void D3D11Renderer::createPipeline() {
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    Microsoft::WRL::ComPtr<ID3DBlob> vertexBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> solidBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> textBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> imageBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;

    HRESULT result = D3DCompile(
        kShaderSource,
        sizeof(kShaderSource) - 1,
        "UIRepoShader",
        nullptr,
        nullptr,
        "VSMain",
        "vs_4_0",
        compileFlags,
        0,
        vertexBytecode.GetAddressOf(),
        errors.GetAddressOf());
    throwIfFailed(result, "D3DCompile vertex shader failed");

    errors.Reset();
    result = D3DCompile(
        kShaderSource,
        sizeof(kShaderSource) - 1,
        "UIRepoShader",
        nullptr,
        nullptr,
        "PSSolid",
        "ps_4_0",
        compileFlags,
        0,
        solidBytecode.GetAddressOf(),
        errors.GetAddressOf());
    throwIfFailed(result, "D3DCompile solid pixel shader failed");

    errors.Reset();
    result = D3DCompile(
        kShaderSource,
        sizeof(kShaderSource) - 1,
        "UIRepoShader",
        nullptr,
        nullptr,
        "PSText",
        "ps_4_0",
        compileFlags,
        0,
        textBytecode.GetAddressOf(),
        errors.GetAddressOf());
    throwIfFailed(result, "D3DCompile text pixel shader failed");

    errors.Reset();
    result = D3DCompile(
        kShaderSource,
        sizeof(kShaderSource) - 1,
        "UIRepoShader",
        nullptr,
        nullptr,
        "PSImage",
        "ps_4_0",
        compileFlags,
        0,
        imageBytecode.GetAddressOf(),
        errors.GetAddressOf());
    throwIfFailed(result, "D3DCompile image pixel shader failed");

    throwIfFailed(
        device_->CreateVertexShader(
            vertexBytecode->GetBufferPointer(),
            vertexBytecode->GetBufferSize(),
            nullptr,
            vertexShader_.GetAddressOf()),
        "CreateVertexShader failed");

    throwIfFailed(
        device_->CreatePixelShader(
            solidBytecode->GetBufferPointer(),
            solidBytecode->GetBufferSize(),
            nullptr,
            solidPixelShader_.GetAddressOf()),
        "CreatePixelShader solid failed");

    throwIfFailed(
        device_->CreatePixelShader(
            textBytecode->GetBufferPointer(),
            textBytecode->GetBufferSize(),
            nullptr,
            textPixelShader_.GetAddressOf()),
        "CreatePixelShader text failed");

    throwIfFailed(
        device_->CreatePixelShader(
            imageBytecode->GetBufferPointer(),
            imageBytecode->GetBufferSize(),
            nullptr,
            imagePixelShader_.GetAddressOf()),
        "CreatePixelShader image failed");

    constexpr D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    throwIfFailed(
        device_->CreateInputLayout(
            inputElements,
            static_cast<UINT>(std::size(inputElements)),
            vertexBytecode->GetBufferPointer(),
            vertexBytecode->GetBufferSize(),
            inputLayout_.GetAddressOf()),
        "CreateInputLayout failed");

    D3D11_BLEND_DESC blendDescription{};
    blendDescription.RenderTarget[0].BlendEnable = TRUE;
    blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    throwIfFailed(
        device_->CreateBlendState(&blendDescription, blendState_.GetAddressOf()),
        "CreateBlendState failed");

    D3D11_DEPTH_STENCIL_DESC depthDescription{};
    depthDescription.DepthEnable = FALSE;
    depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depthDescription.StencilEnable = FALSE;
    throwIfFailed(
        device_->CreateDepthStencilState(&depthDescription, depthState_.GetAddressOf()),
        "CreateDepthStencilState failed");

    D3D11_RASTERIZER_DESC rasterDescription{};
    rasterDescription.FillMode = D3D11_FILL_SOLID;
    rasterDescription.CullMode = D3D11_CULL_NONE;
    rasterDescription.DepthClipEnable = TRUE;
    rasterDescription.ScissorEnable = TRUE;
    throwIfFailed(
        device_->CreateRasterizerState(&rasterDescription, rasterState_.GetAddressOf()),
        "CreateRasterizerState failed");

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MinLOD = 0.0f;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    throwIfFailed(
        device_->CreateSamplerState(&samplerDescription, samplerState_.GetAddressOf()),
        "CreateSamplerState failed");
}

Rect D3D11Renderer::intersect(Rect left, Rect right) noexcept {
    const float x1 = std::max(left.x, right.x);
    const float y1 = std::max(left.y, right.y);
    const float x2 = std::min(left.x + left.width, right.x + right.width);
    const float y2 = std::min(left.y + left.height, right.y + right.height);
    return {
        x1,
        y1,
        std::max(0.0f, x2 - x1),
        std::max(0.0f, y2 - y1),
    };
}

bool D3D11Renderer::empty(Rect rect) noexcept {
    return rect.width <= 0.0f || rect.height <= 0.0f;
}

Rect D3D11Renderer::currentClip() const noexcept {
    if (clipStack_.empty()) {
        return {0.0f, 0.0f, targetSize_.width, targetSize_.height};
    }
    return clipStack_.back();
}

D3D11_RECT D3D11Renderer::scissorFromClip(Rect clip) const noexcept {
    if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
        !std::isfinite(clip.width) || !std::isfinite(clip.height)) {
        return D3D11_RECT{0, 0, 0, 0};
    }
    const double maxX = std::ceil(static_cast<double>(targetSize_.width));
    const double maxY = std::ceil(static_cast<double>(targetSize_.height));
    const double left = std::clamp(std::floor(static_cast<double>(clip.x)), 0.0, maxX);
    const double top = std::clamp(std::floor(static_cast<double>(clip.y)), 0.0, maxY);
    const double right = std::clamp(
        std::ceil(static_cast<double>(clip.x) + static_cast<double>(clip.width)), 0.0, maxX);
    const double bottom = std::clamp(
        std::ceil(static_cast<double>(clip.y) + static_cast<double>(clip.height)), 0.0, maxY);
    return D3D11_RECT{
        static_cast<LONG>(left),
        static_cast<LONG>(top),
        static_cast<LONG>(std::max(left, right)),
        static_cast<LONG>(std::max(top, bottom)),
    };
}

namespace {
[[nodiscard]] bool validRect(Rect rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.width) && std::isfinite(rect.height) &&
           rect.width >= 0.0f && rect.height >= 0.0f;
}

constexpr std::size_t kMaxBatchVertices = std::size_t{1} << 22;
constexpr std::size_t kMaxBatchIndices = std::size_t{1} << 23;
constexpr std::uint32_t kGlyphAtlasDimension = 2048u;
constexpr std::uint32_t kGlyphAtlasPadding = 1u;
} // namespace

void D3D11Renderer::discardBatch() noexcept {
    batchVertices_.clear();
    batchIndices_.clear();
    batchSegments_.clear();
    liveImageSrvs_.clear();
    liveImageBytes_ = 0;
    batchValid_ = false;
}

void D3D11Renderer::resetBatch() noexcept {
    discardBatch();
    batchValid_ = true;
}

namespace {
[[nodiscard]] bool sameScissor(const D3D11_RECT& a, const D3D11_RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
} // namespace

void D3D11Renderer::appendQuad(
    const std::array<Vertex, 4>& quad,
    const D3D11_RECT& scissor,
    ID3D11PixelShader* shader,
    ID3D11ShaderResourceView* resource,
    bool textured) noexcept {
    if (!batchValid_) {
        return;
    }
    if (batchVertices_.size() + 4u > kMaxBatchVertices ||
        batchIndices_.size() + 6u > kMaxBatchIndices) {
        batchValid_ = false;
        return;
    }
    try {
        BatchSegment* segment = batchSegments_.empty() ? nullptr : &batchSegments_.back();
        if (segment == nullptr || segment->shader != shader || segment->textured != textured ||
            segment->resource.Get() != resource || !sameScissor(segment->scissor, scissor)) {
            BatchSegment fresh{};
            fresh.scissor = scissor;
            fresh.shader = shader;
            fresh.resource = resource;
            fresh.textured = textured;
            fresh.indexOffset = static_cast<UINT>(batchIndices_.size());
            batchSegments_.push_back(std::move(fresh));
            segment = &batchSegments_.back();
        }

        const std::uint32_t base = static_cast<std::uint32_t>(batchVertices_.size());
        batchVertices_.insert(batchVertices_.end(), quad.begin(), quad.end());
        const std::array<std::uint32_t, 6> indices{
            base + 0, base + 1, base + 2, base + 0, base + 2, base + 3};
        batchIndices_.insert(batchIndices_.end(), indices.begin(), indices.end());
        segment->indexCount += 6;
    } catch (...) {
        batchValid_ = false;
    }
}

bool D3D11Renderer::ensureBatchBuffers(std::size_t vertexCount, std::size_t indexCount) noexcept {
    if (!vertexBuffer_ || vertexCount > vertexCapacity_) {
        const std::size_t capacity = (std::max<std::size_t>)(vertexCount + 5000u, 4096u);
        const std::size_t byteWidth = sizeof(Vertex) * capacity;
        if (byteWidth > static_cast<std::size_t>(UINT32_MAX)) {
            return false;
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(byteWidth);
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        vertexBuffer_.Reset();
        if (FAILED(device_->CreateBuffer(&description, nullptr, vertexBuffer_.GetAddressOf()))) {
            vertexCapacity_ = 0;
            return false;
        }
        vertexCapacity_ = capacity;
    }
    if (!indexBuffer_ || indexCount > indexCapacity_) {
        const std::size_t capacity = (std::max<std::size_t>)(indexCount + 10000u, 8192u);
        const std::size_t byteWidth = sizeof(std::uint32_t) * capacity;
        if (byteWidth > static_cast<std::size_t>(UINT32_MAX)) {
            return false;
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(byteWidth);
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        indexBuffer_.Reset();
        if (FAILED(device_->CreateBuffer(&description, nullptr, indexBuffer_.GetAddressOf()))) {
            indexCapacity_ = 0;
            return false;
        }
        indexCapacity_ = capacity;
    }
    return true;
}

bool D3D11Renderer::drawFillRect(const FillRectCommand& command, Rect clip) noexcept {
    if (!context_ || targetSize_.width <= 0.0f || targetSize_.height <= 0.0f) {
        return false;
    }

    if (!validRect(command.bounds) || !validRect(clip)) {
        return true;
    }

    if (empty(intersect(command.bounds, clip))) {
        return true;
    }

    const float left = command.bounds.x / targetSize_.width * 2.0f - 1.0f;
    const float right = (command.bounds.x + command.bounds.width) / targetSize_.width * 2.0f - 1.0f;
    const float top = 1.0f - command.bounds.y / targetSize_.height * 2.0f;
    const float bottom = 1.0f - (command.bounds.y + command.bounds.height) / targetSize_.height * 2.0f;

    const float r = static_cast<float>(command.color.r) / 255.0f;
    const float g = static_cast<float>(command.color.g) / 255.0f;
    const float b = static_cast<float>(command.color.b) / 255.0f;
    const float a = static_cast<float>(command.color.a) / 255.0f * clamp01(command.opacity);
    const float w = command.bounds.width;
    const float h = command.bounds.height;
    const float maxRadius = 0.5f * std::min(w, h);
    const float radius = std::isfinite(command.cornerRadius)
        ? std::clamp(command.cornerRadius, 0.0f, std::max(0.0f, maxRadius))
        : 0.0f;

    const std::array<Vertex, 4> quad{{
        {left, top, r, g, b, a, 0.0f, 0.0f, w, h, radius},
        {right, top, r, g, b, a, 1.0f, 0.0f, w, h, radius},
        {right, bottom, r, g, b, a, 1.0f, 1.0f, w, h, radius},
        {left, bottom, r, g, b, a, 0.0f, 1.0f, w, h, radius},
    }};

    appendQuad(quad, scissorFromClip(clip), solidPixelShader_.Get(), nullptr, false);
    return true;
}

bool D3D11Renderer::drawGlyphBitmap(
    Rect bounds,
    Rect clip,
    Color color,
    float opacity,
    ID3D11ShaderResourceView* resource,
    float u0,
    float v0,
    float u1,
    float v1) noexcept {
    if (resource == nullptr || !validRect(bounds) || !validRect(clip) || empty(bounds) ||
        empty(intersect(bounds, clip))) {
        return true;
    }
    if (!context_ || targetSize_.width <= 0.0f || targetSize_.height <= 0.0f) {
        return false;
    }

    const float left = bounds.x / targetSize_.width * 2.0f - 1.0f;
    const float right = (bounds.x + bounds.width) / targetSize_.width * 2.0f - 1.0f;
    const float top = 1.0f - bounds.y / targetSize_.height * 2.0f;
    const float bottom = 1.0f - (bounds.y + bounds.height) / targetSize_.height * 2.0f;

    const float r = static_cast<float>(color.r) / 255.0f;
    const float g = static_cast<float>(color.g) / 255.0f;
    const float b = static_cast<float>(color.b) / 255.0f;
    const float a = static_cast<float>(color.a) / 255.0f * clamp01(opacity);

    const std::array<Vertex, 4> quad{{
        {left, top, r, g, b, a, u0, v0, 0.0f, 0.0f, 0.0f},
        {right, top, r, g, b, a, u1, v0, 0.0f, 0.0f, 0.0f},
        {right, bottom, r, g, b, a, u1, v1, 0.0f, 0.0f, 0.0f},
        {left, bottom, r, g, b, a, u0, v1, 0.0f, 0.0f, 0.0f},
    }};

    appendQuad(quad, scissorFromClip(clip), textPixelShader_.Get(), resource, true);
    return true;
}

bool D3D11Renderer::drawTexturedQuad(
    Rect bounds,
    Rect clip,
    float opacity,
    ID3D11ShaderResourceView* resource,
    ID3D11PixelShader* shader) noexcept {
    if (resource == nullptr || shader == nullptr || !validRect(bounds) || !validRect(clip) ||
        empty(bounds) || empty(intersect(bounds, clip))) {
        return true;
    }
    if (!context_ || targetSize_.width <= 0.0f || targetSize_.height <= 0.0f) {
        return false;
    }

    const float left = bounds.x / targetSize_.width * 2.0f - 1.0f;
    const float right = (bounds.x + bounds.width) / targetSize_.width * 2.0f - 1.0f;
    const float top = 1.0f - bounds.y / targetSize_.height * 2.0f;
    const float bottom = 1.0f - (bounds.y + bounds.height) / targetSize_.height * 2.0f;
    const float alpha = clamp01(opacity);

    const std::array<Vertex, 4> quad{{
        {left, top, 1.0f, 1.0f, 1.0f, alpha, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {right, top, 1.0f, 1.0f, 1.0f, alpha, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {right, bottom, 1.0f, 1.0f, 1.0f, alpha, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        {left, bottom, 1.0f, 1.0f, 1.0f, alpha, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
    }};

    appendQuad(quad, scissorFromClip(clip), shader, resource, true);
    return true;
}

namespace {
struct BudgetFlushContext {
    D3D11Renderer* self;
    D3D11RenderStats* stats;
};
} // namespace

bool D3D11Renderer::reserveLiveImageBudget(std::size_t bytes, D3D11RenderStats& stats) noexcept {
    if (liveImageBudgetBytes_ == 0) {
        return true;
    }
    if (bytes > liveImageBudgetBytes_) {
        return false;
    }
    if (liveImageBytes_ != 0 && bytes > liveImageBudgetBytes_ - liveImageBytes_) {
        if (!flushBatch(stats.drawSegments)) {
            discardBatch();
            return false;
        }
        resetBatch();
        ++stats.imageBudgetFlushes;
    }
    return true;
}

bool D3D11Renderer::beforeImageUpload(std::size_t bytes, void* context) noexcept {
    auto* ctx = static_cast<BudgetFlushContext*>(context);
    return ctx->self->reserveLiveImageBudget(bytes, *ctx->stats);
}

bool D3D11Renderer::admitImageToBatch(
    ID3D11ShaderResourceView* resource,
    std::size_t bytes,
    D3D11RenderStats& stats) noexcept {
    for (ID3D11ShaderResourceView* live : liveImageSrvs_) {
        if (live == resource) {
            return true;
        }
    }
    if (!reserveLiveImageBudget(bytes, stats)) {
        return false;
    }
    try {
        liveImageSrvs_.push_back(resource);
    } catch (...) {
        batchValid_ = false;
        return false;
    }
    liveImageBytes_ += bytes;
    return true;
}

bool D3D11Renderer::drawImage(
    const DrawImageCommand& command,
    Rect clip,
    D3D11RenderStats& stats) noexcept {
    if (command.source.empty() || empty(command.bounds) || empty(intersect(command.bounds, clip))) {
        return true;
    }
    if (!imageCache_) {
        return false;
    }

    BudgetFlushContext budgetContext{this, &stats};
    const auto resolved = imageCache_->resolve(command.source, &beforeImageUpload, &budgetContext);
    if (!resolved.success || !resolved.texture.resource) {
        return true;
    }

    const std::size_t bytes = static_cast<std::size_t>(resolved.texture.width) *
                              static_cast<std::size_t>(resolved.texture.height) *
                              render::Rgba8BytesPerPixel;
    if (!admitImageToBatch(resolved.texture.resource.Get(), bytes, stats)) {
        return false;
    }

    if (!drawTexturedQuad(
            command.bounds,
            clip,
            command.opacity,
            resolved.texture.resource.Get(),
            imagePixelShader_.Get())) {
        return false;
    }
    ++stats.drawCalls;
    return true;
}

bool D3D11Renderer::flushBatch(std::uint32_t& drawSegments) noexcept {
    if (batchIndices_.empty()) {
        return true;
    }
    if (batchVertices_.size() > static_cast<std::size_t>(UINT32_MAX)) {
        return false;
    }
    if (!ensureBatchBuffers(batchVertices_.size(), batchIndices_.size())) {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mappedVertices{};
    if (FAILED(context_->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedVertices))) {
        return false;
    }
    std::memcpy(
        mappedVertices.pData, batchVertices_.data(), sizeof(Vertex) * batchVertices_.size());
    context_->Unmap(vertexBuffer_.Get(), 0);

    D3D11_MAPPED_SUBRESOURCE mappedIndices{};
    if (FAILED(context_->Map(indexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedIndices))) {
        return false;
    }
    std::memcpy(
        mappedIndices.pData,
        batchIndices_.data(),
        sizeof(std::uint32_t) * batchIndices_.size());
    context_->Unmap(indexBuffer_.Get(), 0);

    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    ID3D11Buffer* vertexBuffer = vertexBuffer_.Get();
    context_->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context_->IASetIndexBuffer(indexBuffer_.Get(), DXGI_FORMAT_R32_UINT, 0);

    ID3D11SamplerState* rawSampler = samplerState_.Get();
    ID3D11PixelShader* boundShader = nullptr;
    ID3D11ShaderResourceView* boundResource = nullptr;
    bool samplerBound = false;
    bool haveScissor = false;
    D3D11_RECT boundScissor{};

    for (const BatchSegment& segment : batchSegments_) {
        if (segment.indexCount == 0) {
            continue;
        }
        if (!haveScissor || !sameScissor(boundScissor, segment.scissor)) {
            context_->RSSetScissorRects(1, &segment.scissor);
            boundScissor = segment.scissor;
            haveScissor = true;
        }
        if (boundShader != segment.shader) {
            context_->PSSetShader(segment.shader, nullptr, 0);
            boundShader = segment.shader;
        }
        if (segment.textured) {
            ID3D11ShaderResourceView* resource = segment.resource.Get();
            if (boundResource != resource) {
                context_->PSSetShaderResources(0, 1, &resource);
                boundResource = resource;
            }
            if (!samplerBound) {
                context_->PSSetSamplers(0, 1, &rawSampler);
                samplerBound = true;
            }
        }
        context_->DrawIndexed(segment.indexCount, segment.indexOffset, 0);
        ++drawSegments;
    }
    return true;
}

bool D3D11Renderer::createGlyphBitmap(
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    const DWRITE_GLYPH_RUN& glyphRun,
    GlyphBitmap& bitmap,
    std::size_t& bytes) noexcept {
    bitmap = {};
    bytes = 0;
    if (!device_ || !textMeasurer_.factory_ || glyphRun.fontFace == nullptr || glyphRun.glyphCount == 0) {
        return false;
    }

    try {
        Microsoft::WRL::ComPtr<IDWriteGlyphRunAnalysis> analysis;
        const DWRITE_TEXTURE_TYPE textureType = glyphTextureType();
        const bool grayscaleDirect = textureType == kGlyphGrayscaleTextureType;

        HRESULT result;
        if (grayscaleDirect) {
            result = textMeasurer_.factory2_->CreateGlyphRunAnalysis(
                &glyphRun,
                nullptr,
                kGlyphRenderingMode,
                kGlyphMeasuringMode,
                DWRITE_GRID_FIT_MODE_ENABLED,
                DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                baselineOriginX,
                baselineOriginY,
                analysis.GetAddressOf());
        } else {
            result = textMeasurer_.factory_->CreateGlyphRunAnalysis(
                &glyphRun,
                1.0f,
                nullptr,
                kGlyphRenderingMode,
                kGlyphMeasuringMode,
                baselineOriginX,
                baselineOriginY,
                analysis.GetAddressOf());
        }
        if (FAILED(result)) {
            return false;
        }

        RECT textureBounds{};
        result = analysis->GetAlphaTextureBounds(textureType, &textureBounds);
        if (FAILED(result)) {
            return false;
        }

        const LONG widthLong = textureBounds.right - textureBounds.left;
        const LONG heightLong = textureBounds.bottom - textureBounds.top;
        if (widthLong <= 0 || heightLong <= 0) {
            return true;
        }

        const std::uint64_t width = static_cast<std::uint64_t>(widthLong);
        const std::uint64_t height = static_cast<std::uint64_t>(heightLong);
        if (width > UINT32_MAX || height > UINT32_MAX || width > std::numeric_limits<std::size_t>::max() / height) {
            return false;
        }
        const std::size_t pixelCount = static_cast<std::size_t>(width * height);
        const std::size_t bytesPerPixel = grayscaleDirect ? 1u : 3u;
        if (pixelCount > static_cast<std::size_t>(UINT32_MAX) / bytesPerPixel) {
            return false;
        }

        std::vector<std::uint8_t> coverage(pixelCount * bytesPerPixel);
        result = analysis->CreateAlphaTexture(
            textureType,
            &textureBounds,
            coverage.data(),
            static_cast<UINT32>(coverage.size()));
        if (FAILED(result)) {
            return false;
        }

        std::vector<std::uint8_t> grayscale;
        if (grayscaleDirect) {
            grayscale = std::move(coverage);
        } else {
            grayscale.resize(pixelCount);
            for (std::size_t index = 0; index < grayscale.size(); ++index) {
                const std::size_t source = index * 3u;
                const unsigned value =
                    static_cast<unsigned>(coverage[source]) +
                    static_cast<unsigned>(coverage[source + 1]) +
                    static_cast<unsigned>(coverage[source + 2]);
                grayscale[index] = static_cast<std::uint8_t>(value / 3u);
            }
        }

        bitmap.offsetX = static_cast<float>(textureBounds.left) - baselineOriginX;
        bitmap.offsetY = static_cast<float>(textureBounds.top) - baselineOriginY;
        bitmap.width = static_cast<std::uint32_t>(width);
        bitmap.height = static_cast<std::uint32_t>(height);
        bitmap.pixels = std::move(grayscale);
        bytes = pixelCount;
        ++glyphUploads_;
        return true;
    } catch (...) {
        bitmap = {};
        bytes = 0;
        return false;
    }
}

bool D3D11Renderer::uploadGlyphBitmap(GlyphBitmap& bitmap) noexcept {
    if (!device_ || !context_ || bitmap.width == 0 || bitmap.height == 0 || bitmap.pixels.empty()) {
        return false;
    }

    try {
        const bool fitsAtlas =
            bitmap.width <= kGlyphAtlasDimension - kGlyphAtlasPadding &&
            bitmap.height <= kGlyphAtlasDimension - kGlyphAtlasPadding;
        if (!fitsAtlas) {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = bitmap.width;
            description.Height = bitmap.height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_IMMUTABLE;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA initialData{};
            initialData.pSysMem = bitmap.pixels.data();
            initialData.SysMemPitch = bitmap.width;

            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            if (FAILED(device_->CreateTexture2D(&description, &initialData, texture.GetAddressOf()))) {
                return false;
            }
            if (FAILED(device_->CreateShaderResourceView(
                    texture.Get(), nullptr, bitmap.resource.GetAddressOf()))) {
                return false;
            }
            bitmap.u0 = 0.0f;
            bitmap.v0 = 0.0f;
            bitmap.u1 = 1.0f;
            bitmap.v1 = 1.0f;
            return true;
        }

        auto createAtlas = [&]() noexcept -> bool {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = kGlyphAtlasDimension;
            description.Height = kGlyphAtlasDimension;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            GlyphAtlas next{};
            if (FAILED(device_->CreateTexture2D(&description, nullptr, next.texture.GetAddressOf())) ||
                FAILED(device_->CreateShaderResourceView(
                    next.texture.Get(), nullptr, next.resource.GetAddressOf()))) {
                return false;
            }
            next.cursorX = kGlyphAtlasPadding;
            next.cursorY = kGlyphAtlasPadding;
            glyphAtlas_ = std::move(next);
            return true;
        };

        auto hasRoom = [&]() noexcept -> bool {
            if (!glyphAtlas_.texture || !glyphAtlas_.resource) {
                return false;
            }
            std::uint32_t cursorX = glyphAtlas_.cursorX;
            std::uint32_t cursorY = glyphAtlas_.cursorY;
            if (cursorX + bitmap.width + kGlyphAtlasPadding > kGlyphAtlasDimension) {
                cursorX = kGlyphAtlasPadding;
                cursorY += glyphAtlas_.rowHeight + kGlyphAtlasPadding;
            }
            return cursorY + bitmap.height + kGlyphAtlasPadding <= kGlyphAtlasDimension;
        };

        if (!hasRoom()) {
            glyphCache_.clear();
            glyphAtlas_ = {};
            if (!createAtlas() || !hasRoom()) {
                return false;
            }
        }

        if (glyphAtlas_.cursorX + bitmap.width + kGlyphAtlasPadding > kGlyphAtlasDimension) {
            glyphAtlas_.cursorX = kGlyphAtlasPadding;
            glyphAtlas_.cursorY += glyphAtlas_.rowHeight + kGlyphAtlasPadding;
            glyphAtlas_.rowHeight = 0;
        }

        const std::uint32_t left = glyphAtlas_.cursorX;
        const std::uint32_t top = glyphAtlas_.cursorY;
        const std::uint32_t right = left + bitmap.width;
        const std::uint32_t bottom = top + bitmap.height;
        D3D11_BOX box{
            left,
            top,
            0,
            right,
            bottom,
            1,
        };
        context_->UpdateSubresource(
            glyphAtlas_.texture.Get(),
            0,
            &box,
            bitmap.pixels.data(),
            bitmap.width,
            0);

        bitmap.resource = glyphAtlas_.resource;
        bitmap.u0 = static_cast<float>(left) / static_cast<float>(kGlyphAtlasDimension);
        bitmap.v0 = static_cast<float>(top) / static_cast<float>(kGlyphAtlasDimension);
        bitmap.u1 = static_cast<float>(right) / static_cast<float>(kGlyphAtlasDimension);
        bitmap.v1 = static_cast<float>(bottom) / static_cast<float>(kGlyphAtlasDimension);
        glyphAtlas_.cursorX = right + kGlyphAtlasPadding;
        glyphAtlas_.rowHeight = std::max(glyphAtlas_.rowHeight, bitmap.height);
        return true;
    } catch (...) {
        bitmap.resource.Reset();
        return false;
    }
}

bool D3D11Renderer::drawText(
    const DrawTextCommand& command,
    Rect clip,
    std::uint32_t& glyphRuns) noexcept {
    if (empty(command.bounds) || command.text.empty()) {
        return true;
    }
    if (!textLayoutCache_) {
        return false;
    }

    try {
        const std::wstring wide = DirectWriteTextMeasurer::widen(command.text);
        if (wide.empty()) {
            return false;
        }

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout = textLayoutCache_->acquire(wide, command);
        if (!layout) {
            return false;
        }

        GlyphDrawContext drawingContext{};
        drawingContext.owner = this;
        drawingContext.clip = clip;
        drawingContext.color = command.color;
        drawingContext.opacity = command.opacity;
        drawingContext.glyphRuns = &glyphRuns;
        drawingContext.drawGlyphRun =
            [](GlyphDrawContext& context,
               FLOAT baselineOriginX,
               FLOAT baselineOriginY,
               const DWRITE_GLYPH_RUN* glyphRun) noexcept -> HRESULT {
                auto& renderer = *static_cast<D3D11Renderer*>(context.owner);
                if (glyphRun == nullptr || glyphRun->fontFace == nullptr ||
                    (glyphRun->glyphCount != 0 && glyphRun->glyphIndices == nullptr)) {
                    return E_INVALIDARG;
                }
                if (glyphRun->glyphCount == 0) {
                    return S_OK;
                }

                try {
                    GlyphRunKey key = renderer.makeGlyphRunKey(baselineOriginX, baselineOriginY, *glyphRun);
                    if (key.fontFaceIdentity == 0 || key.glyphIndices.size() != glyphRun->glyphCount) {
                        return E_INVALIDARG;
                    }

                    if (auto* cached = renderer.glyphCache_.find(key); cached != nullptr) {
                        const Rect bounds{
                            baselineOriginX + cached->offsetX,
                            baselineOriginY + cached->offsetY,
                            static_cast<float>(cached->width),
                            static_cast<float>(cached->height),
                        };
                        if (!renderer.drawGlyphBitmap(
                                bounds,
                                context.clip,
                                context.color,
                                context.opacity,
                                cached->resource.Get(),
                                cached->u0,
                                cached->v0,
                                cached->u1,
                                cached->v1)) {
                            return E_FAIL;
                        }
                        if (context.glyphRuns != nullptr) {
                            ++(*context.glyphRuns);
                        }
                        return S_OK;
                    }

                    GlyphBitmap bitmap{};
                    std::size_t bytes{};
                    if (!renderer.createGlyphBitmap(
                            baselineOriginX,
                            baselineOriginY,
                            *glyphRun,
                            bitmap,
                            bytes)) {
                        return E_FAIL;
                    }
                    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.pixels.empty()) {
                        return S_OK;
                    }
                    if (!renderer.uploadGlyphBitmap(bitmap)) {
                        return E_FAIL;
                    }

                    const Rect bounds{
                        baselineOriginX + bitmap.offsetX,
                        baselineOriginY + bitmap.offsetY,
                        static_cast<float>(bitmap.width),
                        static_cast<float>(bitmap.height),
                    };
                    if (!renderer.drawGlyphBitmap(
                            bounds,
                            context.clip,
                            context.color,
                            context.opacity,
                            bitmap.resource.Get(),
                            bitmap.u0,
                            bitmap.v0,
                            bitmap.u1,
                            bitmap.v1)) {
                        return E_FAIL;
                    }

                    if (context.glyphRuns != nullptr) {
                        ++(*context.glyphRuns);
                    }
                    (void)renderer.glyphCache_.insert(std::move(key), std::move(bitmap), bytes);
                    return S_OK;
                } catch (...) {
                    return E_OUTOFMEMORY;
                }
            };

        Microsoft::WRL::ComPtr<IDWriteTextRenderer> rendererAdapter;
        rendererAdapter.Attach(new TextRendererAdapter());

        return SUCCEEDED(layout->Draw(
            &drawingContext,
            rendererAdapter.Get(),
            command.bounds.x,
            command.bounds.y));
    } catch (...) {
        return false;
    }
}

bool D3D11Renderer::render(
    std::span<const DrawCommand> commands,
    D3D11RenderStats* stats) noexcept {
    D3D11RenderStats localStats{};
    const render::CacheStats glyphCacheBefore = glyphCache_.stats();
    const std::uint64_t glyphUploadsBefore = glyphUploads_;

    render::CacheStats imageCacheBefore{};
    std::uint64_t imageUploadsBefore{};
    std::uint64_t imageFailuresBefore{};
    std::uint64_t imageNegativeHitsBefore{};
    if (imageCache_) {
        imageCacheBefore = imageCache_->stats();
        imageUploadsBefore = imageCache_->uploads();
        imageFailuresBefore = imageCache_->decodeFailures();
        imageNegativeHitsBefore = imageCache_->negativeHits();
    }

    const render::CacheStats textLayoutCacheBefore =
        textLayoutCache_ ? textLayoutCache_->stats() : render::CacheStats{};

    const auto finalizeStats = [&]() noexcept {
        const auto& glyphCacheAfter = glyphCache_.stats();
        localStats.commandCount = static_cast<std::uint32_t>(
            std::min<std::size_t>(commands.size(), UINT32_MAX));
        localStats.glyphCacheEntries = static_cast<std::uint32_t>(
            std::min<std::size_t>(glyphCacheAfter.entries, UINT32_MAX));
        localStats.glyphCacheBytes = glyphCacheAfter.bytes;
        localStats.glyphCacheHits = glyphCacheAfter.hits - glyphCacheBefore.hits;
        localStats.glyphCacheMisses = glyphCacheAfter.misses - glyphCacheBefore.misses;
        localStats.glyphCacheUploads = glyphUploads_ - glyphUploadsBefore;
        localStats.glyphCacheEvictions = glyphCacheAfter.evictions - glyphCacheBefore.evictions;
        localStats.glyphCacheRejections = glyphCacheAfter.rejections - glyphCacheBefore.rejections;

        if (imageCache_) {
            const auto& imageCacheAfter = imageCache_->stats();
            localStats.imageCacheEntries = static_cast<std::uint32_t>(
                std::min<std::size_t>(imageCacheAfter.entries, UINT32_MAX));
            localStats.imageCacheBytes = imageCacheAfter.bytes;
            localStats.imageCacheHits = imageCacheAfter.hits - imageCacheBefore.hits;
            localStats.imageCacheMisses = imageCacheAfter.misses - imageCacheBefore.misses;
            localStats.imageCacheUploads = imageCache_->uploads() - imageUploadsBefore;
            localStats.imageCacheEvictions = imageCacheAfter.evictions - imageCacheBefore.evictions;
            localStats.imageCacheRejections = imageCacheAfter.rejections - imageCacheBefore.rejections;
            localStats.imageDecodeFailures = imageCache_->decodeFailures() - imageFailuresBefore;
            localStats.imageNegativeHits = imageCache_->negativeHits() - imageNegativeHitsBefore;
        }
        if (textLayoutCache_) {
            const auto& textLayoutAfter = textLayoutCache_->stats();
            localStats.textLayoutCacheEntries = static_cast<std::uint32_t>(
                std::min<std::size_t>(textLayoutAfter.entries, UINT32_MAX));
            localStats.textLayoutCacheBytes = textLayoutAfter.bytes;
            localStats.textLayoutCacheHits = textLayoutAfter.hits - textLayoutCacheBefore.hits;
            localStats.textLayoutCacheMisses = textLayoutAfter.misses - textLayoutCacheBefore.misses;
            localStats.textLayoutCacheEvictions = textLayoutAfter.evictions - textLayoutCacheBefore.evictions;
            localStats.textLayoutCacheRejections = textLayoutAfter.rejections - textLayoutCacheBefore.rejections;
        }
        if (stats != nullptr) {
            *stats = localStats;
        }
    };

    if (!device_ || !context_ || !target_ ||
        !std::isfinite(targetSize_.width) || !std::isfinite(targetSize_.height) ||
        targetSize_.width <= 0.0f || targetSize_.height <= 0.0f) {
        finalizeStats();
        return false;
    }

    try {
        ScopedD3D11State savedState(context_.Get());

        ID3D11RenderTargetView* target = target_.Get();
        context_->OMSetRenderTargets(1, &target, nullptr);

        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = targetSize_.width;
        viewport.Height = targetSize_.height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &viewport);

        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
        context_->GSSetShader(nullptr, nullptr, 0);
        context_->HSSetShader(nullptr, nullptr, 0);
        context_->DSSetShader(nullptr, nullptr, 0);

        constexpr FLOAT blendFactor[4]{0.0f, 0.0f, 0.0f, 0.0f};
        context_->OMSetBlendState(blendState_.Get(), blendFactor, 0xFFFFFFFFu);
        context_->OMSetDepthStencilState(depthState_.Get(), 0);
        context_->RSSetState(rasterState_.Get());

        clipStack_.clear();
        clipStack_.push_back({0.0f, 0.0f, targetSize_.width, targetSize_.height});
        resetBatch();

        bool valid = true;
        for (const DrawCommand& command : commands) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, PushClipCommand>) {
                        clipStack_.push_back(intersect(currentClip(), value.bounds));
                        ++localStats.clipPushes;
                    } else if constexpr (std::is_same_v<T, PopClipCommand>) {
                        if (clipStack_.size() <= 1) {
                            valid = false;
                        } else {
                            clipStack_.pop_back();
                            ++localStats.clipPops;
                        }
                    } else if constexpr (std::is_same_v<T, FillRectCommand>) {
                        ++localStats.fillRects;
                        if (std::isfinite(value.cornerRadius) && value.cornerRadius > 0.0f &&
                            value.bounds.width > 0.0f && value.bounds.height > 0.0f) {
                            ++localStats.roundedRects;
                        }
                        if (!drawFillRect(value, currentClip())) {
                            valid = false;
                        } else if (!empty(intersect(value.bounds, currentClip()))) {
                            ++localStats.drawCalls;
                        }
                    } else if constexpr (std::is_same_v<T, DrawTextCommand>) {
                        ++localStats.textCommands;
                        const std::uint32_t beforeRuns = localStats.glyphRuns;
                        if (!drawText(value, currentClip(), localStats.glyphRuns)) {
                            valid = false;
                        } else {
                            localStats.drawCalls += localStats.glyphRuns - beforeRuns;
                        }
                    } else if constexpr (std::is_same_v<T, DrawImageCommand>) {
                        ++localStats.imageCommands;
                        if (!drawImage(value, currentClip(), localStats)) {
                            valid = false;
                        }
                    } else if constexpr (std::is_same_v<T, DrawVideoCommand>) {
                        ++localStats.videoCommands;
                    }
                },
                command);

            if (!valid || !batchValid_) {
                break;
            }
        }

        if (clipStack_.size() != 1) {
            valid = false;
        }

        if (valid && !batchValid_) {
            valid = false;
        }

        if (valid) {
            if (!flushBatch(localStats.drawSegments)) {
                discardBatch();
                valid = false;
            } else {
                resetBatch();
            }
        } else {
            discardBatch();
        }

        finalizeStats();
        return valid;
    } catch (...) {
        discardBatch();
        finalizeStats();
        return false;
    }
}

} // namespace uirepo

#endif
