#include "D3D11TextLayout.h"

#ifdef _WIN32

#include "uirepo/D3D11Renderer.h"

#include <Windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <utility>

namespace uirepo::detail {

DWRITE_WORD_WRAPPING wordWrapping(TextWrapping wrapping) noexcept {
    return wrapping == TextWrapping::Wrap
        ? DWRITE_WORD_WRAPPING_WRAP
        : DWRITE_WORD_WRAPPING_NO_WRAP;
}

DWRITE_TEXT_ALIGNMENT textAlignment(TextAlignment alignment) noexcept {
    switch (alignment) {
    case TextAlignment::Center:
        return DWRITE_TEXT_ALIGNMENT_CENTER;
    case TextAlignment::End:
        return DWRITE_TEXT_ALIGNMENT_TRAILING;
    case TextAlignment::Start:
    default:
        return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

bool configureTextLayout(
    IDWriteFactory* factory,
    IDWriteTextFormat* format,
    IDWriteTextLayout* layout,
    TextLayoutOptions options) noexcept {
    if (factory == nullptr || format == nullptr || layout == nullptr) {
        return false;
    }

    const auto wrapping = wordWrapping(options.wrapping);
    if (FAILED(format->SetWordWrapping(wrapping)) ||
        FAILED(layout->SetWordWrapping(wrapping)) ||
        FAILED(layout->SetTextAlignment(textAlignment(options.alignment)))) {
        return false;
    }

    if (options.overflow == TextOverflow::Ellipsis) {
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;

        Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
        if (FAILED(factory->CreateEllipsisTrimmingSign(format, ellipsis.GetAddressOf())) ||
            FAILED(layout->SetTrimming(&trimming, ellipsis.Get()))) {
            return false;
        }
    } else {
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_NONE;
        if (FAILED(layout->SetTrimming(&trimming, nullptr))) {
            return false;
        }
    }

    return true;
}

} // namespace uirepo::detail

namespace uirepo {

Size DirectWriteTextMeasurer::measure(
    std::string_view text,
    float fontSize,
    float maxWidth,
    TextWrapping wrapping) const noexcept {
    if (!factory_ || !std::isfinite(fontSize) || fontSize <= 0.0f || std::isnan(maxWidth)) {
        return Size::Zero();
    }

    try {
        const std::wstring wide = widen(text);
        if (wide.empty() && !text.empty()) {
            return Size::Zero();
        }

        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        if (FAILED(factory_->CreateTextFormat(
                fontFamily_.c_str(),
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                fontSize,
                L"",
                format.GetAddressOf()))) {
            return Size::Zero();
        }

        const float layoutWidth = wrapping == TextWrapping::Wrap && std::isfinite(maxWidth)
            ? std::max(1.0f, maxWidth)
            : 100000.0f;
        if (FAILED(format->SetWordWrapping(detail::wordWrapping(wrapping)))) {
            return Size::Zero();
        }

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(factory_->CreateTextLayout(
                wide.c_str(),
                static_cast<UINT32>(wide.size()),
                format.Get(),
                layoutWidth,
                100000.0f,
                layout.GetAddressOf()))) {
            return Size::Zero();
        }
        if (FAILED(layout->SetWordWrapping(detail::wordWrapping(wrapping)))) {
            return Size::Zero();
        }

        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics))) {
            return Size::Zero();
        }

        return {
            std::max(0.0f, metrics.widthIncludingTrailingWhitespace),
            std::max(0.0f, metrics.height),
        };
    } catch (...) {
        return Size::Zero();
    }
}

namespace {
constexpr std::size_t kTextLayoutFixedWeight = 2048u;

void hashCombine(std::size_t& seed, std::size_t value) noexcept {
    seed ^= value + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) + (seed << 6u) + (seed >> 2u);
}
} // namespace

std::size_t TextLayoutKeyHash::operator()(const TextLayoutKey& key) const noexcept {
    std::size_t seed = std::hash<std::string>{}(key.text);
    hashCombine(seed, key.fontSizeBits);
    hashCombine(seed, key.widthBits);
    hashCombine(seed, key.heightBits);
    hashCombine(seed, static_cast<std::size_t>(key.wrapping));
    hashCombine(seed, static_cast<std::size_t>(key.alignment));
    hashCombine(seed, static_cast<std::size_t>(key.overflow));
    return seed;
}

D3D11TextLayoutCache::D3D11TextLayoutCache(
    IDWriteFactory* factory,
    std::wstring fontFamily,
    std::size_t maxBytes,
    std::size_t maxEntries)
    : factory_(factory),
      fontFamily_(std::move(fontFamily)),
      cache_(maxBytes, maxEntries) {}

Microsoft::WRL::ComPtr<IDWriteTextLayout> D3D11TextLayoutCache::acquire(
    std::wstring_view wideText,
    const DrawTextCommand& command) noexcept {
    if (!factory_) {
        return nullptr;
    }

    try {
        const float layoutWidth = std::max(1.0f, command.bounds.width);
        const float layoutHeight = std::max(1.0f, command.bounds.height);

        TextLayoutKey key{
            .text = std::string(command.text),
            .fontSizeBits = std::bit_cast<std::uint32_t>(command.fontSize),
            .widthBits = std::bit_cast<std::uint32_t>(layoutWidth),
            .heightBits = std::bit_cast<std::uint32_t>(layoutHeight),
            .wrapping = command.layout.wrapping,
            .alignment = command.layout.alignment,
            .overflow = command.layout.overflow,
        };

        if (auto* cached = cache_.find(key); cached != nullptr) {
            return cached->layout;
        }

        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        if (FAILED(factory_->CreateTextFormat(
                fontFamily_.c_str(),
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                command.fontSize,
                L"",
                format.GetAddressOf()))) {
            return nullptr;
        }

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(factory_->CreateTextLayout(
                wideText.data(),
                static_cast<UINT32>(wideText.size()),
                format.Get(),
                layoutWidth,
                layoutHeight,
                layout.GetAddressOf()))) {
            return nullptr;
        }

        if (!detail::configureTextLayout(factory_.Get(), format.Get(), layout.Get(), command.layout)) {
            return nullptr;
        }

        const std::size_t bytes = sizeof(TextLayoutKey) + key.text.size() + kTextLayoutFixedWeight;
        (void)cache_.insert(std::move(key), CachedTextLayout{layout}, bytes);
        return layout;
    } catch (...) {
        return nullptr;
    }
}

} // namespace uirepo

#endif
