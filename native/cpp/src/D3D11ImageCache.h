#pragma once

#ifdef _WIN32

#include "uirepo/BoundedCache.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace uirepo {

struct D3D11ImageCacheTestAccess;

class D3D11ImageCache final {
public:
    static constexpr std::size_t DefaultBytes = 64u * 1024u * 1024u;
    static constexpr std::size_t DefaultEntries = 256u;
    static constexpr std::size_t DefaultNegativeBytes = 64u * 1024u;
    static constexpr std::size_t DefaultNegativeEntries = 128u;
    static constexpr std::size_t DefaultMaxSingleImageBytes = 32u * 1024u * 1024u;

    struct Texture {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource;
        std::uint32_t width{};
        std::uint32_t height{};
    };

    struct ResolveResult {
        Texture texture;
        bool success{};
    };

    explicit D3D11ImageCache(
        ID3D11Device* device,
        std::size_t maxBytes = DefaultBytes,
        std::size_t maxEntries = DefaultEntries,
        std::size_t maxSingleImageBytes = DefaultMaxSingleImageBytes);

    using BeforeUpload = bool (*)(std::size_t bytes, void* context) noexcept;

    [[nodiscard]] ResolveResult resolve(
        std::string_view source,
        BeforeUpload beforeUpload = nullptr,
        void* beforeUploadContext = nullptr) noexcept;

    [[nodiscard]] const render::CacheStats& stats() const noexcept { return cache_.stats(); }
    [[nodiscard]] std::uint64_t uploads() const noexcept { return uploads_; }
    [[nodiscard]] std::uint64_t decodeFailures() const noexcept { return decodeFailures_; }
    [[nodiscard]] std::uint64_t negativeHits() const noexcept { return negativeHits_; }
    [[nodiscard]] std::uint64_t oversizeRejections() const noexcept { return oversizeRejections_; }

private:
    friend struct D3D11ImageCacheTestAccess;

    [[nodiscard]] bool decodeAndUpload(
        std::string_view source,
        Texture& texture,
        std::size_t& bytes,
        BeforeUpload beforeUpload,
        void* beforeUploadContext,
        bool& aborted) noexcept;
    void rememberFailure(std::string_view source) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    render::BoundedLruCache<std::string, Texture> cache_;
    render::BoundedLruCache<std::string, std::uint8_t> negativeCache_;
    std::size_t maxSingleImageBytes_{};
    std::uint64_t uploads_{};
    std::uint64_t decodeFailures_{};
    std::uint64_t oversizeRejections_{};
    std::uint64_t negativeHits_{};
};

} // namespace uirepo

#endif
