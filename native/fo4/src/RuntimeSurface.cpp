#include "Runtime.h"

#include "DocumentRegistry.h"
#include "GameThreadDispatcher.h"

#include <vector>

namespace uirepo::fo4 {

bool Runtime::bindSurfaceManager(
    ID3D11Device* device,
    ID3D11DeviceContext* context) noexcept {
    if (!GameThreadDispatcher::isGameThread() || device == nullptr || context == nullptr) {
        return false;
    }

    try {
        if (!surfaceManager_) {
            surfaceManager_ = std::make_unique<D3D11SurfaceManager>(device, context);
            return true;
        }
        return surfaceManager_->rebindDevice(device, context);
    } catch (...) {
        return false;
    }
}

void Runtime::releaseRegistrySurface(SurfaceHandle surface, void* userData) noexcept {
    if (userData == nullptr) {
        return;
    }
    static_cast<Runtime*>(userData)->releaseSurfaceResource(surface);
}

void Runtime::releaseSurfaceResource(SurfaceHandle surface) noexcept {
    if (!GameThreadDispatcher::isGameThread()) {
        if (GameThreadDispatcher::isReady()) {
            (void)GameThreadDispatcher::dispatchSafety([this, surface] {
                releaseSurfaceResource(surface);
            });
        }
        return;
    }

    std::scoped_lock lock(mutex_);
    if (surfaceManager_) {
        (void)surfaceManager_->destroy(surface);
    }
}

Runtime::SurfaceHandle Runtime::createOffscreenSurface(
    DocumentHandle document,
    std::uint32_t width,
    std::uint32_t height,
    Color clearColor) noexcept {
    if (!GameThreadDispatcher::isGameThread()) {
        return 0;
    }

    auto& registry = DocumentRegistry::instance();
    registry.setSurfaceReleaseHook(&Runtime::releaseRegistrySurface, this);

    const SurfaceHandle surface = registry.attachSurface(document);
    if (surface == 0) {
        return 0;
    }

    bool created = false;
    {
        std::scoped_lock lock(mutex_);
        if (renderer_ && device_ && context_ &&
            bindSurfaceManager(device_.Get(), context_.Get())) {
            created = surfaceManager_->create(
                surface,
                D3D11SurfaceDesc{
                    .width = width,
                    .height = height,
                    .format = DXGI_FORMAT_R8G8B8A8_UNORM,
                    .clearColor = clearColor,
                });
        }
    }

    if (!created) {
        (void)registry.detachSurface(surface);
        return 0;
    }
    return surface;
}

bool Runtime::destroyOffscreenSurface(SurfaceHandle surface) noexcept {
    return DocumentRegistry::instance().detachSurface(surface);
}

bool Runtime::resizeOffscreenSurface(
    SurfaceHandle surface,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    if (!GameThreadDispatcher::isGameThread() ||
        DocumentRegistry::instance().surfaceDocument(surface) == 0) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    return renderer_ && device_ && context_ &&
           bindSurfaceManager(device_.Get(), context_.Get()) &&
           surfaceManager_->resize(surface, width, height);
}

bool Runtime::setOffscreenSurfaceClearColor(
    SurfaceHandle surface,
    Color color) noexcept {
    if (!GameThreadDispatcher::isGameThread() ||
        DocumentRegistry::instance().surfaceDocument(surface) == 0) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    return surfaceManager_ && surfaceManager_->setClearColor(surface, color);
}

bool Runtime::renderDocumentToSurface(
    DocumentHandle document,
    SurfaceHandle surface,
    D3D11RenderStats* stats) noexcept {
    if (!GameThreadDispatcher::isGameThread()) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    if (!renderer_ || !device_ || !context_ ||
        !bindSurfaceManager(device_.Get(), context_.Get())) {
        return false;
    }

    auto& registry = DocumentRegistry::instance();
    if (registry.surfaceDocument(surface) != document) {
        return false;
    }

    D3D11SurfaceInfo info{};
    if (!surfaceManager_->info(surface, info) ||
        info.width == 0 || info.height == 0) {
        return false;
    }

    std::vector<DrawCommand> commands;
    if (!registry.snapshotDisplayList(
            document,
            Size{static_cast<float>(info.width), static_cast<float>(info.height)},
            commands)) {
        return false;
    }

    return surfaceManager_->render(surface, *renderer_, commands, stats);
}

bool Runtime::offscreenSurfaceInfo(
    SurfaceHandle surface,
    D3D11SurfaceInfo& info) noexcept {
    if (!GameThreadDispatcher::isGameThread()) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    if (!surfaceManager_ || !device_ || !context_ ||
        !bindSurfaceManager(device_.Get(), context_.Get())) {
        return false;
    }
    return surfaceManager_->info(surface, info);
}

ID3D11ShaderResourceView* Runtime::borrowOffscreenSurfaceShaderResource(
    SurfaceHandle surface,
    std::uint64_t generation) noexcept {
    if (!GameThreadDispatcher::isGameThread()) {
        return nullptr;
    }

    std::scoped_lock lock(mutex_);
    if (!surfaceManager_ || !device_ || !context_ ||
        !bindSurfaceManager(device_.Get(), context_.Get())) {
        return nullptr;
    }
    return surfaceManager_->shaderResource(surface, generation);
}

D3D11SurfaceStats Runtime::offscreenSurfaceStats() noexcept {
    std::scoped_lock lock(mutex_);
    return surfaceManager_ ? surfaceManager_->stats() : D3D11SurfaceStats{};
}

void Runtime::shutdownOffscreenSurfaces() noexcept {
    DocumentRegistry::instance().setSurfaceReleaseHook(nullptr, nullptr);
    std::scoped_lock lock(mutex_);
    if (renderer_ && !telemetryShutdownSnapshotLogged_) {
        logResourceSnapshot("shutdown");
        telemetryShutdownSnapshotLogged_ = true;
    }
    if (surfaceManager_) {
        surfaceManager_->shutdown();
        surfaceManager_.reset();
    }
}

} // namespace uirepo::fo4
