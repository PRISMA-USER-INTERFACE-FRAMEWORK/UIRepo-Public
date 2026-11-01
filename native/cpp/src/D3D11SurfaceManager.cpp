#include "uirepo/D3D11SurfaceManager.h"

#ifdef _WIN32

#include <limits>
#include <utility>
#include <vector>

namespace uirepo {
namespace {

constexpr float byteToFloat(std::uint8_t value) noexcept {
    return static_cast<float>(value) / 255.0f;
}

} // namespace

D3D11SurfaceManager::D3D11SurfaceManager(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    std::size_t maxLiveBytes,
    std::size_t maxPoolBytes,
    std::size_t maxPoolEntries,
    std::uint32_t maxDimension)
    : device_(device),
      context_(context),
      maxLiveBytes_(maxLiveBytes),
      maxPoolBytes_(maxPoolBytes),
      maxPoolEntries_(maxPoolEntries),
      maxDimension_(maxDimension) {}

std::size_t D3D11SurfaceManager::bytesPerPixel(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return 4;
    default:
        return 0;
    }
}

bool D3D11SurfaceManager::validateDesc(
    const D3D11SurfaceDesc& desc,
    std::size_t& bytes) const noexcept {
    bytes = 0;
    if (!device_ || !context_ || desc.width == 0 || desc.height == 0 ||
        desc.width > maxDimension_ || desc.height > maxDimension_) {
        return false;
    }

    const std::size_t pixelBytes = bytesPerPixel(desc.format);
    if (pixelBytes == 0) {
        return false;
    }

    const std::size_t width = desc.width;
    const std::size_t height = desc.height;
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        return false;
    }
    const std::size_t pixels = width * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / pixelBytes) {
        return false;
    }
    bytes = pixels * pixelBytes;
    return bytes <= maxLiveBytes_;
}

bool D3D11SurfaceManager::compatible(
    const D3D11SurfaceDesc& left,
    const D3D11SurfaceDesc& right) noexcept {
    return left.width == right.width &&
           left.height == right.height &&
           left.format == right.format;
}

void D3D11SurfaceManager::reject() noexcept {
    ++stats_.rejections;
}

void D3D11SurfaceManager::clearResource(const Resource& resource) noexcept {
    if (!context_ || !resource.rtv) {
        return;
    }
    const Color color = resource.desc.clearColor;
    const float values[4]{
        byteToFloat(color.r),
        byteToFloat(color.g),
        byteToFloat(color.b),
        byteToFloat(color.a),
    };
    context_->ClearRenderTargetView(resource.rtv.Get(), values);
}

bool D3D11SurfaceManager::createResource(
    const D3D11SurfaceDesc& desc,
    std::size_t bytes,
    Resource& out) noexcept {
    if (!device_) {
        return false;
    }
    if (forcedAllocationFailures_ != 0) {
        --forcedAllocationFailures_;
        return false;
    }

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = desc.width;
    textureDesc.Height = desc.height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = desc.format;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    Resource resource;
    resource.desc = desc;
    resource.bytes = bytes;
    if (FAILED(device_->CreateTexture2D(&textureDesc, nullptr, resource.texture.GetAddressOf())) ||
        !resource.texture) {
        return false;
    }
    if (FAILED(device_->CreateRenderTargetView(resource.texture.Get(), nullptr, resource.rtv.GetAddressOf())) ||
        !resource.rtv) {
        return false;
    }
    if (FAILED(device_->CreateShaderResourceView(resource.texture.Get(), nullptr, resource.srv.GetAddressOf())) ||
        !resource.srv) {
        return false;
    }

    out = std::move(resource);
    ++stats_.allocations;
    return true;
}

bool D3D11SurfaceManager::acquireResource(
    const D3D11SurfaceDesc& desc,
    std::size_t bytes,
    Resource& out) noexcept {
    for (auto iterator = pool_.rbegin(); iterator != pool_.rend(); ++iterator) {
        if (!compatible(iterator->desc, desc)) {
            continue;
        }

        const auto base = std::next(iterator).base();
        Resource resource = std::move(*base);
        stats_.pooledBytes -= resource.bytes;
        --stats_.pooledCount;
        pool_.erase(base);
        resource.desc.clearColor = desc.clearColor;
        out = std::move(resource);
        ++stats_.reuses;
        return true;
    }

    if (createResource(desc, bytes, out)) {
        return true;
    }

    // Under GPU or host memory pressure every pooled resource is dead weight
    // held against an allocation that is failing right now. Release the whole
    // pool and retry once before refusing the request.
    if (pool_.empty()) {
        return false;
    }
    discardPool();
    return createResource(desc, bytes, out);
}

void D3D11SurfaceManager::trimPool() noexcept {
    while (!pool_.empty() &&
           (stats_.pooledCount > maxPoolEntries_ || stats_.pooledBytes > maxPoolBytes_)) {
        stats_.pooledBytes -= pool_.front().bytes;
        --stats_.pooledCount;
        pool_.pop_front();
    }
}

void D3D11SurfaceManager::discardPool() noexcept {
    if (pool_.empty()) {
        return;
    }
    pool_.clear();
    stats_.pooledCount = 0;
    stats_.pooledBytes = 0;
    ++stats_.poolDrains;
}

void D3D11SurfaceManager::releaseResource(Resource&& resource) noexcept {
    if (!resource.texture || !resource.rtv || !resource.srv ||
        maxPoolEntries_ == 0 || resource.bytes > maxPoolBytes_) {
        return;
    }

    // Pooling is an optimisation, so a failed insert must drop the resource
    // rather than propagate out of this noexcept path and terminate.
    const std::size_t bytes = resource.bytes;
    try {
        pool_.push_back(std::move(resource));
    } catch (...) {
        return;
    }

    stats_.pooledBytes += bytes;
    ++stats_.pooledCount;
    trimPool();
}

bool D3D11SurfaceManager::create(
    registry::SurfaceHandle handle,
    const D3D11SurfaceDesc& desc) noexcept {
    std::scoped_lock lock(mutex_);
    if (handle == 0 || handle <= highestHandleSeen_ || surfaces_.contains(handle)) {
        reject();
        return false;
    }

    std::size_t bytes{};
    if (!validateDesc(desc, bytes) || stats_.liveBytes > maxLiveBytes_ - bytes) {
        reject();
        return false;
    }

    Resource resource;
    if (!acquireResource(desc, bytes, resource)) {
        reject();
        return false;
    }
    clearResource(resource);

    try {
        // try_emplace leaves `resource` intact if the node allocation throws,
        // so a failed insert can return the GPU resource to the pool instead
        // of destroying it while the process is already short on memory.
        const auto iterator = surfaces_.try_emplace(handle).first;
        iterator->second.resource = std::move(resource);
        iterator->second.generation = 1;
    } catch (...) {
        surfaces_.erase(handle);
        releaseResource(std::move(resource));
        reject();
        return false;
    }

    highestHandleSeen_ = handle;
    ++stats_.liveCount;
    stats_.liveBytes += bytes;
    return true;
}

bool D3D11SurfaceManager::destroy(registry::SurfaceHandle handle) noexcept {
    std::scoped_lock lock(mutex_);
    const auto iterator = surfaces_.find(handle);
    if (iterator == surfaces_.end()) {
        return handle != 0 && handle <= highestHandleSeen_;
    }

    Resource resource = std::move(iterator->second.resource);
    stats_.liveBytes -= resource.bytes;
    --stats_.liveCount;
    surfaces_.erase(iterator);
    releaseResource(std::move(resource));
    return true;
}

bool D3D11SurfaceManager::resize(
    registry::SurfaceHandle handle,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    std::scoped_lock lock(mutex_);
    const auto iterator = surfaces_.find(handle);
    if (iterator == surfaces_.end()) {
        reject();
        return false;
    }

    Surface& surface = iterator->second;
    if (surface.resource.desc.width == width && surface.resource.desc.height == height) {
        return true;
    }

    D3D11SurfaceDesc replacementDesc = surface.resource.desc;
    replacementDesc.width = width;
    replacementDesc.height = height;

    std::size_t replacementBytes{};
    if (!validateDesc(replacementDesc, replacementBytes)) {
        reject();
        return false;
    }

    const std::size_t oldBytes = surface.resource.bytes;
    const std::size_t liveWithoutOld = stats_.liveBytes - oldBytes;
    if (liveWithoutOld > maxLiveBytes_ - replacementBytes) {
        reject();
        return false;
    }

    Resource replacement;
    if (!acquireResource(replacementDesc, replacementBytes, replacement)) {
        reject();
        return false;
    }
    clearResource(replacement);

    Resource previous = std::move(surface.resource);
    surface.resource = std::move(replacement);
    ++surface.generation;
    stats_.liveBytes = liveWithoutOld + replacementBytes;
    ++stats_.resizes;
    releaseResource(std::move(previous));
    return true;
}

bool D3D11SurfaceManager::setClearColor(
    registry::SurfaceHandle handle,
    Color color) noexcept {
    std::scoped_lock lock(mutex_);
    const auto iterator = surfaces_.find(handle);
    if (iterator == surfaces_.end()) {
        return false;
    }
    iterator->second.resource.desc.clearColor = color;
    return true;
}

bool D3D11SurfaceManager::clear(registry::SurfaceHandle handle) noexcept {
    Resource snapshot;
    {
        std::scoped_lock lock(mutex_);
        const auto iterator = surfaces_.find(handle);
        if (iterator == surfaces_.end() || !iterator->second.resource.rtv) {
            return false;
        }
        snapshot.desc = iterator->second.resource.desc;
        snapshot.rtv = iterator->second.resource.rtv;
    }
    clearResource(snapshot);
    return true;
}

bool D3D11SurfaceManager::rebindDevice(
    ID3D11Device* device,
    ID3D11DeviceContext* context) noexcept {
    if (device == nullptr || context == nullptr) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    if (device_.Get() == device && context_.Get() == context) {
        return true;
    }

    const auto oldDevice = device_;
    const auto oldContext = context_;
    device_ = device;
    context_ = context;

    // Pooled resources belong to the device being replaced and can never be
    // reused after the rebind, so free them before allocating replacements.
    // A failed rebind therefore also leaves the pool empty, which is correct:
    // the cache is worth nothing next to completing the rebuild.
    discardPool();

    std::vector<std::pair<registry::SurfaceHandle, Resource>> replacements;
    try {
        replacements.reserve(surfaces_.size());
    } catch (...) {
        device_ = oldDevice;
        context_ = oldContext;
        reject();
        return false;
    }

    for (const auto& [handle, surface] : surfaces_) {
        Resource replacement;
        if (!createResource(surface.resource.desc, surface.resource.bytes, replacement)) {
            device_ = oldDevice;
            context_ = oldContext;
            reject();
            return false;
        }
        clearResource(replacement);
        replacements.emplace_back(handle, std::move(replacement));
    }

    for (auto& [handle, replacement] : replacements) {
        auto iterator = surfaces_.find(handle);
        if (iterator == surfaces_.end()) {
            continue;
        }
        iterator->second.resource = std::move(replacement);
        ++iterator->second.generation;
    }
    ++stats_.deviceRebuilds;
    return true;
}

bool D3D11SurfaceManager::render(
    registry::SurfaceHandle handle,
    D3D11Renderer& renderer,
    std::span<const DrawCommand> commands,
    D3D11RenderStats* stats) noexcept {
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    Size size{};
    Color clearColor{};
    {
        std::scoped_lock lock(mutex_);
        const auto iterator = surfaces_.find(handle);
        if (iterator == surfaces_.end() || !iterator->second.resource.rtv) {
            return false;
        }
        target = iterator->second.resource.rtv;
        size = {
            static_cast<float>(iterator->second.resource.desc.width),
            static_cast<float>(iterator->second.resource.desc.height),
        };
        clearColor = iterator->second.resource.desc.clearColor;
    }

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previousTarget{renderer.target()};
    const Size previousSize = renderer.targetSize();

    renderer.setTarget(target.Get(), size);
    const float values[4]{
        byteToFloat(clearColor.r),
        byteToFloat(clearColor.g),
        byteToFloat(clearColor.b),
        byteToFloat(clearColor.a),
    };
    context_->ClearRenderTargetView(target.Get(), values);
    const bool rendered = renderer.render(commands, stats);
    renderer.setTarget(previousTarget.Get(), previousSize);
    return rendered;
}

bool D3D11SurfaceManager::info(
    registry::SurfaceHandle handle,
    D3D11SurfaceInfo& out) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto iterator = surfaces_.find(handle);
    if (iterator == surfaces_.end()) {
        return false;
    }

    const Surface& surface = iterator->second;
    out = {
        .handle = handle,
        .width = surface.resource.desc.width,
        .height = surface.resource.desc.height,
        .format = surface.resource.desc.format,
        .clearColor = surface.resource.desc.clearColor,
        .generation = surface.generation,
        .bytes = surface.resource.bytes,
    };
    return true;
}

ID3D11RenderTargetView* D3D11SurfaceManager::renderTarget(
    registry::SurfaceHandle handle,
    std::uint64_t expectedGeneration) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto iterator = surfaces_.find(handle);
    if (iterator == surfaces_.end() || iterator->second.generation != expectedGeneration) {
        return nullptr;
    }
    return iterator->second.resource.rtv.Get();
}

ID3D11ShaderResourceView* D3D11SurfaceManager::shaderResource(
    registry::SurfaceHandle handle,
    std::uint64_t expectedGeneration) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto iterator = surfaces_.find(handle);
    if (iterator == surfaces_.end() || iterator->second.generation != expectedGeneration) {
        return nullptr;
    }
    return iterator->second.resource.srv.Get();
}

D3D11SurfaceStats D3D11SurfaceManager::stats() const noexcept {
    std::scoped_lock lock(mutex_);
    return stats_;
}

void D3D11SurfaceManager::debugFailAllocations(std::uint32_t count) noexcept {
    std::scoped_lock lock(mutex_);
    forcedAllocationFailures_ = count;
}

void D3D11SurfaceManager::clearPool() noexcept {
    std::scoped_lock lock(mutex_);
    pool_.clear();
    stats_.pooledCount = 0;
    stats_.pooledBytes = 0;
}

void D3D11SurfaceManager::shutdown() noexcept {
    std::scoped_lock lock(mutex_);
    surfaces_.clear();
    pool_.clear();
    stats_.liveCount = 0;
    stats_.liveBytes = 0;
    stats_.pooledCount = 0;
    stats_.pooledBytes = 0;
}

} // namespace uirepo

#endif
