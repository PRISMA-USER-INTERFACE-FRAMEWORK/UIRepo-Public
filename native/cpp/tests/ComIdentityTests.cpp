#include "ComIdentity.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdlib>
#include <iostream>
#include <iterator>

using Microsoft::WRL::ComPtr;
using uirepo::fo4::sameComObject;

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

ComPtr<ID3D11Device> createWarpDevice() {
    constexpr D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL actual{};
    const HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        0,
        requested,
        static_cast<UINT>(std::size(requested)),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        &actual,
        nullptr);
    require(SUCCEEDED(result) && device, "D3D11CreateDevice(WARP) failed");
    return device;
}

/* The case the raw comparison gets wrong.

   A device reached through a different interface is still the same object.
   Microsoft's D3D11 often hands back an identical pointer, which is exactly
   why comparing ID3D11Device* directly survived every WARP regression while
   being wrong - under DXVK the pointers differ and the engine's render target
   was rejected as foreign every frame. */
void testDifferentInterfaceIsStillTheSameObject() {
    ComPtr<ID3D11Device> device = createWarpDevice();

    ComPtr<IDXGIDevice> asDxgi;
    require(SUCCEEDED(device.As(&asDxgi)) && asDxgi, "device does not expose IDXGIDevice");

    ComPtr<ID3D11DeviceChild> child;
    D3D11_TEXTURE2D_DESC description{};
    description.Width = 16;
    description.Height = 16;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;

    ComPtr<ID3D11Texture2D> texture;
    require(
        SUCCEEDED(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf())),
        "could not create identity probe texture");

    ComPtr<ID3D11RenderTargetView> view;
    require(
        SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, view.GetAddressOf())),
        "could not create identity probe render target view");

    /* This is the exact shape of the engine check: the device a resource
       reports must be recognised as the device we hold. */
    ComPtr<ID3D11Device> reported;
    view->GetDevice(reported.GetAddressOf());
    require(reported, "render target view reported no device");
    require(
        sameComObject(reported.Get(), device.Get()),
        "a render target's own device was not recognised as the same object");

    /* Reached through an unrelated interface, it must still compare equal. */
    require(
        sameComObject(asDxgi.Get(), device.Get()),
        "the same device reached through IDXGIDevice was treated as a different object");
    require(
        sameComObject(device.Get(), asDxgi.Get()),
        "COM identity comparison is not symmetric");
}

void testDistinctDevicesAreNotConflated() {
    ComPtr<ID3D11Device> first = createWarpDevice();
    ComPtr<ID3D11Device> second = createWarpDevice();
    require(first.Get() != second.Get(), "identity test requires two distinct devices");
    require(
        !sameComObject(first.Get(), second.Get()),
        "two genuinely different devices were treated as the same object");
}

void testNullHandling() {
    ComPtr<ID3D11Device> device = createWarpDevice();
    require(sameComObject(nullptr, nullptr), "two null pointers are the same absence");
    require(!sameComObject(device.Get(), nullptr), "a device matched null");
    require(!sameComObject(nullptr, device.Get()), "null matched a device");
}

} // namespace

int main() {
    testDifferentInterfaceIsStillTheSameObject();
    testDistinctDevicesAreNotConflated();
    testNullHandling();
    std::cout << "UIRepo COM identity regressions passed\n";
    return 0;
}
