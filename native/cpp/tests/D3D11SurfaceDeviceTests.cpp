#include "uirepo/D3D11SurfaceManager.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <memory>

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

void testDeviceRebindRecreatesLiveSurfaces() {
    Fixture first = createFixture();
    Fixture second = createFixture();
    require(first.device.Get() != second.device.Get(), "device-rebind test requires distinct devices");

    D3D11SurfaceManager manager(first.device.Get(), first.context.Get());
    constexpr registry::SurfaceHandle handle = 9001;
    require(
        manager.create(handle, D3D11SurfaceDesc{
            .width = 80,
            .height = 48,
            .clearColor = Color{12, 34, 56, 255},
        }),
        "could not create device-rebind surface");

    D3D11SurfaceInfo before{};
    require(manager.info(handle, before), "could not inspect pre-rebind surface");
    require(before.generation == 1, "pre-rebind generation must begin at one");
    require(manager.shaderResource(handle, before.generation) != nullptr, "pre-rebind SRV is missing");

    require(manager.rebindDevice(second.device.Get(), second.context.Get()), "surface device rebind failed");

    D3D11SurfaceInfo after{};
    require(manager.info(handle, after), "surface disappeared after device rebind");
    require(after.width == before.width && after.height == before.height, "device rebind changed dimensions");
    require(after.bytes == before.bytes, "device rebind changed live byte accounting");
    require(after.generation == before.generation + 1, "device rebind did not invalidate surface generation");
    require(manager.shaderResource(handle, before.generation) == nullptr, "old generation SRV survived device rebind");

    ID3D11ShaderResourceView* current = manager.shaderResource(handle, after.generation);
    require(current != nullptr, "new-generation SRV is missing after device rebind");

    ComPtr<ID3D11Resource> resource;
    current->GetResource(resource.GetAddressOf());
    require(resource != nullptr, "rebound SRV has no resource");
    ComPtr<ID3D11Device> owner;
    resource->GetDevice(owner.GetAddressOf());
    require(owner.Get() == second.device.Get(), "rebound surface resource is still owned by the old device");

    D3D11Renderer renderer(second.device.Get(), second.context.Get());
    auto root = std::make_unique<PanelNode>("device-rebind-root");
    root->style().background = Color{255, 0, 0, 255};
    Document document(std::move(root), &renderer.textMeasurer());
    document.layout({80.0f, 48.0f});
    D3D11RenderStats renderStats{};
    require(
        manager.render(handle, renderer, document.buildDisplayList(), &renderStats),
        "rebound surface could not render on the new device");
    require(renderStats.fillRects >= 1, "rebound surface render executed no retained geometry");

    const auto stats = manager.stats();
    require(stats.deviceRebuilds == 1, "device rebuild telemetry did not increment");
    require(stats.liveCount == 1 && stats.liveBytes == after.bytes, "device rebuild corrupted live surface accounting");
    require(stats.pooledCount == 0 && stats.pooledBytes == 0, "old-device pooled resources survived rebind");

    const auto generation = after.generation;
    require(manager.rebindDevice(second.device.Get(), second.context.Get()), "same-device rebind should be a no-op success");
    require(manager.info(handle, after) && after.generation == generation, "same-device rebind unnecessarily invalidated generation");
    require(manager.stats().deviceRebuilds == 1, "same-device no-op changed rebuild telemetry");
}

void testFailedRebindPreservesCurrentResources() {
    Fixture fixture = createFixture();
    D3D11SurfaceManager manager(fixture.device.Get(), fixture.context.Get());
    constexpr registry::SurfaceHandle handle = 9101;
    require(manager.create(handle, D3D11SurfaceDesc{.width = 32, .height = 32}), "could not create failed-rebind surface");

    D3D11SurfaceInfo before{};
    require(manager.info(handle, before), "could not inspect failed-rebind surface");
    ID3D11ShaderResourceView* resource = manager.shaderResource(handle, before.generation);
    require(resource != nullptr, "failed-rebind surface SRV is missing");

    require(!manager.rebindDevice(nullptr, fixture.context.Get()), "null-device rebind unexpectedly succeeded");
    require(!manager.rebindDevice(fixture.device.Get(), nullptr), "null-context rebind unexpectedly succeeded");

    D3D11SurfaceInfo after{};
    require(manager.info(handle, after), "failed rebind removed the live surface");
    require(after.generation == before.generation, "failed rebind changed generation");
    require(manager.shaderResource(handle, after.generation) == resource, "failed rebind replaced the current resource");
}


void testRebindDrainsPoolAndFailsClosedUnderAllocationFailure() {
    Fixture first = createFixture();
    Fixture second = createFixture();
    require(first.device.Get() != second.device.Get(), "device-rebind test requires distinct devices");

    D3D11SurfaceManager manager(first.device.Get(), first.context.Get());
    constexpr registry::SurfaceHandle live = 9201;
    constexpr registry::SurfaceHandle pooled = 9202;

    require(manager.create(live, D3D11SurfaceDesc{.width = 40, .height = 24}), "could not create rebind live surface");
    require(manager.create(pooled, D3D11SurfaceDesc{.width = 40, .height = 24}), "could not create rebind pool source surface");
    require(manager.destroy(pooled), "could not return rebind pool source surface");
    require(manager.stats().pooledCount == 1, "rebind pool source did not enter the pool");

    D3D11SurfaceInfo before{};
    require(manager.info(live, before), "could not inspect pre-rebind live surface");
    ID3D11ShaderResourceView* resource = manager.shaderResource(live, before.generation);
    require(resource != nullptr, "pre-rebind live surface has no SRV");

    // A rebind that cannot allocate its replacements must keep the current
    // device and resources intact, and must not leave stale old-device
    // resources cached for reuse.
    manager.debugFailAllocations(1);
    require(!manager.rebindDevice(second.device.Get(), second.context.Get()), "rebind unexpectedly survived an allocation failure");
    manager.debugFailAllocations(0);

    const auto stats = manager.stats();
    require(stats.poolDrains == 1, "rebind did not release the dead device's pooled resources");
    require(stats.pooledCount == 0 && stats.pooledBytes == 0, "drained rebind pool kept accounting for released resources");
    require(stats.deviceRebuilds == 0, "failed rebind reported a device rebuild");
    require(stats.liveCount == 1 && stats.liveBytes == before.bytes, "failed rebind corrupted live surface accounting");

    D3D11SurfaceInfo after{};
    require(manager.info(live, after), "failed rebind removed the live surface");
    require(after.generation == before.generation, "failed rebind changed generation");
    require(manager.shaderResource(live, after.generation) == resource, "failed rebind replaced the current resource");

    // The same rebind succeeds once allocations are available again.
    require(manager.rebindDevice(second.device.Get(), second.context.Get()), "rebind failed after allocations recovered");
    require(manager.stats().deviceRebuilds == 1, "recovered rebind did not report a device rebuild");
    require(manager.info(live, after) && after.generation == before.generation + 1, "recovered rebind did not invalidate the stale generation");
    require(manager.shaderResource(live, before.generation) == nullptr, "stale generation still borrows a resource after rebind");
}

} // namespace

int main() {
    testDeviceRebindRecreatesLiveSurfaces();
    testFailedRebindPreservesCurrentResources();
    testRebindDrainsPoolAndFailsClosedUnderAllocationFailure();
    std::cout << "UIRepo D3D11 surface device-rebuild regressions passed\n";
    return 0;
}
