#include "uirepo/D3D11Renderer.h"
#include "uirepo/D3D11SurfaceManager.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <span>

using Microsoft::WRL::ComPtr;
using namespace uirepo;

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
};

struct TargetFixture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;
};

DeviceFixture createDevice() {
    DeviceFixture fixture;
    constexpr D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL actual{};
    const HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        requested,
        static_cast<UINT>(std::size(requested)),
        D3D11_SDK_VERSION,
        fixture.device.GetAddressOf(),
        &actual,
        fixture.context.GetAddressOf());
    require(SUCCEEDED(result), "D3D11CreateDevice(WARP) failed");
    return fixture;
}

bool supportsRenderTarget(ID3D11Device* device, DXGI_FORMAT format) {
    UINT support{};
    return device != nullptr &&
           SUCCEEDED(device->CheckFormatSupport(format, &support)) &&
           (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
}

TargetFixture createTarget(ID3D11Device* device, DXGI_FORMAT format) {
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = 32;
    textureDesc.Height = 32;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = format;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    TargetFixture target;
    require(
        SUCCEEDED(device->CreateTexture2D(&textureDesc, nullptr, target.texture.GetAddressOf())),
        "CreateTexture2D target failed");
    require(
        SUCCEEDED(device->CreateRenderTargetView(target.texture.Get(), nullptr, target.view.GetAddressOf())),
        "CreateRenderTargetView target failed");
    return target;
}

void requireTargetClassification(
    D3D11Renderer& renderer,
    ID3D11RenderTargetView* view,
    DXGI_FORMAT expectedFormat,
    D3D11TargetColorSpace expectedColorSpace,
    const char* message) {
    renderer.setTarget(view, {32.0f, 32.0f});
    require(renderer.targetFormat() == expectedFormat, message);
    require(renderer.targetColorSpace() == expectedColorSpace, message);
}

void testKnownFormats() {
    auto fixture = createDevice();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());

    const auto linear = createTarget(fixture.device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    requireTargetClassification(
        renderer,
        linear.view.Get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11TargetColorSpace::Linear,
        "R8G8B8A8_UNORM RTV was not classified as linear");

    const auto srgb = createTarget(fixture.device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    requireTargetClassification(
        renderer,
        srgb.view.Get(),
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        D3D11TargetColorSpace::Srgb,
        "R8G8B8A8_UNORM_SRGB RTV was not classified as sRGB");

    if (supportsRenderTarget(fixture.device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM)) {
        const auto bgraLinear = createTarget(fixture.device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM);
        requireTargetClassification(
            renderer,
            bgraLinear.view.Get(),
            DXGI_FORMAT_B8G8R8A8_UNORM,
            D3D11TargetColorSpace::Linear,
            "B8G8R8A8_UNORM RTV was not classified as linear");
    }

    if (supportsRenderTarget(fixture.device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)) {
        const auto bgraSrgb = createTarget(fixture.device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
        requireTargetClassification(
            renderer,
            bgraSrgb.view.Get(),
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            D3D11TargetColorSpace::Srgb,
            "B8G8R8A8_UNORM_SRGB RTV was not classified as sRGB");
    }
}

void testUnknownFormatAndDetach() {
    auto fixture = createDevice();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());

    require(
        D3D11Renderer::classifyTargetColorSpace(DXGI_FORMAT_R16G16B16A16_FLOAT) ==
            D3D11TargetColorSpace::Unknown,
        "floating-point RTV format should remain explicitly unknown");
    require(
        D3D11Renderer::classifyTargetColorSpace(DXGI_FORMAT_UNKNOWN) ==
            D3D11TargetColorSpace::Unknown,
        "DXGI_FORMAT_UNKNOWN should remain explicitly unknown");

    if (supportsRenderTarget(fixture.device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT)) {
        const auto other = createTarget(fixture.device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
        requireTargetClassification(
            renderer,
            other.view.Get(),
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11TargetColorSpace::Unknown,
            "other RTV format did not remain usable with unknown color space");
    }

    renderer.setTarget(nullptr, {});
    require(renderer.targetFormat() == DXGI_FORMAT_UNKNOWN, "detached target retained a stale RTV format");
    require(
        renderer.targetColorSpace() == D3D11TargetColorSpace::Unknown,
        "detached target retained a stale color-space classification");
}

void testTargetReplacementUpdatesClassification() {
    auto fixture = createDevice();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    const auto linear = createTarget(fixture.device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    const auto srgb = createTarget(fixture.device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    requireTargetClassification(
        renderer,
        linear.view.Get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11TargetColorSpace::Linear,
        "initial linear target classification failed");
    requireTargetClassification(
        renderer,
        srgb.view.Get(),
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        D3D11TargetColorSpace::Srgb,
        "replacement sRGB target classification failed");
    requireTargetClassification(
        renderer,
        linear.view.Get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11TargetColorSpace::Linear,
        "restored linear target classification failed");
}

void testOffscreenRenderRestoresClassification() {
    auto fixture = createDevice();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    const auto mainTarget = createTarget(fixture.device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    renderer.setTarget(mainTarget.view.Get(), {32.0f, 32.0f});

    D3D11SurfaceManager surfaces(fixture.device.Get(), fixture.context.Get());
    constexpr registry::SurfaceHandle surface = 1;
    require(
        surfaces.create(
            surface,
            D3D11SurfaceDesc{
                .width = 16,
                .height = 16,
                .format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                .clearColor = Color{0, 0, 0, 0},
            }),
        "failed to create sRGB offscreen surface");

    const std::span<const DrawCommand> noCommands{};
    require(
        surfaces.render(surface, renderer, noCommands, nullptr),
        "offscreen render failed while testing target restoration");
    require(renderer.target() == mainTarget.view.Get(), "offscreen render did not restore previous target");
    require(
        renderer.targetFormat() == DXGI_FORMAT_R8G8B8A8_UNORM,
        "offscreen render did not restore previous RTV format");
    require(
        renderer.targetColorSpace() == D3D11TargetColorSpace::Linear,
        "offscreen render did not restore previous color-space classification");
}

} // namespace

int main() {
    testKnownFormats();
    testUnknownFormatAndDetach();
    testTargetReplacementUpdatesClassification();
    testOffscreenRenderRestoresClassification();
    std::cout << "UIRepo D3D11 target color-space regressions passed\n";
    return 0;
}
