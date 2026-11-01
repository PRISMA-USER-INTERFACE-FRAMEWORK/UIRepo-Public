#pragma once

#ifdef _WIN32

#include "uirepo/Core.h"
#include "uirepo/D3D11Renderer.h"
#include "uirepo/RegistryTypes.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <unordered_map>

namespace uirepo {

struct D3D11SurfaceDesc {
    std::uint32_t width{};
    std::uint32_t height{};
    DXGI_FORMAT format{DXGI_FORMAT_R8G8B8A8_UNORM};
    Color clearColor{0, 0, 0, 0};
};

struct D3D11SurfaceInfo {
    registry::SurfaceHandle handle{};
    std::uint32_t width{};
    std::uint32_t height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    Color clearColor{0, 0, 0, 0};
    std::uint64_t generation{};
    std::size_t bytes{};
};

struct D3D11SurfaceStats {
    std::size_t liveCount{};
    std::size_t liveBytes{};
    std::size_t pooledCount{};
    std::size_t pooledBytes{};
    std::uint64_t allocations{};
    std::uint64_t reuses{};
    std::uint64_t resizes{};
    std::uint64_t deviceRebuilds{};
    std::uint64_t rejections{};
    std::uint64_t poolDrains{};
};

class D3D11SurfaceManager final {
public:
    static constexpr std::uint32_t DefaultMaxDimension = 8192;
    static constexpr std::size_t DefaultMaxLiveBytes = 128u * 1024u * 1024u;
    static constexpr std::size_t DefaultMaxPoolBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t DefaultMaxPoolEntries = 16;

    D3D11SurfaceManager(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        std::size_t maxLiveBytes = DefaultMaxLiveBytes,
        std::size_t maxPoolBytes = DefaultMaxPoolBytes,
        std::size_t maxPoolEntries = DefaultMaxPoolEntries,
        std::uint32_t maxDimension = DefaultMaxDimension);

    D3D11SurfaceManager(const D3D11SurfaceManager&) = delete;
    D3D11SurfaceManager& operator=(const D3D11SurfaceManager&) = delete;

    [[nodiscard]] bool create(registry::SurfaceHandle handle, const D3D11SurfaceDesc& desc) noexcept;
    [[nodiscard]] bool destroy(registry::SurfaceHandle handle) noexcept;
    [[nodiscard]] bool resize(registry::SurfaceHandle handle, std::uint32_t width, std::uint32_t height) noexcept;
    [[nodiscard]] bool setClearColor(registry::SurfaceHandle handle, Color color) noexcept;
    [[nodiscard]] bool clear(registry::SurfaceHandle handle) noexcept;
    [[nodiscard]] bool rebindDevice(ID3D11Device* device, ID3D11DeviceContext* context) noexcept;

    [[nodiscard]] bool render(
        registry::SurfaceHandle handle,
        D3D11Renderer& renderer,
        std::span<const DrawCommand> commands,
        D3D11RenderStats* stats = nullptr) noexcept;

    [[nodiscard]] bool info(registry::SurfaceHandle handle, D3D11SurfaceInfo& out) const noexcept;
    [[nodiscard]] ID3D11RenderTargetView* renderTarget(
        registry::SurfaceHandle handle,
        std::uint64_t expectedGeneration) const noexcept;
    [[nodiscard]] ID3D11ShaderResourceView* shaderResource(
        registry::SurfaceHandle handle,
        std::uint64_t expectedGeneration) const noexcept;

    [[nodiscard]] D3D11SurfaceStats stats() const noexcept;
    void clearPool() noexcept;
    void shutdown() noexcept;

    // Test-only: force the next `count` GPU allocations to fail so the
    // low-memory drain-and-retry path can be exercised deterministically.
    void debugFailAllocations(std::uint32_t count) noexcept;

private:
    struct Resource {
        D3D11SurfaceDesc desc{};
        std::size_t bytes{};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    };

    struct Surface {
        Resource resource;
        std::uint64_t generation{1};
    };

    [[nodiscard]] bool validateDesc(const D3D11SurfaceDesc& desc, std::size_t& bytes) const noexcept;
    [[nodiscard]] bool acquireResource(const D3D11SurfaceDesc& desc, std::size_t bytes, Resource& out) noexcept;
    [[nodiscard]] bool createResource(const D3D11SurfaceDesc& desc, std::size_t bytes, Resource& out) noexcept;
    void clearResource(const Resource& resource) noexcept;
    void releaseResource(Resource&& resource) noexcept;
    void trimPool() noexcept;
    void discardPool() noexcept;
    [[nodiscard]] static bool compatible(const D3D11SurfaceDesc& left, const D3D11SurfaceDesc& right) noexcept;
    [[nodiscard]] static std::size_t bytesPerPixel(DXGI_FORMAT format) noexcept;
    void reject() noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    const std::size_t maxLiveBytes_{};
    const std::size_t maxPoolBytes_{};
    const std::size_t maxPoolEntries_{};
    const std::uint32_t maxDimension_{};

    mutable std::mutex mutex_;
    std::unordered_map<registry::SurfaceHandle, Surface> surfaces_;
    std::deque<Resource> pool_;
    registry::SurfaceHandle highestHandleSeen_{};
    std::uint32_t forcedAllocationFailures_{};
    D3D11SurfaceStats stats_{};
};

} // namespace uirepo

#endif
