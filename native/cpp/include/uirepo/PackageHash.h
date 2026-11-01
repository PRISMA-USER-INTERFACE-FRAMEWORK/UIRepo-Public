#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace uirepo::pkg {

using Digest = std::array<std::uint8_t, 32>;

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t previous = 0) noexcept;

class Sha256 final {
public:
    Sha256() noexcept;

    void update(std::span<const std::uint8_t> data) noexcept;
    [[nodiscard]] Digest finish() noexcept;

private:
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t totalBytes_{};
    std::size_t buffered_{};
};

[[nodiscard]] Digest sha256(std::span<const std::uint8_t> data) noexcept;

}  // namespace uirepo::pkg
