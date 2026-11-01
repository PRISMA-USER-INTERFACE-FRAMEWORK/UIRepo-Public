#include "uirepo/Core.h"
#include "uirepo/D3D11Renderer.h"

#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

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

void requireNoD3D11Errors(ID3D11InfoQueue* infoQueue) {
    require(infoQueue != nullptr, "D3D11 debug info queue is unavailable");

    bool failed = false;
    const auto count = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 index = 0; index < count; ++index) {
        SIZE_T size = 0;
        require(
            SUCCEEDED(infoQueue->GetMessage(index, nullptr, &size)) && size >= sizeof(D3D11_MESSAGE),
            "ID3D11InfoQueue::GetMessage size query failed");

        std::vector<std::byte> storage(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        require(
            SUCCEEDED(infoQueue->GetMessage(index, message, &size)),
            "ID3D11InfoQueue::GetMessage failed");

        if (message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION ||
            message->Severity == D3D11_MESSAGE_SEVERITY_ERROR) {
            failed = true;
            std::cerr << "D3D11 debug-layer failure: "
                      << (message->pDescription != nullptr ? message->pDescription : "<no description>")
                      << '\n';
        }
    }

    require(!failed, "UIRepo emitted D3D11 debug-layer error/corruption messages");
}

} // namespace

int main() {
    constexpr D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL actual{};
    const HRESULT debugResult = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        D3D11_CREATE_DEVICE_DEBUG,
        requested,
        static_cast<UINT>(std::size(requested)),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        &actual,
        context.GetAddressOf());

    if (debugResult == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        std::cout << "UIRepo Stage 0 D3D11 debug layer unavailable; native WARP debug check skipped\n";
        return 0;
    }
    require(SUCCEEDED(debugResult), "D3D11CreateDevice(WARP + DEBUG) failed");

    ComPtr<ID3D11InfoQueue> infoQueue;
    require(SUCCEEDED(device.As(&infoQueue)) && infoQueue, "D3D11 debug info queue query failed");

    D3D11_TEXTURE2D_DESC description{};
    description.Width = 128;
    description.Height = 128;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;

    ComPtr<ID3D11Texture2D> targetTexture;
    require(
        SUCCEEDED(device->CreateTexture2D(&description, nullptr, targetTexture.GetAddressOf())),
        "CreateTexture2D debug target failed");

    ComPtr<ID3D11RenderTargetView> targetView;
    require(
        SUCCEEDED(device->CreateRenderTargetView(targetTexture.Get(), nullptr, targetView.GetAddressOf())),
        "CreateRenderTargetView debug target failed");

    infoQueue->ClearStoredMessages();

    D3D11Renderer renderer(device.Get(), context.Get());
    renderer.setTarget(targetView.Get(), {128.0f, 128.0f});

    auto root = std::make_unique<PanelNode>("debug-root");
    root->style().background = Color{48, 64, 96, 255};
    root->style().padding = Thickness{8.0f};
    auto& label = root->emplaceChild<TextNode>("debug-label", "UIRepo D3D11 debug probe");
    label.setFontSize(20.0f);
    label.style().width = Length::Auto();
    label.style().height = Length::Auto();

    Document document(std::move(root), &renderer.textMeasurer());
    document.layout({128.0f, 128.0f});

    D3D11RenderStats stats{};
    require(renderer.render(document.buildDisplayList(), &stats), "debug-layer renderer rejected valid display list");
    require(stats.fillRects >= 1, "debug-layer render did not execute fill rectangle");
    require(stats.textCommands == 1, "debug-layer render did not execute text command");
    require(stats.glyphRuns >= 1, "debug-layer render did not rasterize glyphs");
    require(stats.clipPushes == stats.clipPops, "debug-layer render left clip stack unbalanced");

    context->Flush();
    requireNoD3D11Errors(infoQueue.Get());

    std::cout << "UIRepo Stage 0 D3D11/WARP debug-layer check passed\n";
    return 0;
}
