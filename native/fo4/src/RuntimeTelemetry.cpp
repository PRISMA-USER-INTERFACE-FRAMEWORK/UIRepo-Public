#include "Runtime.h"

#include "DocumentRegistry.h"
#include "../../cpp/src/D3D11ImageCache.h"
#include "../../cpp/src/D3D11TextLayout.h"

#include <spdlog/spdlog.h>

#include <limits>

namespace uirepo {

struct D3D11RendererTelemetryAccess final {
    struct Snapshot {
        render::CacheStats glyph{};
        std::uint64_t glyphUploads{};
        render::CacheStats image{};
        std::uint64_t imageUploads{};
        std::uint64_t imageDecodeFailures{};
        std::uint64_t imageNegativeHits{};
        render::CacheStats textLayout{};
    };

    [[nodiscard]] static Snapshot snapshot(const D3D11Renderer& renderer) noexcept {
        Snapshot result{};
        result.glyph = renderer.glyphCache_.stats();
        result.glyphUploads = renderer.glyphUploads_;
        if (renderer.imageCache_) {
            result.image = renderer.imageCache_->stats();
            result.imageUploads = renderer.imageCache_->uploads();
            result.imageDecodeFailures = renderer.imageCache_->decodeFailures();
            result.imageNegativeHits = renderer.imageCache_->negativeHits();
        }
        if (renderer.textLayoutCache_) {
            result.textLayout = renderer.textLayoutCache_->stats();
        }
        return result;
    }
};

} // namespace uirepo

namespace uirepo::fo4 {
namespace {

std::uint64_t nextRendererTelemetryEpoch() noexcept {
    static std::uint64_t next{};
    if (next != std::numeric_limits<std::uint64_t>::max()) {
        ++next;
    }
    return next;
}

} // namespace

void Runtime::maybeLogResourceSnapshot() noexcept {
    if (!renderer_) {
        return;
    }

    telemetryShutdownSnapshotLogged_ = false;
    const auto now = telemetry::PeriodicGate::Clock::now();
    if (telemetryGeneration_ == 0 || telemetryRendererIdentity_ != renderer_.get()) {
        telemetryRendererIdentity_ = renderer_.get();
        telemetryGeneration_ = nextRendererTelemetryEpoch();
        telemetryGate_.reset(now);
        logResourceSnapshot("baseline");
        return;
    }

    if (telemetryGate_.due(now)) {
        logResourceSnapshot("periodic");
    }
}

void Runtime::logResourceSnapshot(const char* reason) noexcept {
    if (!renderer_) {
        return;
    }

    try {
        if (telemetryGeneration_ == 0 || telemetryRendererIdentity_ != renderer_.get()) {
            telemetryRendererIdentity_ = renderer_.get();
            telemetryGeneration_ = nextRendererTelemetryEpoch();
        }

        const auto rendererStats = D3D11RendererTelemetryAccess::snapshot(*renderer_);
        const auto surfaceStats = surfaceManager_ ? surfaceManager_->stats() : D3D11SurfaceStats{};
        const auto visibleDocuments = DocumentRegistry::instance().visibleCount();

        spdlog::info(
            "UIRepo_F4 telemetry: reason {} renderer-epoch {} target-generation {} target {}x{} visible-docs {} "
            "glyph(entries {} bytes {} hits {} misses {} uploads {} evictions {} rejections {}) "
            "image(entries {} bytes {} hits {} misses {} uploads {} evictions {} rejections {} decode-failures {} negative-hits {}) "
            "text-layout(entries {} bytes {} hits {} misses {} evictions {} rejections {}) "
            "surfaces(live {} live-bytes {} pooled {} pooled-bytes {} allocations {} reuses {} resizes {} rebuilds {} rejections {} drains {})",
            reason != nullptr ? reason : "unknown",
            telemetryGeneration_,
            rendererGeneration_,
            static_cast<std::uint32_t>(targetSize_.width > 0.0f ? targetSize_.width : 0.0f),
            static_cast<std::uint32_t>(targetSize_.height > 0.0f ? targetSize_.height : 0.0f),
            visibleDocuments,
            rendererStats.glyph.entries,
            rendererStats.glyph.bytes,
            rendererStats.glyph.hits,
            rendererStats.glyph.misses,
            rendererStats.glyphUploads,
            rendererStats.glyph.evictions,
            rendererStats.glyph.rejections,
            rendererStats.image.entries,
            rendererStats.image.bytes,
            rendererStats.image.hits,
            rendererStats.image.misses,
            rendererStats.imageUploads,
            rendererStats.image.evictions,
            rendererStats.image.rejections,
            rendererStats.imageDecodeFailures,
            rendererStats.imageNegativeHits,
            rendererStats.textLayout.entries,
            rendererStats.textLayout.bytes,
            rendererStats.textLayout.hits,
            rendererStats.textLayout.misses,
            rendererStats.textLayout.evictions,
            rendererStats.textLayout.rejections,
            surfaceStats.liveCount,
            surfaceStats.liveBytes,
            surfaceStats.pooledCount,
            surfaceStats.pooledBytes,
            surfaceStats.allocations,
            surfaceStats.reuses,
            surfaceStats.resizes,
            surfaceStats.deviceRebuilds,
            surfaceStats.rejections,
            surfaceStats.poolDrains);
    } catch (...) {
        // Telemetry must never affect rendering, shutdown, or recovery.
    }
}

} // namespace uirepo::fo4
