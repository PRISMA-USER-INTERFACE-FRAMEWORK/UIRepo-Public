#pragma once

#include "uirepo/Core.h"
#include "uirepo/D3D11Renderer.h"
#include "uirepo/D3D11SurfaceManager.h"
#include "uirepo/RegistryTypes.h"
#include "uirepo/TelemetryPolicy.h"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

namespace uirepo::fo4 {

class Runtime final {
public:
    using DocumentHandle = registry::DocumentHandle;
    using SurfaceHandle = registry::SurfaceHandle;

    static Runtime& instance() noexcept;

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    void renderEngineFrame() noexcept;
    void render(IDXGISwapChain* swapChain) noexcept;

    [[nodiscard]] bool rendererReady() noexcept;

    [[nodiscard]] bool customCursorAvailable() const noexcept {
        return customCursorAvailable_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] SurfaceHandle createOffscreenSurface(
        DocumentHandle document,
        std::uint32_t width,
        std::uint32_t height,
        Color clearColor = Color{0, 0, 0, 0}) noexcept;
    [[nodiscard]] bool destroyOffscreenSurface(SurfaceHandle surface) noexcept;
    [[nodiscard]] bool resizeOffscreenSurface(
        SurfaceHandle surface,
        std::uint32_t width,
        std::uint32_t height) noexcept;
    [[nodiscard]] bool setOffscreenSurfaceClearColor(
        SurfaceHandle surface,
        Color color) noexcept;
    [[nodiscard]] bool renderDocumentToSurface(
        DocumentHandle document,
        SurfaceHandle surface,
        D3D11RenderStats* stats = nullptr) noexcept;
    [[nodiscard]] bool offscreenSurfaceInfo(
        SurfaceHandle surface,
        D3D11SurfaceInfo& info) noexcept;
    [[nodiscard]] ID3D11ShaderResourceView* borrowOffscreenSurfaceShaderResource(
        SurfaceHandle surface,
        std::uint64_t generation) noexcept;
    [[nodiscard]] D3D11SurfaceStats offscreenSurfaceStats() noexcept;
    void shutdownOffscreenSurfaces() noexcept;

    void beforeResize() noexcept;
    void shutdown() noexcept;

private:
    Runtime() = default;

    [[nodiscard]] bool ensureEngineTarget() noexcept;
    [[nodiscard]] bool ensureDevice(IDXGISwapChain* swapChain) noexcept;
    [[nodiscard]] bool ensureTarget(IDXGISwapChain* swapChain) noexcept;
    [[nodiscard]] bool bindSurfaceManager(
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept;
    static void releaseRegistrySurface(SurfaceHandle surface, void* userData) noexcept;
    void releaseSurfaceResource(SurfaceHandle surface) noexcept;
    void buildProbeDocument();
    void renderDocument() noexcept;
    void renderCursorOverlay() noexcept;
    void maybeLogResourceSnapshot() noexcept;
    void logResourceSnapshot(const char* reason) noexcept;
    void resetTarget() noexcept;
    void resetAll() noexcept;

    std::mutex mutex_;
    std::atomic<bool> customCursorAvailable_{false};
    IDXGISwapChain* swapChain_{};
    bool backbufferBorrowed_{};
    void* rendererWindowIdentity_{};
    std::uint64_t rendererGeneration_{};
    std::uint64_t lastReportedProbeGeneration_{};
    std::uint64_t telemetryGeneration_{};
    const D3D11Renderer* telemetryRendererIdentity_{};
    telemetry::PeriodicGate telemetryGate_{std::chrono::minutes(5)};
    bool telemetryShutdownSnapshotLogged_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targetView_;
    Size targetSize_{};
    std::unique_ptr<D3D11Renderer> renderer_;
    std::unique_ptr<D3D11SurfaceManager> surfaceManager_;
    std::unique_ptr<Document> document_;
};

} // namespace uirepo::fo4
