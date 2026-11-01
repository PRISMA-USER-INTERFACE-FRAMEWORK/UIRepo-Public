#pragma once

#ifdef _WIN32

#include "uirepo/BoundedCache.h"
#include "uirepo/Core.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace uirepo::detail {

[[nodiscard]] DWRITE_WORD_WRAPPING wordWrapping(TextWrapping wrapping) noexcept;
[[nodiscard]] DWRITE_TEXT_ALIGNMENT textAlignment(TextAlignment alignment) noexcept;

[[nodiscard]] bool configureTextLayout(
    IDWriteFactory* factory,
    IDWriteTextFormat* format,
    IDWriteTextLayout* layout,
    TextLayoutOptions options) noexcept;

} // namespace uirepo::detail

namespace uirepo {

struct TextLayoutKey {
    std::string text;
    std::uint32_t fontSizeBits{};
    std::uint32_t widthBits{};
    std::uint32_t heightBits{};
    TextWrapping wrapping{};
    TextAlignment alignment{};
    TextOverflow overflow{};

    friend bool operator==(const TextLayoutKey&, const TextLayoutKey&) noexcept = default;
};

struct TextLayoutKeyHash {
    [[nodiscard]] std::size_t operator()(const TextLayoutKey& key) const noexcept;
};

struct CachedTextLayout {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
};

class D3D11TextLayoutCache final {
public:
    static constexpr std::size_t DefaultBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t DefaultEntries = 512u;

    D3D11TextLayoutCache(
        IDWriteFactory* factory,
        std::wstring fontFamily,
        std::size_t maxBytes,
        std::size_t maxEntries);

    [[nodiscard]] Microsoft::WRL::ComPtr<IDWriteTextLayout> acquire(
        std::wstring_view wideText,
        const DrawTextCommand& command) noexcept;

    [[nodiscard]] const render::CacheStats& stats() const noexcept { return cache_.stats(); }

private:
    Microsoft::WRL::ComPtr<IDWriteFactory> factory_;
    std::wstring fontFamily_;
    render::BoundedLruCache<TextLayoutKey, CachedTextLayout, TextLayoutKeyHash> cache_;
};

} // namespace uirepo

#endif
