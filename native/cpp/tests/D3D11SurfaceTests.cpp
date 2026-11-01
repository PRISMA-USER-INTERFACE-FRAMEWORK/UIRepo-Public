#include "uirepo/D3D11SurfaceManager.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <utility>

using Microsoft::WRL::ComPtr;
using namespace uirepo;

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
};

Fixture createFixture() {
    Fixture fixture;
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
        0,
        requested,
        static_cast<UINT>(std::size(requested)),
        D3D11_SDK_VERSION,
        fixture.device.GetAddressOf(),
        &actual,
        fixture.context.GetAddressOf());
    require(SUCCEEDED(result), "D3D11CreateDevice(WARP) failed");
    return fixture;
}

ComPtr<ID3D11RenderTargetView> createTarget(
    ID3D11Device* device,
    std::uint32_t width,
    std::uint32_t height) {
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;

    ComPtr<ID3D11Texture2D> texture;
    require(
        SUCCEEDED(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf())),
        "could not create previous renderer target texture");

    ComPtr<ID3D11RenderTargetView> view;
    require(
        SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, view.GetAddressOf())),
        "could not create previous renderer target view");
    return view;
}

std::array<std::uint8_t, 4> readPixel(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* resourceView,
    std::uint32_t x,
    std::uint32_t y) {
    require(resourceView != nullptr, "readPixel requires a surface SRV");

    ComPtr<ID3D11Resource> resource;
    resourceView->GetResource(resource.GetAddressOf());
    require(resource != nullptr, "surface SRV returned no resource");

    ComPtr<ID3D11Texture2D> texture;
    require(SUCCEEDED(resource.As(&texture)) && texture != nullptr, "surface SRV resource is not a texture2D");

    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    require(x < description.Width && y < description.Height, "readPixel coordinates are outside surface bounds");

    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    require(
        SUCCEEDED(device->CreateTexture2D(&description, nullptr, staging.GetAddressOf())),
        "could not create surface staging texture");
    context->CopyResource(staging.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(
        SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "could not map surface staging texture");
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    const std::size_t offset = static_cast<std::size_t>(y) * mapped.RowPitch + static_cast<std::size_t>(x) * 4u;
    const std::array<std::uint8_t, 4> result{
        pixels[offset + 0],
        pixels[offset + 1],
        pixels[offset + 2],
        pixels[offset + 3],
    };
    context->Unmap(staging.Get(), 0);
    return result;
}

std::unique_ptr<Document> createDocument(D3D11Renderer& renderer, Color color, Size size) {
    auto root = std::make_unique<PanelNode>("surface-root");
    root->style().background = color;
    auto document = std::make_unique<Document>(std::move(root), &renderer.textMeasurer());
    document->layout(size);
    return document;
}

void testCreateRenderReadbackAndRestore() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    const auto previousTarget = createTarget(fixture.device.Get(), 32, 32);
    renderer.setTarget(previousTarget.Get(), {32.0f, 32.0f});

    constexpr registry::SurfaceHandle handle = 1001;
    require(
        manager.create(handle, D3D11SurfaceDesc{
            .width = 64,
            .height = 48,
            .format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .clearColor = Color{0, 0, 255, 255},
        }),
        "could not create offscreen surface");

    D3D11SurfaceInfo info{};
    require(manager.info(handle, info), "could not query created surface");
    require(info.width == 64 && info.height == 48, "created surface dimensions are wrong");
    require(info.generation == 1, "new surface generation must begin at one");
    require(info.bytes == 64u * 48u * 4u, "surface byte accounting is wrong");
    require(manager.shaderResource(handle, info.generation) != nullptr, "current-generation SRV is missing");

    auto document = createDocument(renderer, Color{255, 0, 0, 255}, {64.0f, 48.0f});
    D3D11RenderStats renderStats{};
    require(
        manager.render(handle, renderer, document->buildDisplayList(), &renderStats),
        "offscreen surface render failed");
    require(renderStats.fillRects >= 1, "offscreen render did not execute retained geometry");
    require(renderer.target() == previousTarget.Get(), "offscreen render did not restore previous renderer target");
    require(
        renderer.targetSize().width == 32.0f && renderer.targetSize().height == 32.0f,
        "offscreen render did not restore previous renderer target size");

    const auto pixel = readPixel(
        fixture.device.Get(),
        fixture.context.Get(),
        manager.shaderResource(handle, info.generation),
        32,
        24);
    require(pixel[0] > 220 && pixel[1] < 30 && pixel[2] < 30, "offscreen surface pixel output is incorrect");
}

void testResizeGenerationAndStaleBorrowRejection() {
    Fixture fixture = createFixture();
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    constexpr registry::SurfaceHandle handle = 2001;
    require(manager.create(handle, D3D11SurfaceDesc{.width = 32, .height = 32}), "could not create resize surface");

    D3D11SurfaceInfo before{};
    require(manager.info(handle, before), "could not inspect pre-resize surface");
    require(manager.shaderResource(handle, before.generation) != nullptr, "pre-resize SRV is missing");

    require(manager.resize(handle, 96, 64), "surface resize failed");
    D3D11SurfaceInfo after{};
    require(manager.info(handle, after), "could not inspect resized surface");
    require(after.width == 96 && after.height == 64, "resized dimensions are wrong");
    require(after.generation == before.generation + 1, "resize did not advance surface generation");
    require(manager.shaderResource(handle, before.generation) == nullptr, "stale generation returned an SRV after resize");
    require(manager.renderTarget(handle, before.generation) == nullptr, "stale generation returned an RTV after resize");
    require(manager.shaderResource(handle, after.generation) != nullptr, "new generation SRV is missing");

    const auto stats = manager.stats();
    require(stats.resizes == 1, "resize telemetry did not increment");
    require(stats.liveBytes == after.bytes, "live byte accounting did not follow resize");
}

void testBudgetFailurePreservesPreviousSurface() {
    Fixture fixture = createFixture();
    D3D11SurfaceManager manager(
        fixture.device.Get(),
        fixture.context.Get(),
        20u * 1024u,
        20u * 1024u,
        2,
        256);

    constexpr registry::SurfaceHandle first = 3001;
    constexpr registry::SurfaceHandle second = 3002;
    require(manager.create(first, D3D11SurfaceDesc{.width = 64, .height = 64}), "budget test first surface failed");
    require(!manager.create(second, D3D11SurfaceDesc{.width = 64, .height = 64}), "live-byte budget accepted a second oversized aggregate surface");

    D3D11SurfaceInfo before{};
    require(manager.info(first, before), "could not inspect budget surface");
    ID3D11ShaderResourceView* beforeSrv = manager.shaderResource(first, before.generation);
    require(beforeSrv != nullptr, "budget surface SRV missing before failed resize");

    require(!manager.resize(first, 128, 64), "resize exceeding hard live budget unexpectedly succeeded");
    D3D11SurfaceInfo after{};
    require(manager.info(first, after), "surface disappeared after failed resize");
    require(after.width == before.width && after.height == before.height, "failed resize changed dimensions");
    require(after.generation == before.generation, "failed resize changed generation");
    require(manager.shaderResource(first, after.generation) == beforeSrv, "failed resize replaced the valid prior resource");
    require(manager.stats().rejections >= 2, "surface admission rejection telemetry is incomplete");
}

void testPoolReuseAndIdempotentDestroy() {
    Fixture fixture = createFixture();
    D3D11SurfaceManager manager(
        fixture.device.Get(),
        fixture.context.Get(),
        D3D11SurfaceManager::DefaultMaxLiveBytes,
        64u * 1024u,
        1);

    constexpr registry::SurfaceHandle first = 4001;
    constexpr registry::SurfaceHandle second = 4002;
    constexpr registry::SurfaceHandle third = 4003;
    const D3D11SurfaceDesc desc{.width = 48, .height = 48};

    require(manager.create(first, desc), "could not create pool source surface");
    const auto allocations = manager.stats().allocations;
    require(manager.destroy(first), "could not destroy pool source surface");
    require(manager.destroy(first), "duplicate surface destroy must be harmless");
    require(manager.stats().pooledCount == 1, "destroyed surface did not enter bounded pool");
    require(!manager.create(first, desc), "destroyed handle was reused");

    require(manager.create(second, desc), "could not create exact-compatible pooled surface");
    require(manager.stats().reuses == 1, "compatible pooled resource was not reused");
    require(manager.stats().allocations == allocations, "pool reuse performed a fresh allocation");

    require(manager.destroy(second), "could not return second surface to pool");
    require(manager.create(third, D3D11SurfaceDesc{.width = 32, .height = 32}), "could not create incompatible third surface");
    require(manager.destroy(third), "could not destroy incompatible third surface");
    require(manager.stats().pooledCount <= 1, "pool exceeded hard entry count");
    require(manager.stats().pooledBytes <= 64u * 1024u, "pool exceeded hard byte budget");
}

void testInvalidAdmission() {
    Fixture fixture = createFixture();
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    require(!manager.create(0, D3D11SurfaceDesc{.width = 32, .height = 32}), "zero surface handle was accepted");
    require(!manager.create(5001, D3D11SurfaceDesc{.width = 0, .height = 32}), "zero-width surface was accepted");
    require(!manager.create(5002, D3D11SurfaceDesc{.width = 32, .height = 0}), "zero-height surface was accepted");
    require(
        !manager.create(5003, D3D11SurfaceDesc{
            .width = 32,
            .height = 32,
            .format = DXGI_FORMAT_R16_FLOAT,
        }),
        "unsupported surface format was accepted");
}


void testLowMemoryPoolDrainAndRetry() {
    Fixture fixture = createFixture();
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    constexpr registry::SurfaceHandle source = 6001;
    constexpr registry::SurfaceHandle drained = 6002;
    constexpr registry::SurfaceHandle starved = 6003;
    constexpr registry::SurfaceHandle exhausted = 6004;

    // Park an incompatible resource in the pool so it cannot simply be reused.
    require(manager.create(source, D3D11SurfaceDesc{.width = 48, .height = 48}), "could not create pool source surface");
    require(manager.destroy(source), "could not return pool source surface");
    require(manager.stats().pooledCount == 1, "pool source surface did not enter the pool");
    require(manager.stats().poolDrains == 0, "pool drain telemetry started dirty");

    // A failing allocation must release the pool and retry rather than give up
    // while cached resources are still holding memory.
    manager.debugFailAllocations(1);
    require(manager.create(drained, D3D11SurfaceDesc{.width = 32, .height = 32}), "allocation failure was not retried after draining the pool");
    require(manager.stats().poolDrains == 1, "pool was not drained on allocation failure");
    require(manager.stats().pooledCount == 0 && manager.stats().pooledBytes == 0, "drained pool kept accounting for released resources");

    D3D11SurfaceInfo live{};
    require(manager.info(drained, live), "surface created by the drain retry is not addressable");
    require(manager.shaderResource(drained, live.generation) != nullptr, "surface created by the drain retry has no SRV");

    // With nothing left to drain the request must fail closed and leave the
    // live accounting untouched.
    const auto beforeStarved = manager.stats();
    manager.debugFailAllocations(1);
    require(!manager.create(starved, D3D11SurfaceDesc{.width = 32, .height = 32}), "allocation failure with an empty pool unexpectedly succeeded");
    const auto afterStarved = manager.stats();
    require(afterStarved.poolDrains == beforeStarved.poolDrains, "empty pool reported a drain");
    require(afterStarved.rejections == beforeStarved.rejections + 1, "failed allocation was not counted as a rejection");
    require(afterStarved.liveCount == beforeStarved.liveCount, "failed allocation changed live surface count");
    require(afterStarved.liveBytes == beforeStarved.liveBytes, "failed allocation changed live byte accounting");

    // The retry is bounded: a drain plus a second failure still fails closed.
    require(manager.create(exhausted, D3D11SurfaceDesc{.width = 24, .height = 24}), "could not create retry-exhaustion source surface");
    require(manager.destroy(exhausted), "could not return retry-exhaustion source surface");
    require(manager.stats().pooledCount == 1, "retry-exhaustion source did not enter the pool");

    const auto beforeExhausted = manager.stats();
    manager.debugFailAllocations(2);
    require(!manager.create(6005, D3D11SurfaceDesc{.width = 64, .height = 64}), "exhausted allocation retry unexpectedly succeeded");
    const auto afterExhausted = manager.stats();
    require(afterExhausted.poolDrains == beforeExhausted.poolDrains + 1, "exhausted retry did not drain the pool");
    require(afterExhausted.liveCount == beforeExhausted.liveCount, "exhausted retry changed live surface count");
    require(afterExhausted.liveBytes == beforeExhausted.liveBytes, "exhausted retry changed live byte accounting");
    manager.debugFailAllocations(0);
}

void testResizeUnderAllocationFailurePreservesSurface() {
    Fixture fixture = createFixture();
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    constexpr registry::SurfaceHandle handle = 6101;
    require(manager.create(handle, D3D11SurfaceDesc{.width = 32, .height = 32}), "could not create resize-failure surface");

    D3D11SurfaceInfo before{};
    require(manager.info(handle, before), "could not inspect resize-failure surface");
    ID3D11ShaderResourceView* resource = manager.shaderResource(handle, before.generation);
    require(resource != nullptr, "resize-failure surface has no SRV");

    manager.debugFailAllocations(1);
    require(!manager.resize(handle, 64, 64), "resize unexpectedly survived an allocation failure");
    manager.debugFailAllocations(0);

    D3D11SurfaceInfo after{};
    require(manager.info(handle, after), "failed resize removed the live surface");
    require(after.width == before.width && after.height == before.height, "failed resize changed dimensions");
    require(after.generation == before.generation, "failed resize changed generation");
    require(manager.shaderResource(handle, after.generation) == resource, "failed resize replaced the valid prior resource");
    require(manager.stats().liveBytes == before.bytes, "failed resize corrupted live byte accounting");
}


bool samePixel(const std::array<std::uint8_t, 4>& pixel, Color color) {
    return pixel[0] == color.r && pixel[1] == color.g && pixel[2] == color.b && pixel[3] == color.a;
}

void testTransparentAndOpaqueClearPolicy() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    constexpr registry::SurfaceHandle transparent = 7001;
    constexpr registry::SurfaceHandle opaque = 7002;

    // A transparent surface must read back fully zeroed, not black-opaque:
    // a composited world panel depends on the alpha channel being honoured.
    require(
        manager.create(transparent, D3D11SurfaceDesc{
            .width = 32,
            .height = 32,
            .clearColor = Color{0, 0, 0, 0},
        }),
        "could not create transparent surface");

    D3D11SurfaceInfo info{};
    require(manager.info(transparent, info), "could not query transparent surface");
    require(
        manager.render(transparent, renderer, std::span<const DrawCommand>{}, nullptr),
        "empty render on the transparent surface failed");

    auto* view = manager.shaderResource(transparent, info.generation);
    for (const auto [x, y] : {std::pair<std::uint32_t, std::uint32_t>{0, 0},
                              std::pair<std::uint32_t, std::uint32_t>{16, 16},
                              std::pair<std::uint32_t, std::uint32_t>{31, 31}}) {
        const auto pixel = readPixel(fixture.device.Get(), fixture.context.Get(), view, x, y);
        require(samePixel(pixel, Color{0, 0, 0, 0}), "transparent clear did not zero the surface");
    }

    require(
        manager.create(opaque, D3D11SurfaceDesc{
            .width = 32,
            .height = 32,
            .clearColor = Color{12, 34, 56, 255},
        }),
        "could not create opaque surface");
    require(manager.info(opaque, info), "could not query opaque surface");
    require(
        manager.render(opaque, renderer, std::span<const DrawCommand>{}, nullptr),
        "empty render on the opaque surface failed");
    require(
        samePixel(
            readPixel(fixture.device.Get(), fixture.context.Get(), manager.shaderResource(opaque, info.generation), 16, 16),
            Color{12, 34, 56, 255}),
        "opaque clear colour was not applied");

    // A clear-colour change must take effect on the next render, not the next
    // resource replacement.
    require(manager.setClearColor(opaque, Color{200, 100, 50, 255}), "could not change surface clear colour");
    require(
        manager.render(opaque, renderer, std::span<const DrawCommand>{}, nullptr),
        "empty render after clear-colour change failed");
    require(manager.info(opaque, info), "could not re-query opaque surface");
    require(info.generation == 1, "changing clear colour must not replace the resource");
    require(
        samePixel(
            readPixel(fixture.device.Get(), fixture.context.Get(), manager.shaderResource(opaque, info.generation), 16, 16),
            Color{200, 100, 50, 255}),
        "changed clear colour was not applied on the next render");
}

void testMultiSurfaceIsolation() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    constexpr registry::SurfaceHandle left = 7101;
    constexpr registry::SurfaceHandle right = 7102;
    const D3D11SurfaceDesc desc{.width = 32, .height = 32, .clearColor = Color{0, 0, 0, 255}};

    require(manager.create(left, desc), "could not create first isolation surface");
    require(manager.create(right, desc), "could not create second isolation surface");

    D3D11SurfaceInfo leftInfo{};
    D3D11SurfaceInfo rightInfo{};
    require(manager.info(left, leftInfo) && manager.info(right, rightInfo), "could not query isolation surfaces");
    require(
        manager.shaderResource(left, leftInfo.generation) != manager.shaderResource(right, rightInfo.generation),
        "two live surfaces shared one GPU resource");

    auto red = createDocument(renderer, Color{255, 0, 0, 255}, {32.0f, 32.0f});
    auto green = createDocument(renderer, Color{0, 255, 0, 255}, {32.0f, 32.0f});
    require(manager.render(left, renderer, red->buildDisplayList(), nullptr), "could not render the first isolation surface");
    require(manager.render(right, renderer, green->buildDisplayList(), nullptr), "could not render the second isolation surface");

    const auto readLeft = [&] {
        return readPixel(fixture.device.Get(), fixture.context.Get(), manager.shaderResource(left, leftInfo.generation), 16, 16);
    };
    const auto readRight = [&] {
        return readPixel(fixture.device.Get(), fixture.context.Get(), manager.shaderResource(right, rightInfo.generation), 16, 16);
    };

    require(samePixel(readLeft(), Color{255, 0, 0, 255}), "first surface did not retain its own content");
    require(samePixel(readRight(), Color{0, 255, 0, 255}), "second surface did not retain its own content");

    // Re-rendering one surface must not disturb the other.
    auto blue = createDocument(renderer, Color{0, 0, 255, 255}, {32.0f, 32.0f});
    require(manager.render(left, renderer, blue->buildDisplayList(), nullptr), "could not re-render the first isolation surface");
    require(samePixel(readLeft(), Color{0, 0, 255, 255}), "re-render did not update the first surface");
    require(samePixel(readRight(), Color{0, 255, 0, 255}), "re-rendering one surface overwrote another surface");

    // Resizing one surface must not disturb the other either.
    require(manager.resize(left, 48, 48), "could not resize the first isolation surface");
    require(samePixel(readRight(), Color{0, 255, 0, 255}), "resizing one surface overwrote another surface");
}

void testPooledResourceDoesNotLeakPreviousPixels() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    constexpr registry::SurfaceHandle first = 7201;
    constexpr registry::SurfaceHandle second = 7202;
    const D3D11SurfaceDesc desc{.width = 32, .height = 32, .clearColor = Color{0, 0, 0, 255}};

    require(manager.create(first, desc), "could not create pool-leak source surface");
    D3D11SurfaceInfo info{};
    require(manager.info(first, info), "could not query pool-leak source surface");

    auto white = createDocument(renderer, Color{255, 255, 255, 255}, {32.0f, 32.0f});
    require(manager.render(first, renderer, white->buildDisplayList(), nullptr), "could not render pool-leak source surface");
    require(
        samePixel(
            readPixel(fixture.device.Get(), fixture.context.Get(), manager.shaderResource(first, info.generation), 16, 16),
            Color{255, 255, 255, 255}),
        "pool-leak source surface did not take its content");

    require(manager.destroy(first), "could not return pool-leak source surface");
    require(manager.stats().pooledCount == 1, "pool-leak source surface did not enter the pool");

    // The recycled texture still holds the previous document's pixels, so it
    // must be cleared before it is ever exposed - not merely before its first
    // render, which the caller may skip entirely.
    require(
        manager.create(second, D3D11SurfaceDesc{
            .width = 32,
            .height = 32,
            .clearColor = Color{0, 0, 0, 0},
        }),
        "could not create pool-leak destination surface");
    require(manager.stats().reuses == 1, "pool-leak destination surface was not served from the pool");

    require(manager.info(second, info), "could not query pool-leak destination surface");
    auto* view = manager.shaderResource(second, info.generation);
    for (const auto [x, y] : {std::pair<std::uint32_t, std::uint32_t>{0, 0},
                              std::pair<std::uint32_t, std::uint32_t>{16, 16},
                              std::pair<std::uint32_t, std::uint32_t>{31, 31}}) {
        const auto pixel = readPixel(fixture.device.Get(), fixture.context.Get(), view, x, y);
        require(samePixel(pixel, Color{0, 0, 0, 0}), "pooled resource leaked the previous document's pixels");
    }
}


void testSurfaceResourceShapeAndByteAccounting() {
    Fixture fixture = createFixture();
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    constexpr registry::SurfaceHandle handle = 7301;
    require(manager.create(handle, D3D11SurfaceDesc{.width = 40, .height = 24}), "could not create accounting surface");

    D3D11SurfaceInfo info{};
    require(manager.info(handle, info), "could not query accounting surface");
    require(info.bytes == 40u * 24u * 4u, "surface byte accounting does not match the resource");
    require(manager.stats().liveBytes == info.bytes, "live byte accounting does not match the surface");

    ComPtr<ID3D11Resource> resource;
    manager.shaderResource(handle, info.generation)->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> texture;
    require(SUCCEEDED(resource.As(&texture)) && texture != nullptr, "accounting surface is not a texture2D");

    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);

    // The byte budget is exact only because the resource has no hidden
    // multiplier: no MSAA (so no resolve target either) and no mip chain.
    // Offering either would require the budget to account for them first.
    require(description.SampleDesc.Count == 1, "offscreen surfaces must not be multisampled");
    require(description.SampleDesc.Quality == 0, "offscreen surfaces must not request a sample quality level");
    require(description.MipLevels == 1, "offscreen surfaces must not allocate a mip chain");
    require(description.ArraySize == 1, "offscreen surfaces must not allocate an array");
    require(description.Usage == D3D11_USAGE_DEFAULT, "offscreen surfaces must be default-usage");
    require(
        (description.BindFlags & D3D11_BIND_RENDER_TARGET) != 0 &&
            (description.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0,
        "offscreen surfaces must own both render-target and shader-resource bindings");
    require(description.CPUAccessFlags == 0, "offscreen surfaces must not be CPU accessible");
    require(description.Width == 40 && description.Height == 24, "offscreen resource does not match the requested size");
}

void testRepeatedCreateDestroyAccounting() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    constexpr std::uint32_t iterations = 64;
    const D3D11SurfaceDesc desc{.width = 64, .height = 64, .clearColor = Color{0, 0, 0, 255}};
    auto document = createDocument(renderer, Color{10, 20, 30, 255}, {64.0f, 64.0f});

    registry::SurfaceHandle handle = 7401;
    for (std::uint32_t i = 0; i < iterations; ++i, ++handle) {
        require(manager.create(handle, desc), "repeated create failed");
        require(manager.render(handle, renderer, document->buildDisplayList(), nullptr), "repeated render failed");
        require(manager.destroy(handle), "repeated destroy failed");
    }

    const auto stats = manager.stats();
    require(stats.liveCount == 0, "repeated create/destroy leaked live surface count");
    require(stats.liveBytes == 0, "repeated create/destroy leaked live byte accounting");
    require(stats.pooledCount <= D3D11SurfaceManager::DefaultMaxPoolEntries, "pool exceeded its entry budget under churn");
    require(stats.pooledBytes <= D3D11SurfaceManager::DefaultMaxPoolBytes, "pool exceeded its byte budget under churn");

    // One compatible resource should serve the whole churn: churn that keeps
    // allocating is the failure this guards against.
    require(stats.allocations == 1, "repeated create/destroy kept allocating instead of reusing the pool");
    require(stats.reuses == iterations - 1, "repeated create/destroy did not reuse the pooled resource every time");
    require(stats.rejections == 0, "repeated create/destroy produced spurious rejections");
}

void testDocumentRenderedToMultipleSurfaces() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());

    constexpr registry::SurfaceHandle smallTarget = 7501;
    constexpr registry::SurfaceHandle largeTarget = 7502;
    require(
        manager.create(smallTarget, D3D11SurfaceDesc{.width = 32, .height = 32, .clearColor = Color{0, 0, 0, 255}}),
        "could not create the small multi-surface target");
    require(
        manager.create(largeTarget, D3D11SurfaceDesc{.width = 64, .height = 48, .clearColor = Color{0, 0, 0, 255}}),
        "could not create the large multi-surface target");

    D3D11SurfaceInfo smallInfo{};
    D3D11SurfaceInfo largeInfo{};
    require(manager.info(smallTarget, smallInfo) && manager.info(largeTarget, largeInfo), "could not query multi-surface targets");

    // One document may back several surfaces. Layout is per render, at that
    // surface's size, so the surfaces stay independent and neither one's size
    // leaks into the other.
    auto document = createDocument(renderer, Color{255, 128, 0, 255}, {32.0f, 32.0f});

    document->layout({32.0f, 32.0f});
    require(manager.render(smallTarget, renderer, document->buildDisplayList(), nullptr), "could not render the document to the small surface");

    document->layout({64.0f, 48.0f});
    require(manager.render(largeTarget, renderer, document->buildDisplayList(), nullptr), "could not render the document to the large surface");

    require(manager.info(smallTarget, smallInfo) && manager.info(largeTarget, largeInfo), "could not re-query multi-surface targets");
    require(smallInfo.width == 32 && smallInfo.height == 32, "rendering another surface resized the small surface");
    require(largeInfo.width == 64 && largeInfo.height == 48, "rendering another surface resized the large surface");
    require(smallInfo.generation == 1 && largeInfo.generation == 1, "rendering must not replace a surface resource");

    const Color expected{255, 128, 0, 255};
    require(
        samePixel(
            readPixel(fixture.device.Get(), fixture.context.Get(), manager.shaderResource(smallTarget, smallInfo.generation), 16, 16),
            expected),
        "the small surface lost its content when the document was laid out again");
    require(
        samePixel(
            readPixel(fixture.device.Get(), fixture.context.Get(), manager.shaderResource(largeTarget, largeInfo.generation), 32, 24),
            expected),
        "the large surface did not take the document content");

    // Destroying one surface must leave the other completely intact.
    require(manager.destroy(smallTarget), "could not destroy the small surface");
    require(manager.info(largeTarget, largeInfo), "destroying one surface removed another");
    require(
        samePixel(
            readPixel(fixture.device.Get(), fixture.context.Get(), manager.shaderResource(largeTarget, largeInfo.generation), 32, 24),
            expected),
        "destroying one surface disturbed another surface");
}

} // namespace

int main() {
    testCreateRenderReadbackAndRestore();
    testResizeGenerationAndStaleBorrowRejection();
    testBudgetFailurePreservesPreviousSurface();
    testPoolReuseAndIdempotentDestroy();
    testInvalidAdmission();
    testLowMemoryPoolDrainAndRetry();
    testResizeUnderAllocationFailurePreservesSurface();
    testTransparentAndOpaqueClearPolicy();
    testMultiSurfaceIsolation();
    testPooledResourceDoesNotLeakPreviousPixels();
    testSurfaceResourceShapeAndByteAccounting();
    testRepeatedCreateDestroyAccounting();
    testDocumentRenderedToMultipleSurfaces();
    std::cout << "UIRepo D3D11 offscreen surface regressions passed\n";
    return 0;
}
