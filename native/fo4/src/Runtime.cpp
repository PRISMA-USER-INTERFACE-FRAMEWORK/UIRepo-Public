#include "Runtime.h"

#include "ActiveDeviceState.h"
#include "ComIdentity.h"
#include "DocumentRegistry.h"
#include "FocusLeaseManager.h"
#include "Stage0Diagnostics.h"
#include "SwapChainCompat.h"

#include <RE/Fallout.h>
#include <spdlog/spdlog.h>

#include <array>
#include <exception>
#include <string>
#include <utility>

namespace uirepo::fo4 {
namespace {

struct RuntimeErrorOnce final {
    bool targetDeviceMismatch{};
    bool engineImmediateContextUnavailable{};
    bool engineRendererInitialization{};
    bool engineTargetNotTexture{};
    bool fallbackGetDevice{};
    bool fallbackImmediateContextUnavailable{};
    bool fallbackRendererInitialization{};
    bool fallbackGetBuffer{};
    bool fallbackCreateRenderTargetView{};
    bool probeFrameException{};
    bool probeFrameUnknownException{};
};

RuntimeErrorOnce g_runtimeErrorOnce;

} // namespace

Runtime& Runtime::instance() noexcept {
    static Runtime runtime;
    return runtime;
}

bool Runtime::ensureEngineTarget() noexcept {
    auto* rendererData = RE::BSGraphics::GetRendererData();
    if (rendererData == nullptr) {
        return false;
    }

    auto& renderWindow = rendererData->renderWindow[0];
    auto* rawDevice = reinterpret_cast<ID3D11Device*>(rendererData->device);
    auto* rawSwapChain = reinterpret_cast<IDXGISwapChain*>(renderWindow.swapChain);
    auto* rawTarget = reinterpret_cast<ID3D11RenderTargetView*>(
        renderWindow.swapChainRenderTarget.rtView);
    void* windowIdentity = reinterpret_cast<void*>(renderWindow.hwnd);

    if (rawDevice == nullptr || rawSwapChain == nullptr) {
        return false;
    }
    if (rawTarget == nullptr) {
        resetTarget();
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> targetDevice;
    rawTarget->GetDevice(targetDevice.GetAddressOf());
    if (!targetDevice || !sameComObject(targetDevice.Get(), rawDevice)) {
        if (!g_runtimeErrorOnce.targetDeviceMismatch) {
            g_runtimeErrorOnce.targetDeviceMismatch = true;
            /* Report both pointers: if this still fires after the identity
               comparison, the target genuinely belongs to another device
               (a wrapper or an overlay), which is a different problem. */
            spdlog::error(
                "UIRepo_F4: Fallout render target is not owned by the renderer D3D11 device; skipping frame "
                "(target device {}, engine device {})",
                static_cast<const void*>(targetDevice.Get()),
                static_cast<const void*>(rawDevice));
        }
        resetTarget();
        return false;
    }

    const bool swapChainChanged = swapChain_ != nullptr && swapChain_ != rawSwapChain;
    const bool rendererChanged =
        !device_ || !context_ || !renderer_ ||
        !sameComObject(device_.Get(), rawDevice) ||
        rendererWindowIdentity_ != windowIdentity ||
        swapChain_ != rawSwapChain;

    if (rendererChanged) {
        const bool hadRenderer = static_cast<bool>(device_) || static_cast<bool>(renderer_);
        auto* previousSwapChain = swapChain_;
        resetAll();

        if (swapChainChanged) {
            spdlog::info(
                "UIRepo_F4 Stage0: engine swap chain changed {} -> {}; rebuilding native renderer state",
                static_cast<const void*>(previousSwapChain),
                static_cast<const void*>(rawSwapChain));
        }

        Microsoft::WRL::ComPtr<ID3D11Device> engineDevice{rawDevice};
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> engineContext;
        engineDevice->GetImmediateContext(engineContext.GetAddressOf());
        if (!engineContext) {
            if (!g_runtimeErrorOnce.engineImmediateContextUnavailable) {
                g_runtimeErrorOnce.engineImmediateContextUnavailable = true;
                spdlog::error(
                    "UIRepo_F4: Fallout renderer device returned no immediate D3D11 context; will retry next frame");
            }
            return false;
        }

        try {
            auto renderer = std::make_unique<D3D11Renderer>(
                engineDevice.Get(),
                engineContext.Get());
            const auto featureLevel = engineDevice->GetFeatureLevel();
            device_ = std::move(engineDevice);
            context_ = std::move(engineContext);
            renderer_ = std::move(renderer);
            swapChain_ = rawSwapChain;
            rendererWindowIdentity_ = windowIdentity;
            ++rendererGeneration_;
            DocumentRegistry::instance().setTextMeasurer(&renderer_->textMeasurer());
            buildProbeDocument();

            spdlog::info(
                "UIRepo_F4: {} Fallout-native D3D11 renderer generation {} initialized (feature level 0x{:04X})",
                hadRenderer ? "rebuilt" : "initialized",
                rendererGeneration_,
                static_cast<unsigned>(featureLevel));
        } catch (const std::exception& exception) {
            if (!g_runtimeErrorOnce.engineRendererInitialization) {
                g_runtimeErrorOnce.engineRendererInitialization = true;
                spdlog::error(
                    "UIRepo_F4: Fallout-native renderer initialization failed: {}",
                    exception.what());
            }
            resetAll();
            return false;
        } catch (...) {
            if (!g_runtimeErrorOnce.engineRendererInitialization) {
                g_runtimeErrorOnce.engineRendererInitialization = true;
                spdlog::error(
                    "UIRepo_F4: Fallout-native renderer initialization failed with an unknown exception");
            }
            resetAll();
            return false;
        }
    }

    Microsoft::WRL::ComPtr<ID3D11Resource> targetResource;
    rawTarget->GetResource(targetResource.GetAddressOf());
    if (!targetResource) {
        resetTarget();
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> targetTexture;
    if (FAILED(targetResource.As(&targetTexture)) || !targetTexture) {
        if (!g_runtimeErrorOnce.engineTargetNotTexture) {
            g_runtimeErrorOnce.engineTargetNotTexture = true;
            spdlog::error("UIRepo_F4: Fallout render target resource is not a D3D11 texture");
        }
        resetTarget();
        return false;
    }

    D3D11_TEXTURE2D_DESC description{};
    targetTexture->GetDesc(&description);
    if (description.Width == 0 || description.Height == 0) {
        resetTarget();
        return false;
    }

    const Size currentSize{
        static_cast<float>(description.Width),
        static_cast<float>(description.Height),
    };
    const bool targetChanged =
        targetView_.Get() != rawTarget ||
        targetSize_.width != currentSize.width ||
        targetSize_.height != currentSize.height;

    if (targetChanged) {
        resetTarget();
        targetView_ = rawTarget;
        targetSize_ = currentSize;
        renderer_->setTarget(targetView_.Get(), targetSize_);
        ++rendererGeneration_;

        const auto viewFormat = renderer_->targetFormat();
        const auto colorSpace = renderer_->targetColorSpace();
        spdlog::info(
            "UIRepo_F4: Fallout-native render target generation {} acquired ({}x{}, resource format {}, rtv format {}, color-space {}, samples {})",
            rendererGeneration_,
            description.Width,
            description.Height,
            static_cast<unsigned>(description.Format),
            static_cast<unsigned>(viewFormat),
            D3D11Renderer::targetColorSpaceName(colorSpace),
            description.SampleDesc.Count);
    }

    return renderer_ && document_ && targetView_ &&
           targetSize_.width > 0.0f && targetSize_.height > 0.0f;
}

bool Runtime::ensureDevice(IDXGISwapChain* swapChain) noexcept {
    if (swapChain == nullptr) {
        return false;
    }

    if (swapChain_ != swapChain) {
        if (swapChain_ != nullptr) {
            spdlog::info("UIRepo_F4: fallback swap chain changed, rebuilding native renderer state");
            resetAll();
        }

        swapChain_ = swapChain;
        const auto policy = SwapChainCompat::classifyBackbuffer(swapChain);
        backbufferBorrowed_ = policy.borrowed;
        spdlog::info(
            "UIRepo_F4: fallback swap-chain GetBuffer owner '{}' ({})",
            policy.getBufferOwner,
            backbufferBorrowed_
                ? "known frame-generation proxy, borrowed backbuffer"
                : "normal COM ownership");
    }

    if (device_ && context_ && renderer_) {
        return true;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    const HRESULT result = swapChain->GetDevice(
        __uuidof(ID3D11Device),
        reinterpret_cast<void**>(device.GetAddressOf()));
    if (FAILED(result) || !device) {
        if (!g_runtimeErrorOnce.fallbackGetDevice) {
            g_runtimeErrorOnce.fallbackGetDevice = true;
            spdlog::error(
                "UIRepo_F4: fallback IDXGISwapChain::GetDevice failed: 0x{:08X}",
                static_cast<unsigned>(result));
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.GetAddressOf());
    if (!context) {
        if (!g_runtimeErrorOnce.fallbackImmediateContextUnavailable) {
            g_runtimeErrorOnce.fallbackImmediateContextUnavailable = true;
            spdlog::error("UIRepo_F4: fallback D3D11 immediate context is unavailable");
        }
        return false;
    }

    try {
        auto renderer = std::make_unique<D3D11Renderer>(device.Get(), context.Get());
        const auto featureLevel = device->GetFeatureLevel();
        device_ = std::move(device);
        context_ = std::move(context);
        renderer_ = std::move(renderer);
        rendererWindowIdentity_ = nullptr;
        ++rendererGeneration_;
        DocumentRegistry::instance().setTextMeasurer(&renderer_->textMeasurer());
        buildProbeDocument();
        spdlog::info(
            "UIRepo_F4: fallback D3D11 renderer generation {} initialized (feature level 0x{:04X})",
            rendererGeneration_,
            static_cast<unsigned>(featureLevel));
        return true;
    } catch (const std::exception& exception) {
        if (!g_runtimeErrorOnce.fallbackRendererInitialization) {
            g_runtimeErrorOnce.fallbackRendererInitialization = true;
            spdlog::error("UIRepo_F4: fallback renderer initialization failed: {}", exception.what());
        }
    } catch (...) {
        if (!g_runtimeErrorOnce.fallbackRendererInitialization) {
            g_runtimeErrorOnce.fallbackRendererInitialization = true;
            spdlog::error("UIRepo_F4: fallback renderer initialization failed with an unknown exception");
        }
    }

    resetAll();
    return false;
}

bool Runtime::ensureTarget(IDXGISwapChain* swapChain) noexcept {
    if (targetView_ && targetSize_.width > 0.0f && targetSize_.height > 0.0f) {
        return true;
    }
    if (!device_ || !renderer_ || swapChain == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    const HRESULT bufferResult = swapChain->GetBuffer(
        0,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    if (FAILED(bufferResult) || !backBuffer) {
        if (!g_runtimeErrorOnce.fallbackGetBuffer) {
            g_runtimeErrorOnce.fallbackGetBuffer = true;
            spdlog::error(
                "UIRepo_F4: fallback failed to acquire swap-chain backbuffer: 0x{:08X}",
                static_cast<unsigned>(bufferResult));
        }
        return false;
    }

    ID3D11Texture2D* backBufferRaw = backbufferBorrowed_ ? backBuffer.Detach() : backBuffer.Get();
    if (backBufferRaw == nullptr) {
        return false;
    }

    D3D11_TEXTURE2D_DESC description{};
    backBufferRaw->GetDesc(&description);
    if (description.Width == 0 || description.Height == 0) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targetView;
    const HRESULT viewResult = device_->CreateRenderTargetView(
        backBufferRaw,
        nullptr,
        targetView.GetAddressOf());
    if (FAILED(viewResult) || !targetView) {
        if (!g_runtimeErrorOnce.fallbackCreateRenderTargetView) {
            g_runtimeErrorOnce.fallbackCreateRenderTargetView = true;
            spdlog::error(
                "UIRepo_F4: fallback failed to create backbuffer RTV: 0x{:08X}",
                static_cast<unsigned>(viewResult));
        }
        return false;
    }

    targetView_ = std::move(targetView);
    targetSize_ = {
        static_cast<float>(description.Width),
        static_cast<float>(description.Height),
    };
    renderer_->setTarget(targetView_.Get(), targetSize_);
    ++rendererGeneration_;
    const auto viewFormat = renderer_->targetFormat();
    const auto colorSpace = renderer_->targetColorSpace();
    spdlog::info(
        "UIRepo_F4: fallback render target generation {} acquired ({}x{}, resource format {}, rtv format {}, color-space {}, samples {}, backbuffer {})",
        rendererGeneration_,
        description.Width,
        description.Height,
        static_cast<unsigned>(description.Format),
        static_cast<unsigned>(viewFormat),
        D3D11Renderer::targetColorSpaceName(colorSpace),
        description.SampleDesc.Count,
        backbufferBorrowed_ ? "borrowed" : "owned");
    return true;
}

void Runtime::buildProbeDocument() {
    if (!renderer_) {
        document_.reset();
        return;
    }

    auto root = std::make_unique<PanelNode>("root");
    root->style().padding = Thickness{24.0f};
    root->style().alignItems = Align::Start;

    auto& card = root->emplaceChild<StackNode>("native-card", Axis::Column);
    card.style().width = Length::Pixels(460.0f);
    card.style().height = Length::Auto();
    card.style().padding = Thickness{18.0f, 14.0f, 18.0f, 14.0f};
    card.style().gap = 4.0f;
    card.style().alignItems = Align::Start;
    card.style().background = Color{18, 20, 24, 224};

    auto& title = card.emplaceChild<TextNode>("title", "UIRepo native runtime");
    title.setFontSize(24.0f);
    title.setColor(Color{238, 240, 244, 255});

    auto& detail = card.emplaceChild<TextNode>("detail", "C++23 / engine D3D11 / DirectWrite");
    detail.setFontSize(16.0f);
    detail.setColor(Color{176, 184, 196, 255});

    document_ = std::make_unique<Document>(std::move(root), &renderer_->textMeasurer());
}

void Runtime::renderDocument() noexcept {
    if (!renderer_ || !document_ || !targetView_ ||
        targetSize_.width <= 0.0f || targetSize_.height <= 0.0f) {
        return;
    }

    try {
        document_->layout(targetSize_);
        D3D11RenderStats stats{};
        if (!renderer_->render(document_->buildDisplayList(), &stats)) {
            static bool reportedFailure = false;
            if (!reportedFailure) {
                reportedFailure = true;
                spdlog::error("UIRepo_F4: renderer rejected the built-in probe display list");
            }
        } else {
            if (lastReportedProbeGeneration_ != rendererGeneration_) {
                const bool firstProbeFrame = lastReportedProbeGeneration_ == 0;
                lastReportedProbeGeneration_ = rendererGeneration_;

                if (firstProbeFrame) {
                    spdlog::info(
                        "UIRepo_F4: first native probe frame rendered (rects {}, text {}, glyph runs {}, clips {}/{})",
                        stats.fillRects,
                        stats.textCommands,
                        stats.glyphRuns,
                        stats.clipPushes,
                        stats.clipPops);
                } else {
                    spdlog::info(
                        "UIRepo_F4 Stage0: native probe recovery frame rendered (generation {}, rects {}, text {}, glyph runs {}, clips {}/{})",
                        rendererGeneration_,
                        stats.fillRects,
                        stats.textCommands,
                        stats.glyphRuns,
                        stats.clipPushes,
                        stats.clipPops);
                }
            }

            const auto focusTransition = Stage0Diagnostics::consumeFocusRecoveryTransition();
            if (focusTransition != 0) {
                spdlog::info(
                    "UIRepo_F4 Stage0: focus recovery frame rendered (transition {}, generation {}, rects {}, text {}, glyph runs {}, clips {}/{})",
                    focusTransition,
                    rendererGeneration_,
                    stats.fillRects,
                    stats.textCommands,
                    stats.glyphRuns,
                    stats.clipPushes,
                    stats.clipPops);
            }

            const auto windowStyleTransition =
                Stage0Diagnostics::consumeWindowStyleRecoveryTransition();
            if (windowStyleTransition != 0) {
                spdlog::info(
                    "UIRepo_F4 Stage0: window style recovery frame rendered (transition {}, generation {}, rects {}, text {}, glyph runs {}, clips {}/{})",
                    windowStyleTransition,
                    rendererGeneration_,
                    stats.fillRects,
                    stats.textCommands,
                    stats.glyphRuns,
                    stats.clipPushes,
                    stats.clipPops);
            }
        }
    } catch (const std::exception& exception) {
        if (!g_runtimeErrorOnce.probeFrameException) {
            g_runtimeErrorOnce.probeFrameException = true;
            spdlog::error("UIRepo_F4: built-in probe frame failed: {}", exception.what());
        }
    } catch (...) {
        if (!g_runtimeErrorOnce.probeFrameUnknownException) {
            g_runtimeErrorOnce.probeFrameUnknownException = true;
            spdlog::error("UIRepo_F4: built-in probe frame failed with an unknown exception");
        }
    }

    DocumentRegistry::instance().renderVisible(*renderer_, targetSize_);
    renderCursorOverlay();
    maybeLogResourceSnapshot();
}

void Runtime::renderCursorOverlay() noexcept {
    if (!renderer_ || !FocusLeaseManager::ownsCursor()) {
        return;
    }

    float cursorX{};
    float cursorY{};
    switch (ActiveDeviceState::routedPointerLocation(cursorX, cursorY)) {
    case ActiveDeviceState::PointerLocation::OutsideClient:
        return;
    case ActiveDeviceState::PointerLocation::Unknown: {
        auto* cursor = RE::MenuCursor::GetSingleton();
        if (cursor == nullptr) {
            return;
        }
        cursorX = static_cast<float>(cursor->cursorPosX);
        cursorY = static_cast<float>(cursor->cursorPosY);
        break;
    }
    case ActiveDeviceState::PointerLocation::InsideClient:
        break;
    }

    static const std::string kCursorSource = "Data/UIRepo_F4/misc/cursor.png";
    static const std::string kCursorNodeId = "uirepo-cursor";
    constexpr float kCursorSize = 30.0f;

    const DrawImageCommand cursorCommand{
        .nodeId = kCursorNodeId,
        .bounds = Rect{cursorX, cursorY, kCursorSize, kCursorSize},
        .opacity = 1.0f,
        .zIndex = 0,
        .source = kCursorSource,
    };
    const std::array<DrawCommand, 1> commands{DrawCommand{cursorCommand}};

    try {
        D3D11RenderStats stats{};
        (void)renderer_->render(commands, &stats);
        customCursorAvailable_.store(stats.drawCalls >= 1, std::memory_order_relaxed);
    } catch (...) {
        customCursorAvailable_.store(false, std::memory_order_relaxed);
    }
}

void Runtime::renderEngineFrame() noexcept {
    std::scoped_lock lock(mutex_);
    if (!ensureEngineTarget()) {
        return;
    }
    renderDocument();
}

void Runtime::render(IDXGISwapChain* swapChain) noexcept {
    std::scoped_lock lock(mutex_);

    if (!ensureDevice(swapChain) || !ensureTarget(swapChain)) {
        return;
    }
    renderDocument();
}

bool Runtime::rendererReady() noexcept {
    std::scoped_lock lock(mutex_);
    return renderer_ && context_ && device_;
}

void Runtime::resetTarget() noexcept {
    if (renderer_) {
        renderer_->setTarget(nullptr, {});
    }
    targetView_.Reset();
    targetSize_ = {};
}

void Runtime::beforeResize() noexcept {
    std::scoped_lock lock(mutex_);
    if (targetView_) {
        spdlog::info("UIRepo_F4 Stage0: ResizeBuffers pre-release of native render target");
    }
    resetTarget();
}

void Runtime::resetAll() noexcept {
    if (renderer_) {
        logResourceSnapshot("reset");
    }
    telemetryGeneration_ = 0;
    telemetryGate_.clear();
    DocumentRegistry::instance().setTextMeasurer(nullptr);
    document_.reset();
    resetTarget();
    renderer_.reset();
    context_.Reset();
    device_.Reset();
    swapChain_ = nullptr;
    backbufferBorrowed_ = false;
    rendererWindowIdentity_ = nullptr;
}

void Runtime::shutdown() noexcept {
    std::scoped_lock lock(mutex_);
    resetAll();
}

} // namespace uirepo::fo4
