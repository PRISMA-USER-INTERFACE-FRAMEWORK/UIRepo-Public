#include "uirepo/Core.h"
#include "uirepo/D3D11Renderer.h"
#include "../src/D3D11ImageCache.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace uirepo;

namespace uirepo {
struct D3D11RendererPipelineAccess final {
    [[nodiscard]] static ID3D11BlendState* blend(const D3D11Renderer& r) noexcept { return r.blendState_.Get(); }
    [[nodiscard]] static ID3D11DepthStencilState* depth(const D3D11Renderer& r) noexcept { return r.depthState_.Get(); }
    [[nodiscard]] static ID3D11RasterizerState* raster(const D3D11Renderer& r) noexcept { return r.rasterState_.Get(); }
    [[nodiscard]] static ID3D11SamplerState* sampler(const D3D11Renderer& r) noexcept { return r.samplerState_.Get(); }
    [[nodiscard]] static D3D11ImageCache* imageCache(D3D11Renderer& r) noexcept { return r.imageCache_.get(); }
    [[nodiscard]] static std::size_t liveImageBytes(const D3D11Renderer& r) noexcept { return r.liveImageBytes_; }
    [[nodiscard]] static std::size_t liveImageCount(const D3D11Renderer& r) noexcept { return r.liveImageSrvs_.size(); }
    [[nodiscard]] static std::size_t batchSegmentCount(const D3D11Renderer& r) noexcept { return r.batchSegments_.size(); }
};

struct D3D11RendererTestAccess final {
    [[nodiscard]] static bool factory2Available(const D3D11Renderer& r) noexcept {
        return r.textMeasurer_.factory2_ != nullptr;
    }
    [[nodiscard]] static DWRITE_TEXTURE_TYPE effectiveGlyphTextureType(const D3D11Renderer& r) noexcept {
        return r.glyphTextureType();
    }
};

struct D3D11ImageCacheTestAccess final {
    static bool prime(
        D3D11ImageCache& cache,
        std::string source,
        D3D11ImageCache::Texture texture,
        std::size_t bytes) {
        return cache.cache_.insert(std::move(source), std::move(texture), bytes);
    }
};
} // namespace uirepo

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct DeviceFixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> targetTexture;
    ComPtr<ID3D11RenderTargetView> targetView;
};

struct RenderTargetFixture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;
    Size size{};
};

struct Pixel {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{};
};

RenderTargetFixture createRenderTarget(
    ID3D11Device* device,
    std::uint32_t width,
    std::uint32_t height) {
    require(device != nullptr, "createRenderTarget requires a device");
    require(width != 0 && height != 0, "createRenderTarget requires non-zero dimensions");

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;

    RenderTargetFixture target;
    require(
        SUCCEEDED(device->CreateTexture2D(&description, nullptr, target.texture.GetAddressOf())),
        "CreateTexture2D replacement target failed");
    require(
        SUCCEEDED(device->CreateRenderTargetView(target.texture.Get(), nullptr, target.view.GetAddressOf())),
        "CreateRenderTargetView replacement target failed");
    target.size = {static_cast<float>(width), static_cast<float>(height)};
    return target;
}

DeviceFixture createFixture() {
    DeviceFixture fixture;

    constexpr D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL actual{};

    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        0,
        requested,
        static_cast<UINT>(std::size(requested)),
        D3D11_SDK_VERSION,
        fixture.device.GetAddressOf(),
        &actual,
        fixture.context.GetAddressOf());
    require(SUCCEEDED(result), "D3D11CreateDevice(WARP) failed");

    auto target = createRenderTarget(fixture.device.Get(), 128, 128);
    fixture.targetTexture = std::move(target.texture);
    fixture.targetView = std::move(target.view);
    return fixture;
}

Pixel readPixel(DeviceFixture& fixture, std::uint32_t x, std::uint32_t y) {
    D3D11_TEXTURE2D_DESC description{};
    fixture.targetTexture->GetDesc(&description);
    require(x < description.Width && y < description.Height, "pixel sample is outside target");

    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> staging;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(&description, nullptr, staging.GetAddressOf())),
        "CreateTexture2D pixel staging failed");
    fixture.context->CopyResource(staging.Get(), fixture.targetTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(
        SUCCEEDED(fixture.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "Map pixel staging texture failed");
    const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                      static_cast<std::size_t>(y) * mapped.RowPitch;
    const std::size_t offset = static_cast<std::size_t>(x) * 4u;
    const Pixel pixel{row[offset + 0], row[offset + 1], row[offset + 2], row[offset + 3]};
    fixture.context->Unmap(staging.Get(), 0);
    return pixel;
}

ComPtr<ID3D11RenderTargetView> createPreviousTarget(DeviceFixture& fixture) {
    D3D11_TEXTURE2D_DESC description{};
    fixture.targetTexture->GetDesc(&description);

    ComPtr<ID3D11Texture2D> texture;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(&description, nullptr, texture.GetAddressOf())),
        "CreateTexture2D previous target failed");

    ComPtr<ID3D11RenderTargetView> view;
    require(
        SUCCEEDED(fixture.device->CreateRenderTargetView(texture.Get(), nullptr, view.GetAddressOf())),
        "CreateRenderTargetView previous target failed");
    return view;
}

ComPtr<ID3D11ShaderResourceView> createPreviousShaderResource(DeviceFixture& fixture) {
    constexpr std::uint8_t pixel = 255;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = 1;
    description.Height = 1;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = &pixel;
    initialData.SysMemPitch = 1;

    ComPtr<ID3D11Texture2D> texture;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(&description, &initialData, texture.GetAddressOf())),
        "CreateTexture2D previous shader resource failed");

    ComPtr<ID3D11ShaderResourceView> resource;
    require(
        SUCCEEDED(fixture.device->CreateShaderResourceView(texture.Get(), nullptr, resource.GetAddressOf())),
        "CreateShaderResourceView previous resource failed");
    return resource;
}

ComPtr<ID3D11SamplerState> createPreviousSampler(DeviceFixture& fixture) {
    D3D11_SAMPLER_DESC description{};
    description.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    description.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    description.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    description.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    description.MinLOD = 0.0f;
    description.MaxLOD = D3D11_FLOAT32_MAX;

    ComPtr<ID3D11SamplerState> sampler;
    require(
        SUCCEEDED(fixture.device->CreateSamplerState(&description, sampler.GetAddressOf())),
        "CreateSamplerState previous sampler failed");
    return sampler;
}

std::unique_ptr<Document> createTextDocument(D3D11Renderer& renderer, std::string text) {
    auto root = std::make_unique<PanelNode>("cache-root");
    root->style().padding = Thickness{8.0f};
    auto& label = root->emplaceChild<TextNode>("cache-label", std::move(text));
    label.setFontSize(24.0f);
    label.style().width = Length::Auto();
    label.style().height = Length::Auto();
    auto document = std::make_unique<Document>(std::move(root), &renderer.textMeasurer());
    document->layout({128.0f, 128.0f});
    return document;
}

void testDirectWriteMeasurement() {
    DirectWriteTextMeasurer measurer;
    const Size narrow = measurer.measure("iiii", 24.0f);
    const Size wide = measurer.measure("WWWW", 24.0f);
    const Size invalid = measurer.measure(std::string("\xC3\x28", 2), 24.0f);

    require(narrow.height > 0.0f, "DirectWrite returned zero height");
    require(wide.width > narrow.width, "DirectWrite measurement is not using glyph metrics");
    require(invalid.width == 0.0f && invalid.height == 0.0f, "invalid UTF-8 did not fail safely");
}

void testDirectGrayscaleGlyphBaking() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    require(
        D3D11RendererTestAccess::factory2Available(renderer),
        "the native renderer test requires IDWriteFactory2 on the Windows CI runner");
    require(
        D3D11RendererTestAccess::effectiveGlyphTextureType(renderer) == DWRITE_TEXTURE_ALIASED_1x1,
        "Factory2 glyph baking must select the grayscale 1x1 texture type");

    auto document = createTextDocument(renderer, "Grayscale");
    D3D11RenderStats stats{};
    require(renderer.render(document->buildDisplayList(), &stats), "direct grayscale glyph render failed");
    require(stats.glyphRuns >= 1, "direct grayscale glyph render produced no glyph run");
    require(
        stats.glyphCacheUploads >= 1 && stats.glyphCacheBytes > 0,
        "direct grayscale glyph bake produced no non-empty bitmap");
}

void testDrawAndStateRestore() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    auto root = std::make_unique<PanelNode>("root");
    root->style().background = Color{255, 0, 0, 255};
    root->style().clipToBounds = true;
    auto& label = root->emplaceChild<TextNode>("label", "Native UI");
    label.style().width = Length::Auto();
    label.style().height = Length::Auto();

    Document document(std::move(root), &renderer.textMeasurer());
    document.layout({128.0f, 128.0f});

    const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
    fixture.context->ClearRenderTargetView(fixture.targetView.Get(), clear);

    const auto previousTarget = createPreviousTarget(fixture);
    ID3D11RenderTargetView* rawPreviousTarget = previousTarget.Get();
    fixture.context->OMSetRenderTargets(1, &rawPreviousTarget, nullptr);

    const auto previousResource = createPreviousShaderResource(fixture);
    ID3D11ShaderResourceView* rawPreviousResource = previousResource.Get();
    fixture.context->PSSetShaderResources(0, 1, &rawPreviousResource);

    const auto previousSampler = createPreviousSampler(fixture);
    ID3D11SamplerState* rawPreviousSampler = previousSampler.Get();
    fixture.context->PSSetSamplers(0, 1, &rawPreviousSampler);

    D3D11_BUFFER_DESC previousConstantDescription{};
    previousConstantDescription.ByteWidth = 16;
    previousConstantDescription.Usage = D3D11_USAGE_DEFAULT;
    previousConstantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> previousConstantBuffer;
    require(
        SUCCEEDED(fixture.device->CreateBuffer(
            &previousConstantDescription, nullptr, previousConstantBuffer.GetAddressOf())),
        "CreateBuffer constant failed");
    ID3D11Buffer* rawPreviousConstant = previousConstantBuffer.Get();
    fixture.context->VSSetConstantBuffers(0, 1, &rawPreviousConstant);
    fixture.context->PSSetConstantBuffers(0, 1, &rawPreviousConstant);

    fixture.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    D3D11_VIEWPORT originalViewport{};
    originalViewport.TopLeftX = 3.0f;
    originalViewport.TopLeftY = 4.0f;
    originalViewport.Width = 37.0f;
    originalViewport.Height = 41.0f;
    originalViewport.MinDepth = 0.0f;
    originalViewport.MaxDepth = 1.0f;
    fixture.context->RSSetViewports(1, &originalViewport);

    D3D11RenderStats stats{};
    require(renderer.render(document.buildDisplayList(), &stats), "Renderer rejected valid display list");
    require(stats.fillRects >= 1, "Renderer did not execute fill rectangle");
    require(stats.textCommands == 1, "Renderer did not execute text command");
    require(stats.glyphRuns >= 1, "DirectWrite glyph run was not rasterized");
    require(stats.clipPushes == 1 && stats.clipPops == 1, "Renderer clip stack accounting failed");
    require(stats.commandCount >= stats.textCommands, "renderer command telemetry is inconsistent");
    require(stats.drawCalls >= 2, "renderer draw-call telemetry missed executed primitives");
    require(stats.glyphCacheMisses >= 1 && stats.glyphCacheUploads >= 1, "first text render did not populate glyph cache");
    require(stats.glyphCacheEntries >= 1 && stats.glyphCacheBytes > 0, "glyph cache live accounting remained empty");

    D3D11_PRIMITIVE_TOPOLOGY restoredTopology{};
    fixture.context->IAGetPrimitiveTopology(&restoredTopology);
    require(
        restoredTopology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
        "Renderer did not restore primitive topology");

    UINT viewportCount = 1;
    D3D11_VIEWPORT restoredViewport{};
    fixture.context->RSGetViewports(&viewportCount, &restoredViewport);
    require(viewportCount == 1, "Renderer did not restore viewport count");
    require(
        std::fabs(restoredViewport.TopLeftX - originalViewport.TopLeftX) < 0.001f &&
        std::fabs(restoredViewport.Width - originalViewport.Width) < 0.001f,
        "Renderer did not restore viewport");

    ComPtr<ID3D11RenderTargetView> restoredTarget;
    fixture.context->OMGetRenderTargets(1, restoredTarget.GetAddressOf(), nullptr);
    require(restoredTarget.Get() == previousTarget.Get(), "Renderer did not restore render target");

    ComPtr<ID3D11ShaderResourceView> restoredResource;
    fixture.context->PSGetShaderResources(0, 1, restoredResource.GetAddressOf());
    require(restoredResource.Get() == previousResource.Get(), "Renderer did not restore PS resource slot 0");

    ComPtr<ID3D11SamplerState> restoredSampler;
    fixture.context->PSGetSamplers(0, 1, restoredSampler.GetAddressOf());
    require(restoredSampler.Get() == previousSampler.Get(), "Renderer did not restore PS sampler slot 0");

    ComPtr<ID3D11Buffer> restoredVertexConstant;
    fixture.context->VSGetConstantBuffers(0, 1, restoredVertexConstant.GetAddressOf());
    require(
        restoredVertexConstant.Get() == previousConstantBuffer.Get(),
        "Renderer did not restore VS constant buffer slot 0");

    ComPtr<ID3D11Buffer> restoredPixelConstant;
    fixture.context->PSGetConstantBuffers(0, 1, restoredPixelConstant.GetAddressOf());
    require(
        restoredPixelConstant.Get() == previousConstantBuffer.Get(),
        "Renderer did not restore PS constant buffer slot 0");

    D3D11_TEXTURE2D_DESC stagingDescription{};
    fixture.targetTexture->GetDesc(&stagingDescription);
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> staging;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(
            &stagingDescription, nullptr, staging.GetAddressOf())),
        "CreateTexture2D staging failed");

    fixture.context->CopyResource(staging.Get(), fixture.targetTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(
        SUCCEEDED(fixture.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "Map staging texture failed");

    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    const std::size_t centerOffset = static_cast<std::size_t>(64) * mapped.RowPitch + 64u * 4u;
    const std::uint8_t red = pixels[centerOffset + 0];
    const std::uint8_t green = pixels[centerOffset + 1];
    const std::uint8_t blue = pixels[centerOffset + 2];
    fixture.context->Unmap(staging.Get(), 0);

    require(red > 200 && green < 20 && blue < 20, "D3D11 fill output is incorrect");
}

void testRoundedRectFill() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    constexpr float clear[4]{0.0f, 0.0f, 0.0f, 0.0f};
    fixture.context->ClearRenderTargetView(fixture.targetView.Get(), clear);

    const FillRectCommand rounded{
        "rounded",
        {16.0f, 16.0f, 96.0f, 96.0f},
        1.0f,
        0,
        Color{0, 255, 0, 255},
        24.0f,
    };
    const DrawCommand command = rounded;
    D3D11RenderStats stats{};
    require(
        renderer.render(std::span<const DrawCommand>(&command, 1), &stats),
        "rounded rectangle render failed");
    require(stats.fillRects == 1, "rounded rectangle was not counted as a fill");
    require(stats.roundedRects == 1, "rounded rectangle telemetry did not record shader path");
    require(stats.drawCalls == 1, "rounded rectangle required more than one draw call");

    const Pixel corner = readPixel(fixture, 17, 17);
    const Pixel topCenter = readPixel(fixture, 64, 17);
    const Pixel center = readPixel(fixture, 64, 64);
    require(corner.g < 10 && corner.a < 10, "rounded rectangle did not clear its corner pixels");
    require(topCenter.g > 200 && topCenter.a > 200, "rounded rectangle removed a valid top-edge pixel");
    require(center.g > 240 && center.a > 240, "rounded rectangle center was not filled");
}

void testRoundedRectRadiusNormalization() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    constexpr float clear[4]{0.0f, 0.0f, 0.0f, 0.0f};
    const auto requireSquareFallback = [&](float radius, const char* label) {
        fixture.context->ClearRenderTargetView(fixture.targetView.Get(), clear);
        const FillRectCommand fill{
            label,
            {16.0f, 16.0f, 48.0f, 48.0f},
            1.0f,
            0,
            Color{255, 0, 0, 255},
            radius,
        };
        const DrawCommand command = fill;
        D3D11RenderStats stats{};
        require(
            renderer.render(std::span<const DrawCommand>(&command, 1), &stats),
            "invalid-radius square fallback render failed");
        require(stats.fillRects == 1 && stats.drawCalls == 1, "square fallback changed fill/draw telemetry");
        require(stats.roundedRects == 0, "invalid radius was reported as a rounded fill");
        const Pixel corner = readPixel(fixture, 17, 17);
        require(corner.r > 240 && corner.a > 240, "invalid radius did not fall back to square fill");
    };

    requireSquareFallback(0.0f, "radius-zero");
    requireSquareFallback(-8.0f, "radius-negative");
    requireSquareFallback(std::numeric_limits<float>::quiet_NaN(), "radius-nan");

    fixture.context->ClearRenderTargetView(fixture.targetView.Get(), clear);
    const std::array<DrawCommand, 2> commands{
        FillRectCommand{
            "radius-max",
            {8.0f, 80.0f, 48.0f, 32.0f},
            1.0f,
            0,
            Color{0, 255, 0, 255},
            16.0f,
        },
        FillRectCommand{
            "radius-oversized",
            {72.0f, 80.0f, 48.0f, 32.0f},
            1.0f,
            0,
            Color{0, 255, 0, 255},
            1000.0f,
        },
    };
    D3D11RenderStats stats{};
    require(renderer.render(commands, &stats), "oversized-radius render failed");
    require(stats.fillRects == 2 && stats.roundedRects == 2, "oversized radius skipped rounded-fill path");
    require(stats.drawCalls == 2, "oversized radius changed one-quad draw policy");

    const Pixel maxCorner = readPixel(fixture, 9, 81);
    const Pixel oversizedCorner = readPixel(fixture, 73, 81);
    const Pixel maxTop = readPixel(fixture, 32, 81);
    const Pixel oversizedTop = readPixel(fixture, 96, 81);
    const Pixel maxCenter = readPixel(fixture, 32, 96);
    const Pixel oversizedCenter = readPixel(fixture, 96, 96);
    require(maxCorner.g < 10 && maxCorner.a < 10, "max legal radius did not clip its corner");
    require(oversizedCorner.g < 10 && oversizedCorner.a < 10, "oversized radius was not clamped to rounded geometry");
    require(maxTop.g > 200 && maxTop.a > 200, "max legal radius removed top-center pixel");
    require(oversizedTop.g > 200 && oversizedTop.a > 200, "oversized-radius clamp removed top-center pixel");
    require(maxCenter.g > 240 && maxCenter.a > 240, "max legal radius removed center pixel");
    require(oversizedCenter.g > 240 && oversizedCenter.a > 240, "oversized-radius clamp removed center pixel");
}

void testTargetDetachAndReacquire() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    auto root = std::make_unique<PanelNode>("resize-root");
    root->style().background = Color{0, 0, 255, 255};
    Document document(std::move(root), &renderer.textMeasurer());
    document.layout({128.0f, 128.0f});

    require(renderer.render(document.buildDisplayList()), "initial target render failed");

    renderer.setTarget(nullptr, {});
    require(
        !renderer.render(document.buildDisplayList()),
        "renderer accepted a frame after target pre-release");

    auto replacement = createRenderTarget(fixture.device.Get(), 96, 64);
    const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
    fixture.context->ClearRenderTargetView(replacement.view.Get(), clear);
    renderer.setTarget(replacement.view.Get(), replacement.size);
    document.layout(replacement.size);

    D3D11RenderStats stats{};
    require(
        renderer.render(document.buildDisplayList(), &stats),
        "renderer failed after replacement target acquisition");
    require(stats.fillRects >= 1, "replacement target render did not execute fill rectangle");

    D3D11_TEXTURE2D_DESC stagingDescription{};
    replacement.texture->GetDesc(&stagingDescription);
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> staging;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(
            &stagingDescription, nullptr, staging.GetAddressOf())),
        "CreateTexture2D replacement staging failed");
    fixture.context->CopyResource(staging.Get(), replacement.texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(
        SUCCEEDED(fixture.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "Map replacement staging texture failed");
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    const std::size_t sampleOffset = static_cast<std::size_t>(32) * mapped.RowPitch + 48u * 4u;
    const std::uint8_t red = pixels[sampleOffset + 0];
    const std::uint8_t green = pixels[sampleOffset + 1];
    const std::uint8_t blue = pixels[sampleOffset + 2];
    fixture.context->Unmap(staging.Get(), 0);

    require(
        red < 20 && green < 20 && blue > 200,
        "replacement render target did not receive the reacquired frame");
}

void testGlyphCacheReusesUploadedBitmap() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get(), 1024u * 1024u, 64u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    auto document = createTextDocument(renderer, "Cache me");

    D3D11RenderStats first{};
    require(renderer.render(document->buildDisplayList(), &first), "first glyph-cache render failed");
    require(first.glyphCacheMisses >= 1, "first glyph-cache render did not miss");
    require(first.glyphCacheUploads >= 1, "first glyph-cache render did not upload a bitmap");
    require(first.glyphCacheBytes <= 1024u * 1024u, "glyph cache exceeded configured byte budget");

    D3D11RenderStats second{};
    require(renderer.render(document->buildDisplayList(), &second), "second glyph-cache render failed");
    require(second.glyphCacheHits >= 1, "second glyph-cache render did not reuse cached shaped run");
    require(second.glyphCacheUploads == 0, "second glyph-cache render repeated GPU texture upload");
    require(second.glyphCacheBytes == first.glyphCacheBytes, "cache hit changed live glyph bytes");
}

void testGlyphCacheHardBudgetRejectsOversizedBitmap() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get(), 1u, 1u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    auto document = createTextDocument(renderer, "Budget");

    D3D11RenderStats stats{};
    require(renderer.render(document->buildDisplayList(), &stats), "oversized glyph fallback render failed");
    require(stats.glyphCacheUploads >= 1, "oversized glyph was not uploaded for one-shot rendering");
    require(stats.glyphCacheRejections >= 1, "oversized glyph bypassed cache hard budget");
    require(stats.glyphCacheBytes == 0 && stats.glyphCacheEntries == 0, "rejected glyph remained resident in cache");
}

void testUnbalancedClipFailsClosed() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    const PopClipCommand pop{"bad", {0.0f, 0.0f, 1.0f, 1.0f}, 1.0f, 0};
    const DrawCommand command = pop;
    require(!renderer.render(std::span<const DrawCommand>(&command, 1)), "Unbalanced clip did not fail closed");
}

} // namespace

void testPipelineStateDescriptors() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());

    D3D11_BLEND_DESC blend{};
    D3D11RendererPipelineAccess::blend(renderer)->GetDesc(&blend);
    require(blend.RenderTarget[0].BlendEnable == TRUE, "blend must stay enabled for compositing");
    require(blend.RenderTarget[0].SrcBlend == D3D11_BLEND_SRC_ALPHA, "blend SrcBlend drifted");
    require(blend.RenderTarget[0].DestBlend == D3D11_BLEND_INV_SRC_ALPHA, "blend DestBlend drifted");
    require(blend.RenderTarget[0].BlendOp == D3D11_BLEND_OP_ADD, "blend BlendOp drifted");
    require(blend.RenderTarget[0].SrcBlendAlpha == D3D11_BLEND_ONE, "blend SrcBlendAlpha drifted");
    require(blend.RenderTarget[0].DestBlendAlpha == D3D11_BLEND_INV_SRC_ALPHA, "blend DestBlendAlpha drifted");
    require(blend.RenderTarget[0].BlendOpAlpha == D3D11_BLEND_OP_ADD, "blend BlendOpAlpha drifted");
    require(
        blend.RenderTarget[0].RenderTargetWriteMask == D3D11_COLOR_WRITE_ENABLE_ALL,
        "blend write mask drifted");

    D3D11_DEPTH_STENCIL_DESC depth{};
    D3D11RendererPipelineAccess::depth(renderer)->GetDesc(&depth);
    require(depth.DepthEnable == FALSE, "depth test must stay disabled for a UI overlay");
    require(depth.StencilEnable == FALSE, "stencil must stay disabled");

    D3D11_RASTERIZER_DESC raster{};
    D3D11RendererPipelineAccess::raster(renderer)->GetDesc(&raster);
    require(raster.FillMode == D3D11_FILL_SOLID, "raster fill mode drifted");
    require(raster.CullMode == D3D11_CULL_NONE, "raster cull mode drifted");
    require(raster.ScissorEnable == TRUE, "raster must keep scissor enabled for clip rects");
    require(raster.DepthClipEnable == TRUE, "raster depth clip drifted");

    D3D11_SAMPLER_DESC sampler{};
    D3D11RendererPipelineAccess::sampler(renderer)->GetDesc(&sampler);
    require(sampler.Filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR, "sampler filter drifted");
    require(sampler.AddressU == D3D11_TEXTURE_ADDRESS_CLAMP, "sampler AddressU drifted");
    require(sampler.AddressV == D3D11_TEXTURE_ADDRESS_CLAMP, "sampler AddressV drifted");
    require(sampler.AddressW == D3D11_TEXTURE_ADDRESS_CLAMP, "sampler AddressW drifted");
}

void testBatchingCollapsesDrawCalls() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    const Color fill{200, 60, 60, 255};

    std::vector<DrawCommand> sameClip;
    for (int i = 0; i < 5; ++i) {
        FillRectCommand rect{};
        rect.bounds = Rect{static_cast<float>(i) * 12.0f, 0.0f, 10.0f, 10.0f};
        rect.opacity = 1.0f;
        rect.color = fill;
        rect.cornerRadius = 0.0f;
        sameClip.emplace_back(rect);
    }
    D3D11RenderStats batched{};
    require(renderer.render(sameClip, &batched), "batched fill render failed");
    require(batched.fillRects == 5, "expected five fill rects");
    require(batched.drawSegments == 1, "same-clip solid rects must collapse into one DrawIndexed");

    std::vector<DrawCommand> withClip;
    FillRectCommand outer{};
    outer.bounds = Rect{0.0f, 0.0f, 10.0f, 10.0f};
    outer.opacity = 1.0f;
    outer.color = fill;
    withClip.emplace_back(outer);
    PushClipCommand push{};
    push.bounds = Rect{20.0f, 20.0f, 40.0f, 40.0f};
    push.opacity = 1.0f;
    withClip.emplace_back(push);
    FillRectCommand inner{};
    inner.bounds = Rect{22.0f, 22.0f, 10.0f, 10.0f};
    inner.opacity = 1.0f;
    inner.color = fill;
    withClip.emplace_back(inner);
    PopClipCommand pop{};
    pop.bounds = Rect{20.0f, 20.0f, 40.0f, 40.0f};
    pop.opacity = 1.0f;
    withClip.emplace_back(pop);
    D3D11RenderStats split{};
    require(renderer.render(withClip, &split), "clip-split render failed");
    require(split.drawSegments == 2, "a clip change must start a new draw segment");
}

bool bandHasInk(DeviceFixture& fixture, std::uint32_t y0, std::uint32_t y1) {
    D3D11_TEXTURE2D_DESC description{};
    fixture.targetTexture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(&description, nullptr, staging.GetAddressOf())),
        "CreateTexture2D band staging failed");
    fixture.context->CopyResource(staging.Get(), fixture.targetTexture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(SUCCEEDED(fixture.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "Map band staging failed");
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    bool ink = false;
    for (std::uint32_t y = y0; y < y1 && y < 128u; ++y) {
        for (std::uint32_t x = 0; x < 128u; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * mapped.RowPitch + x * 4u;
            if (pixels[offset] > 40 || pixels[offset + 1] > 40 || pixels[offset + 2] > 40) {
                ink = true;
                break;
            }
        }
        if (ink) {
            break;
        }
    }
    fixture.context->Unmap(staging.Get(), 0);
    return ink;
}

void testGlyphEvictionDuringFrameStillBatches() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get(), 16u * 1024u * 1024u, 1u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
    fixture.context->ClearRenderTargetView(fixture.targetView.Get(), clear);

    static const std::string firstText = "Ay";
    static const std::string secondText = "Zq";
    DrawTextCommand first{};
    first.bounds = Rect{2.0f, 2.0f, 100.0f, 28.0f};
    first.opacity = 1.0f;
    first.text = firstText;
    first.fontSize = 20.0f;
    first.color = Color{255, 255, 255, 255};
    DrawTextCommand second{};
    second.bounds = Rect{2.0f, 60.0f, 100.0f, 28.0f};
    second.opacity = 1.0f;
    second.text = secondText;
    second.fontSize = 20.0f;
    second.color = Color{255, 255, 255, 255};

    std::vector<DrawCommand> commands{first, second};
    D3D11RenderStats stats{};
    require(renderer.render(commands, &stats), "eviction-during-frame render must not fail or use-after-free");
    require(stats.glyphRuns >= 2, "both text runs must rasterize");
    require(stats.glyphCacheEvictions >= 1, "one-entry cache must evict the first run before flush");
    require(stats.drawSegments == 1, "glyph runs sharing the atlas must collapse into one segment");
    require(bandHasInk(fixture, 0u, 40u), "first (evicted) glyph produced no ink: SRV did not survive to flush");
    require(bandHasInk(fixture, 55u, 100u), "second glyph produced no ink after batched flush");
}

void testGlyphRejectionDuringFrameStillInks() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get(), 1u, 1u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
    fixture.context->ClearRenderTargetView(fixture.targetView.Get(), clear);

    static const std::string label = "Reject";
    DrawTextCommand text{};
    text.bounds = Rect{2.0f, 40.0f, 120.0f, 32.0f};
    text.opacity = 1.0f;
    text.text = label;
    text.fontSize = 20.0f;
    text.color = Color{255, 255, 255, 255};

    std::vector<DrawCommand> commands{text};
    D3D11RenderStats stats{};
    require(renderer.render(commands, &stats), "rejected-glyph render must not fail or use-after-free");
    require(stats.glyphCacheRejections >= 1, "tiny budget must reject the glyph");
    require(stats.glyphCacheEntries == 0, "rejected glyph must not stay resident");
    require(
        bandHasInk(fixture, 38u, 78u),
        "rejected glyph produced no ink: the rejected temporary's SRV did not survive to flush");
}

void testBatchKeepsStateTransitionsOrdered() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    static const std::string word = "Hi";
    FillRectCommand rectA{};
    rectA.bounds = Rect{0.0f, 0.0f, 20.0f, 20.0f};
    rectA.opacity = 1.0f;
    rectA.color = Color{200, 60, 60, 255};
    DrawTextCommand text{};
    text.bounds = Rect{0.0f, 24.0f, 60.0f, 24.0f};
    text.opacity = 1.0f;
    text.text = word;
    text.fontSize = 16.0f;
    text.color = Color{255, 255, 255, 255};
    FillRectCommand rectB{};
    rectB.bounds = Rect{0.0f, 60.0f, 20.0f, 20.0f};
    rectB.opacity = 1.0f;
    rectB.color = Color{60, 200, 60, 255};

    std::vector<DrawCommand> commands{rectA, text, rectB};
    D3D11RenderStats stats{};
    require(renderer.render(commands, &stats), "solid/text/solid render failed");
    require(stats.glyphRuns >= 1, "the middle text run must rasterize");
    require(
        stats.drawSegments == 3,
        "solid/text/solid must stay three ordered segments (batching must not globally group by state)");
}

void testBatchedImagePathRendersPrimedSrv() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
    fixture.context->ClearRenderTargetView(fixture.targetView.Get(), clear);

    const std::uint8_t greenPixels[16] = {
        0, 255, 0, 255, 0, 255, 0, 255,
        0, 255, 0, 255, 0, 255, 0, 255};
    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = 2;
    textureDescription.Height = 2;
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_IMMUTABLE;
    textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = greenPixels;
    initialData.SysMemPitch = 8;
    ComPtr<ID3D11Texture2D> texture;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(&textureDescription, &initialData, texture.GetAddressOf())),
        "create image texture failed");
    ComPtr<ID3D11ShaderResourceView> srv;
    require(
        SUCCEEDED(fixture.device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf())),
        "create image SRV failed");

    D3D11ImageCache* cache = D3D11RendererPipelineAccess::imageCache(renderer);
    require(cache != nullptr, "renderer image cache unavailable");
    static const std::string source = "test://green";
    D3D11ImageCache::Texture primed{};
    primed.resource = srv;
    primed.width = 2;
    primed.height = 2;
    require(
        D3D11ImageCacheTestAccess::prime(*cache, source, std::move(primed), 16u),
        "prime image cache failed");

    DrawImageCommand image{};
    image.bounds = Rect{16.0f, 16.0f, 96.0f, 96.0f};
    image.opacity = 1.0f;
    image.source = source;
    std::vector<DrawCommand> commands{image};
    D3D11RenderStats stats{};
    require(renderer.render(commands, &stats), "batched image render failed");
    require(stats.imageCommands == 1, "image command was not counted");
    require(stats.drawSegments >= 1, "primed image produced no draw segment");

    const Pixel center = readPixel(fixture, 64u, 64u);
    require(
        center.r < 40 && center.g > 200 && center.b < 40,
        "batched image path did not render the primed green texture");
}

ComPtr<ID3D11ShaderResourceView> primeSolidImage(
    DeviceFixture& fixture,
    D3D11Renderer& renderer,
    const std::string& source,
    std::uint32_t width,
    std::uint32_t height,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
    for (std::size_t i = 0; i < pixels.size(); i += 4u) {
        pixels[i] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = 255;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = pixels.data();
    sd.SysMemPitch = width * 4u;
    ComPtr<ID3D11Texture2D> tex;
    require(SUCCEEDED(fixture.device->CreateTexture2D(&td, &sd, tex.GetAddressOf())), "primeSolidImage texture");
    ComPtr<ID3D11ShaderResourceView> srv;
    require(SUCCEEDED(fixture.device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf())), "primeSolidImage srv");
    D3D11ImageCache* cache = D3D11RendererPipelineAccess::imageCache(renderer);
    require(cache != nullptr, "primeSolidImage cache");
    D3D11ImageCache::Texture t{};
    t.resource = srv;
    t.width = width;
    t.height = height;
    require(
        D3D11ImageCacheTestAccess::prime(*cache, source, std::move(t), static_cast<std::size_t>(width) * height * 4u),
        "primeSolidImage prime");
    return srv;
}

DrawImageCommand imageAt(const std::string& src, float x, float y, float size) {
    DrawImageCommand c{};
    c.bounds = Rect{x, y, size, size};
    c.opacity = 1.0f;
    c.source = src;
    return c;
}

D3D11Renderer makeBudgetRenderer(DeviceFixture& fixture, std::size_t liveBudget) {
    return D3D11Renderer(
        fixture.device.Get(),
        fixture.context.Get(),
        D3D11Renderer::DefaultGlyphCacheBytes,
        D3D11Renderer::DefaultGlyphCacheEntries,
        D3D11Renderer::DefaultImageCacheBytes,
        D3D11Renderer::DefaultImageCacheEntries,
        liveBudget,
        D3D11Renderer::DefaultSingleImageBudgetBytes);
}

void testImageBudgetRepeatedSrvCountsOnce() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer = makeBudgetRenderer(fixture, 24000u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    static const std::string a = "budget://a";
    (void)primeSolidImage(fixture, renderer, a, 64, 64, 200, 60, 60);
    std::vector<DrawCommand> cmds{imageAt(a, 0.0f, 0.0f, 32.0f), imageAt(a, 40.0f, 0.0f, 32.0f)};
    D3D11RenderStats stats{};
    require(renderer.render(cmds, &stats), "repeated-image render failed");
    require(stats.imageCommands == 2, "both image commands counted");
    require(stats.imageBudgetFlushes == 0, "repeated same SRV must not force a budget flush (counts once)");
}

void testImageBudgetTwoUniqueUnderBudget() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer = makeBudgetRenderer(fixture, 40000u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    static const std::string a = "budget2://a";
    static const std::string b = "budget2://b";
    (void)primeSolidImage(fixture, renderer, a, 64, 64, 200, 60, 60);
    (void)primeSolidImage(fixture, renderer, b, 64, 64, 60, 200, 60);
    std::vector<DrawCommand> cmds{imageAt(a, 0.0f, 0.0f, 32.0f), imageAt(b, 40.0f, 0.0f, 32.0f)};
    D3D11RenderStats stats{};
    require(renderer.render(cmds, &stats), "two-image render failed");
    require(stats.imageBudgetFlushes == 0, "two unique images under budget must not flush");
    require(stats.drawSegments == 2, "two under-budget images remain a normal two-segment batch");
}

void testImageBudgetOverBudgetForcesOrderedFlush() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer = makeBudgetRenderer(fixture, 24000u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
    fixture.context->ClearRenderTargetView(fixture.targetView.Get(), clear);
    static const std::string a = "budget3://a";
    static const std::string b = "budget3://b";
    (void)primeSolidImage(fixture, renderer, a, 64, 64, 220, 40, 40);
    (void)primeSolidImage(fixture, renderer, b, 64, 64, 40, 220, 40);
    std::vector<DrawCommand> cmds{
        imageAt(a, 0.0f, 0.0f, 32.0f),
        imageAt(b, 48.0f, 0.0f, 32.0f),
        imageAt(a, 96.0f, 0.0f, 32.0f)};
    D3D11RenderStats stats{};
    require(renderer.render(cmds, &stats), "A-B-A over-budget render failed");
    require(stats.imageBudgetFlushes >= 1, "exceeding the live-image budget must force at least one flush");

    const Pixel first = readPixel(fixture, 16u, 16u);
    const Pixel middle = readPixel(fixture, 64u, 16u);
    const Pixel third = readPixel(fixture, 112u, 16u);
    require(first.r > 200 && first.g < 60, "A (region 1) must be red after the flush");
    require(middle.g > 200 && middle.r < 60, "B (region 2) must be green");
    require(third.r > 200 && third.g < 60, "A (region 3) must be red — z-order preserved across the flush");
}

void testImageBudgetNoResourceRetainedAfterReset() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer = makeBudgetRenderer(fixture, 24000u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    static const std::string a = "budget4://a";
    (void)primeSolidImage(fixture, renderer, a, 64, 64, 200, 60, 60);
    std::vector<DrawCommand> cmds{imageAt(a, 0.0f, 0.0f, 32.0f)};
    D3D11RenderStats first{};
    require(renderer.render(cmds, &first), "image render failed");

    std::vector<DrawCommand> empty;
    D3D11RenderStats second{};
    require(renderer.render(empty, &second), "empty render failed");
    require(
        D3D11RendererPipelineAccess::liveImageBytes(renderer) == 0 &&
            D3D11RendererPipelineAccess::liveImageCount(renderer) == 0,
        "resetBatch must release all pinned image SRVs (no live image bytes retained)");
}

void testImageBudgetReleasedAfterSuccessfulRender() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer = makeBudgetRenderer(fixture, 24000u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    static const std::string a = "budget6://a";
    (void)primeSolidImage(fixture, renderer, a, 64, 64, 200, 60, 60);
    std::vector<DrawCommand> cmds{imageAt(a, 0.0f, 0.0f, 32.0f)};
    D3D11RenderStats stats{};
    require(renderer.render(cmds, &stats), "image render failed");
    require(stats.drawSegments >= 1, "image render produced no draw segment");
    require(
        D3D11RendererPipelineAccess::liveImageBytes(renderer) == 0 &&
            D3D11RendererPipelineAccess::liveImageCount(renderer) == 0 &&
            D3D11RendererPipelineAccess::batchSegmentCount(renderer) == 0,
        "a successful render must release its pinned image SRVs before returning, not next frame");
}

void testImageBudgetReleasedAfterFailedRender() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer = makeBudgetRenderer(fixture, 24000u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    static const std::string a = "budget7://a";
    (void)primeSolidImage(fixture, renderer, a, 64, 64, 200, 60, 60);
    std::vector<DrawCommand> cmds{imageAt(a, 0.0f, 0.0f, 32.0f), PopClipCommand{}};
    D3D11RenderStats stats{};
    require(!renderer.render(cmds, &stats), "unbalanced PopClip after an image must fail the render");
    require(
        D3D11RendererPipelineAccess::liveImageBytes(renderer) == 0 &&
            D3D11RendererPipelineAccess::liveImageCount(renderer) == 0 &&
            D3D11RendererPipelineAccess::batchSegmentCount(renderer) == 0,
        "a failed render must also release its pinned image SRVs before returning, not next frame");
}

struct RendererBmp {
    std::string path;

    RendererBmp(std::string p, std::uint32_t width, std::uint32_t height) : path(std::move(p)) {
        const std::uint32_t rowPadded = ((width * 3u + 3u) / 4u) * 4u;
        const std::uint32_t dataSize = rowPadded * height;
        const std::uint32_t fileSize = 54u + dataSize;
        std::vector<std::uint8_t> buf(fileSize, 0u);
        const auto put32 = [&](std::size_t off, std::uint32_t v) {
            buf[off] = static_cast<std::uint8_t>(v & 0xFFu);
            buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
            buf[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
            buf[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
        };
        buf[0] = 'B';
        buf[1] = 'M';
        put32(2, fileSize);
        put32(10, 54);
        put32(14, 40);
        put32(18, width);
        put32(22, height);
        buf[26] = 1;
        buf[28] = 24;
        put32(34, dataSize);
        for (std::uint32_t y = 0; y < height; ++y) {
            std::uint8_t* row = &buf[54u + static_cast<std::size_t>(y) * rowPadded];
            for (std::uint32_t x = 0; x < width; ++x) {
                row[x * 3u + 0] = 40;
                row[x * 3u + 1] = 40;
                row[x * 3u + 2] = 200;
            }
        }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        require(stream.good(), "could not create renderer BMP fixture");
        stream.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(fileSize));
        require(stream.good(), "could not write renderer BMP fixture");
    }

    ~RendererBmp() {
        std::remove(path.c_str());
    }
};

void testImageBudgetOverLiveWindowIsPermanentOversize() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(
        fixture.device.Get(),
        fixture.context.Get(),
        D3D11Renderer::DefaultGlyphCacheBytes,
        D3D11Renderer::DefaultGlyphCacheEntries,
        D3D11Renderer::DefaultImageCacheBytes,
        D3D11Renderer::DefaultImageCacheEntries,
        8u * 1024u,
        32u * 1024u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    RendererBmp img("uirepo-effective-budget-test.bmp", 64, 64);
    std::vector<DrawCommand> cmds{imageAt(img.path, 0.0f, 0.0f, 32.0f)};

    D3D11RenderStats first{};
    require(renderer.render(cmds, &first), "an oversize image must be skipped, not fail the frame");
    D3D11ImageCache* cache = D3D11RendererPipelineAccess::imageCache(renderer);
    require(cache != nullptr, "renderer image cache unavailable");
    require(cache->uploads() == 0, "an image larger than the live window must never allocate a GPU texture");
    require(cache->oversizeRejections() == 1, "image over the effective single budget must be oversize-rejected");
    require(first.imageBudgetFlushes == 0, "a permanent oversize reject must not trigger a budget flush");
    require(
        D3D11RendererPipelineAccess::liveImageBytes(renderer) == 0 &&
            D3D11RendererPipelineAccess::liveImageCount(renderer) == 0,
        "an oversize-rejected image must pin no batch resource");

    D3D11RenderStats second{};
    require(renderer.render(cmds, &second), "second render over the oversize image failed");
    require(
        cache->oversizeRejections() == 1,
        "the repeat request must hit the negative cache, not re-decode and re-reject the image");
    require(cache->negativeHits() >= 1, "a repeated oversize request must be served from the negative cache");
}

void testImageBudgetFirstImageOverBudgetFails() {
    DeviceFixture fixture = createFixture();
    // Live budget deliberately below a single 64x64 RGBA8 image (64*64*4 = 16384 bytes).
    D3D11Renderer renderer = makeBudgetRenderer(fixture, 8000u);
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    static const std::string a = "budget5://a";
    (void)primeSolidImage(fixture, renderer, a, 64, 64, 200, 60, 60);
    std::vector<DrawCommand> cmds{imageAt(a, 0.0f, 0.0f, 32.0f)};
    D3D11RenderStats stats{};
    require(
        !renderer.render(cmds, &stats),
        "an image larger than the whole live-image budget must fail the frame, not be admitted");
    require(
        D3D11RendererPipelineAccess::liveImageBytes(renderer) == 0 &&
            D3D11RendererPipelineAccess::liveImageCount(renderer) == 0,
        "a rejected over-budget image must leave no live image state pinned");
}

struct RetainedBatchBaselineSample {
    double layoutUs{};
    double displayUs{};
    double renderUs{};
    double totalUs{};
    std::size_t commandCount{};
    D3D11RenderStats stats{};
};

std::int64_t queryPerformanceTicks() {
    LARGE_INTEGER value{};
    require(QueryPerformanceCounter(&value) != FALSE, "QueryPerformanceCounter failed");
    return value.QuadPart;
}

double performanceTicksToMicroseconds(std::int64_t ticks) {
    static const double frequency = [] {
        LARGE_INTEGER value{};
        require(QueryPerformanceFrequency(&value) != FALSE, "QueryPerformanceFrequency failed");
        return static_cast<double>(value.QuadPart);
    }();
    return static_cast<double>(ticks) * 1000000.0 / frequency;
}

std::unique_ptr<Document> createRetainedBatchBaselineDocument(
    D3D11Renderer& renderer,
    TextNode*& changingText) {
    constexpr std::size_t rows = 160;
    auto root = std::make_unique<PanelNode>("baseline-root");
    root->style().width = Length::Pixels(1280.0f);
    root->style().height = Length::Pixels(720.0f);
    root->style().clipToBounds = true;

    auto& stack = root->emplaceChild<StackNode>("baseline-stack", Axis::Column);
    stack.style().width = Length::Pixels(1240.0f);
    stack.style().height = Length::Pixels(712.0f);
    stack.style().padding = Thickness{4.0f};
    stack.style().gap = 1.0f;
    stack.style().alignItems = Align::Start;
    stack.style().clipToBounds = true;

    for (std::size_t index = 0; index < rows; ++index) {
        const std::string suffix = std::to_string(index);
        auto& row = stack.emplaceChild<PanelNode>("baseline-row-" + suffix);
        row.style().width = Length::Pixels(1230.0f);
        row.style().height = Length::Pixels(18.0f);
        row.style().background = Color{
            static_cast<std::uint8_t>(24u + (index % 3u) * 8u),
            static_cast<std::uint8_t>(28u + (index % 5u) * 5u),
            static_cast<std::uint8_t>(38u + (index % 7u) * 4u),
            255};

        auto& label = row.emplaceChild<TextNode>(
            "baseline-label-" + suffix,
            "Retained batch baseline row " + suffix + " A");
        label.style().width = Length::Auto();
        label.style().height = Length::Auto();
        label.setFontSize(12.0f);
        label.setColor(Color{220, 224, 232, 255});
        if (index == rows / 2u) {
            changingText = &label;
        }
    }

    return std::make_unique<Document>(std::move(root), &renderer.textMeasurer());
}

RetainedBatchBaselineSample captureRetainedBatchBaselineFrame(
    Document& document,
    D3D11Renderer& renderer) {
    const std::int64_t totalStart = queryPerformanceTicks();
    const std::int64_t layoutStart = totalStart;
    document.layout({1280.0f, 720.0f});
    const std::int64_t displayStart = queryPerformanceTicks();
    const auto display = document.buildDisplayList();
    const std::int64_t renderStart = queryPerformanceTicks();

    RetainedBatchBaselineSample sample{};
    sample.commandCount = display.size();
    require(renderer.render(display, &sample.stats), "retained batch baseline render failed");
    const std::int64_t end = queryPerformanceTicks();
    sample.layoutUs = performanceTicksToMicroseconds(displayStart - layoutStart);
    sample.displayUs = performanceTicksToMicroseconds(renderStart - displayStart);
    sample.renderUs = performanceTicksToMicroseconds(end - renderStart);
    sample.totalUs = performanceTicksToMicroseconds(end - totalStart);
    return sample;
}

double retainedBatchBaselinePercentile(std::vector<double> values, std::size_t rank) {
    require(!values.empty(), "retained batch baseline has no samples");
    rank = std::min(rank, values.size() - 1u);
    for (std::size_t index = 0; index <= rank; ++index) {
        std::size_t smallest = index;
        for (std::size_t candidate = index + 1u; candidate < values.size(); ++candidate) {
            if (values[candidate] < values[smallest]) {
                smallest = candidate;
            }
        }
        const double value = values[index];
        values[index] = values[smallest];
        values[smallest] = value;
    }
    return values[rank];
}

void printRetainedBatchBaselineSummary(
    const char* scenario,
    const std::vector<RetainedBatchBaselineSample>& samples) {
    std::vector<double> layout;
    std::vector<double> display;
    std::vector<double> render;
    std::vector<double> total;
    layout.reserve(samples.size());
    display.reserve(samples.size());
    render.reserve(samples.size());
    total.reserve(samples.size());
    for (const auto& sample : samples) {
        layout.push_back(sample.layoutUs);
        display.push_back(sample.displayUs);
        render.push_back(sample.renderUs);
        total.push_back(sample.totalUs);
    }

    const auto medianRank = (samples.size() - 1u) / 2u;
    const auto p95Rank = (samples.size() * 95u + 99u) / 100u - 1u;
    const auto& representative = samples.back();
    std::cout << "baseline,scenario=" << scenario
              << ",frames=" << samples.size()
              << ",commands=" << representative.commandCount
              << ",logical_draws=" << representative.stats.drawCalls
              << ",segments=" << representative.stats.drawSegments
              << ",layout_median_us=" << retainedBatchBaselinePercentile(layout, medianRank)
              << ",display_median_us=" << retainedBatchBaselinePercentile(display, medianRank)
              << ",render_median_us=" << retainedBatchBaselinePercentile(render, medianRank)
              << ",total_median_us=" << retainedBatchBaselinePercentile(total, medianRank)
              << ",total_p95_us=" << retainedBatchBaselinePercentile(total, p95Rank)
              << '\n';
}

int runRetainedBatchBaseline() {
    DeviceFixture fixture = createFixture();
    auto baselineTarget = createRenderTarget(fixture.device.Get(), 1280, 720);
    D3D11_TEXTURE2D_DESC baselineDescription{};
    baselineTarget.texture->GetDesc(&baselineDescription);
    require(
        baselineDescription.Width == 1280 && baselineDescription.Height == 720,
        "retained batch baseline target has unexpected dimensions");

    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(baselineTarget.view.Get(), baselineTarget.size);

    TextNode* changingText = nullptr;
    auto document = createRetainedBatchBaselineDocument(renderer, changingText);
    require(changingText != nullptr, "retained batch baseline changing text was not created");
    constexpr std::size_t warmupFrames = 12;
    constexpr std::size_t measuredFrames = 120;
    for (std::size_t index = 0; index < warmupFrames; ++index) {
        (void)captureRetainedBatchBaselineFrame(*document, renderer);
    }
    changingText->setText("Retained batch baseline row 80 B");
    for (std::size_t index = 0; index < warmupFrames; ++index) {
        (void)captureRetainedBatchBaselineFrame(*document, renderer);
    }
    changingText->setColor(Color{180, 220, 255, 255});
    for (std::size_t index = 0; index < warmupFrames; ++index) {
        (void)captureRetainedBatchBaselineFrame(*document, renderer);
    }

    std::cout << "baseline,device=WARP,target=1280x720,rows=160,warmup_frames="
              << warmupFrames << ",measured_frames=" << measuredFrames << '\n';
    for (const char* scenario : {"stable", "leaf-color-dirty", "leaf-text-dirty"}) {
        const std::string scenarioName(scenario);
        const bool colorDirty = scenarioName == "leaf-color-dirty";
        const bool textDirty = scenarioName == "leaf-text-dirty";
        std::vector<RetainedBatchBaselineSample> samples;
        samples.reserve(measuredFrames);
        for (std::size_t index = 0; index < measuredFrames; ++index) {
            if (colorDirty) {
                changingText->setColor(
                    index % 2u == 0u ? Color{180, 220, 255, 255} : Color{255, 210, 180, 255});
            } else if (textDirty) {
                changingText->setText(
                    index % 2u == 0u
                        ? "Retained batch baseline row 80 A"
                        : "Retained batch baseline row 80 B");
            }
            samples.push_back(captureRetainedBatchBaselineFrame(*document, renderer));
        }
        printRetainedBatchBaselineSummary(scenario, samples);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--retained-batch-baseline") {
        return runRetainedBatchBaseline();
    }
    testDirectWriteMeasurement();
    testDirectGrayscaleGlyphBaking();
    testDrawAndStateRestore();
    testPipelineStateDescriptors();
    testBatchingCollapsesDrawCalls();
    testGlyphEvictionDuringFrameStillBatches();
    testGlyphRejectionDuringFrameStillInks();
    testBatchKeepsStateTransitionsOrdered();
    testBatchedImagePathRendersPrimedSrv();
    testImageBudgetRepeatedSrvCountsOnce();
    testImageBudgetTwoUniqueUnderBudget();
    testImageBudgetOverBudgetForcesOrderedFlush();
    testImageBudgetNoResourceRetainedAfterReset();
    testImageBudgetReleasedAfterSuccessfulRender();
    testImageBudgetReleasedAfterFailedRender();
    testImageBudgetFirstImageOverBudgetFails();
    testImageBudgetOverLiveWindowIsPermanentOversize();
    testRoundedRectFill();
    testRoundedRectRadiusNormalization();
    testTargetDetachAndReacquire();
    testGlyphCacheReusesUploadedBitmap();
    testGlyphCacheHardBudgetRejectsOversizedBitmap();
    testUnbalancedClipFailsClosed();
    std::cout << "UIRepo Stage 0 D3D11/WARP regressions passed\n";
    return 0;
}
