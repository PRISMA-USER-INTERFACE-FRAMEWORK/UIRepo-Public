#include "uirepo/Core.h"
#include "uirepo/D3D11Renderer.h"
#include "../src/D3D11ImageCache.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace uirepo;

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct DeviceFixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> targetTexture;
    ComPtr<ID3D11RenderTargetView> targetView;
};

struct TemporaryBmp {
    std::string path{"uirepo-image-test.bmp"};

    TemporaryBmp() {
        constexpr std::array<std::uint8_t, 70> bytes{
            0x42, 0x4D, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
            0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
            0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
        };

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        require(stream.good(), "could not create temporary BMP fixture");
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        require(stream.good(), "could not write temporary BMP fixture");
    }

    ~TemporaryBmp() {
        std::remove(path.c_str());
    }
};

DeviceFixture createFixture() {
    DeviceFixture fixture;

    constexpr D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL actual{};

    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        0,
        requested,
        static_cast<UINT>(std::size(requested)),
        D3D11_SDK_VERSION,
        fixture.device.GetAddressOf(),
        &actual,
        fixture.context.GetAddressOf());
    require(SUCCEEDED(result), "D3D11CreateDevice(WARP) failed");

    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = 128;
    textureDescription.Height = 128;
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_DEFAULT;
    textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET;

    result = fixture.device->CreateTexture2D(
        &textureDescription, nullptr, fixture.targetTexture.GetAddressOf());
    require(SUCCEEDED(result), "CreateTexture2D target failed");

    result = fixture.device->CreateRenderTargetView(
        fixture.targetTexture.Get(), nullptr, fixture.targetView.GetAddressOf());
    require(SUCCEEDED(result), "CreateRenderTargetView failed");

    return fixture;
}

std::unique_ptr<Document> createImageDocument(
    D3D11Renderer& renderer,
    const std::string& source) {
    auto root = std::make_unique<PanelNode>("image-root");
    root->style().alignItems = Align::Start;
    auto& image = root->emplaceChild<ImageNode>(
        "image", source, Size{32.0f, 32.0f});
    image.style().width = Length::Pixels(32.0f);
    image.style().height = Length::Pixels(32.0f);
    auto document = std::make_unique<Document>(std::move(root), &renderer.textMeasurer());
    document->layout({128.0f, 128.0f});
    return document;
}

void testWicImageDecodeUploadAndReuse() {
    TemporaryBmp imageFile;
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    auto document = createImageDocument(renderer, imageFile.path);

    const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
    fixture.context->ClearRenderTargetView(fixture.targetView.Get(), clear);

    D3D11RenderStats first{};
    require(renderer.render(document->buildDisplayList(), &first), "first WIC image render failed");
    require(first.imageCommands == 1, "retained ImageNode did not emit one image command");
    require(first.imageCacheMisses >= 1, "first WIC image render did not miss image cache");
    require(first.imageCacheUploads == 1, "first WIC image render did not upload exactly once");
    require(first.imageCacheEntries == 1, "decoded image did not become cache resident");
    require(first.imageCacheBytes == 16, "2x2 RGBA image accounting is wrong");
    require(first.imageDecodeFailures == 0, "valid BMP reported a decode failure");
    require(first.drawCalls >= 1, "decoded image did not execute a D3D draw");

    D3D11_TEXTURE2D_DESC stagingDescription{};
    fixture.targetTexture->GetDesc(&stagingDescription);
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(
            &stagingDescription, nullptr, staging.GetAddressOf())),
        "CreateTexture2D image staging failed");
    fixture.context->CopyResource(staging.Get(), fixture.targetTexture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(
        SUCCEEDED(fixture.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "Map image staging texture failed");
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    const std::size_t sampleOffset = static_cast<std::size_t>(16) * mapped.RowPitch + 16u * 4u;
    const std::uint8_t red = pixels[sampleOffset + 0];
    const std::uint8_t green = pixels[sampleOffset + 1];
    const std::uint8_t blue = pixels[sampleOffset + 2];
    fixture.context->Unmap(staging.Get(), 0);
    require(red < 30 && green > 220 && blue < 30, "WIC image pixel output is incorrect");

    D3D11RenderStats second{};
    require(renderer.render(document->buildDisplayList(), &second), "second WIC image render failed");
    require(second.imageCacheHits >= 1, "second WIC image render did not reuse cached texture");
    require(second.imageCacheUploads == 0, "second WIC image render repeated GPU upload");
    require(second.imageCacheBytes == first.imageCacheBytes, "image cache hit changed live bytes");
}

void testRepeatedImageDocumentChurnStabilizesCache() {
    TemporaryBmp imageFile;
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});

    std::uint32_t baselineEntries = 0;
    std::size_t baselineBytes = 0;

    for (std::uint32_t iteration = 0; iteration < 32u; ++iteration) {
        auto document = createImageDocument(renderer, imageFile.path);
        D3D11RenderStats stats{};
        require(renderer.render(document->buildDisplayList(), &stats), "image document-churn render failed");
        require(stats.imageCommands == 1, "image document churn skipped retained image command");

        if (iteration == 0u) {
            require(stats.imageCacheMisses >= 1, "first image document-churn render did not miss");
            require(stats.imageCacheUploads == 1, "first image document-churn render did not upload exactly once");
            baselineEntries = stats.imageCacheEntries;
            baselineBytes = stats.imageCacheBytes;
            require(baselineEntries == 1u && baselineBytes == 16u, "image document churn baseline accounting is wrong");
            continue;
        }

        require(stats.imageCacheHits >= 1, "recreated image document did not reuse cached texture");
        require(stats.imageCacheUploads == 0, "recreated image document caused a repeated GPU upload");
        require(stats.imageCacheEntries == baselineEntries, "image document churn grew cache entry count");
        require(stats.imageCacheBytes == baselineBytes, "image document churn grew cache byte residency");
        require(stats.imageDecodeFailures == 0, "image document churn repeated or introduced decode failure");
    }
}

void testMissingImageUsesNegativeCache() {
    DeviceFixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.targetView.Get(), {128.0f, 128.0f});
    constexpr const char* missing = "uirepo-image-file-that-does-not-exist.png";

    {
        auto document = createImageDocument(renderer, missing);
        D3D11RenderStats first{};
        require(renderer.render(document->buildDisplayList(), &first), "missing image made renderer fail the frame");
        require(first.imageCommands == 1, "missing retained image command was not observed");
        require(first.imageDecodeFailures == 1, "missing image did not record exactly one decode failure");
        require(first.imageCacheUploads == 0 && first.imageCacheEntries == 0, "missing image created a GPU resource");
    }

    auto recreated = createImageDocument(renderer, missing);
    D3D11RenderStats second{};
    require(renderer.render(recreated->buildDisplayList(), &second), "negative-cached recreated image made renderer fail");
    require(second.imageNegativeHits == 1, "recreated missing-image document did not hit negative cache");
    require(second.imageDecodeFailures == 0, "document recreation repeated missing-image WIC decode work");
    require(second.imageCacheUploads == 0, "negative cache unexpectedly uploaded a texture");
}

} // namespace

struct SizedBmp {
    std::string path;

    SizedBmp(std::string p, std::uint32_t width, std::uint32_t height) : path(std::move(p)) {
        const std::uint32_t rowPadded = ((width * 3u + 3u) / 4u) * 4u;
        const std::uint32_t dataSize = rowPadded * height;
        const std::uint32_t fileSize = 54u + dataSize;
        std::vector<std::uint8_t> buf(fileSize, 0u);
        const auto put32 = [&](std::size_t off, std::uint32_t v) {
            buf[off] = static_cast<std::uint8_t>(v & 0xFFu);
            buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
            buf[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
            buf[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
        };
        buf[0] = 'B';
        buf[1] = 'M';
        put32(2, fileSize);
        put32(10, 54);
        put32(14, 40);
        put32(18, width);
        put32(22, height);
        buf[26] = 1;
        buf[28] = 24;
        put32(34, dataSize);
        for (std::uint32_t y = 0; y < height; ++y) {
            std::uint8_t* row = &buf[54u + static_cast<std::size_t>(y) * rowPadded];
            for (std::uint32_t x = 0; x < width; ++x) {
                row[x * 3u + 0] = 40;
                row[x * 3u + 1] = 40;
                row[x * 3u + 2] = 200;
            }
        }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        require(stream.good(), "could not create sized BMP fixture");
        stream.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(fileSize));
        require(stream.good(), "could not write sized BMP fixture");
    }

    ~SizedBmp() {
        std::remove(path.c_str());
    }
};

void testOversizeImageRejectedBeforeUpload() {
    SizedBmp big("uirepo-oversize-test.bmp", 64, 64);
    DeviceFixture fixture = createFixture();
    D3D11ImageCache cache(
        fixture.device.Get(), D3D11ImageCache::DefaultBytes, D3D11ImageCache::DefaultEntries, 4096u);
    const auto result = cache.resolve(big.path);
    require(!result.success, "an image over the single-resource budget must not resolve to a texture");
    require(cache.oversizeRejections() == 1, "oversize image must be rejected before GPU upload");
    require(cache.uploads() == 0, "oversize image must not allocate a GPU texture");
}

void testBeforeUploadHookGatesUpload() {
    SizedBmp img("uirepo-preupload-test.bmp", 32, 32);
    DeviceFixture fixture = createFixture();
    D3D11ImageCache cache(fixture.device.Get());

    struct HookState {
        std::size_t bytes{};
        int calls{};
        bool proceed{};
    } state;
    const auto hook = [](std::size_t bytes, void* context) noexcept -> bool {
        auto* s = static_cast<HookState*>(context);
        s->bytes = bytes;
        ++s->calls;
        return s->proceed;
    };

    const auto aborted = cache.resolve(img.path, +hook, &state);
    require(!aborted.success, "beforeUpload returning false must abort the resolve");
    require(state.calls == 1, "beforeUpload must fire exactly once on a cache miss");
    require(state.bytes == 32u * 32u * 4u, "beforeUpload must receive the RGBA8 byte cost before GPU upload");
    require(cache.uploads() == 0, "aborting in beforeUpload must prevent the GPU allocation");

    state.proceed = true;
    const auto ok = cache.resolve(img.path, +hook, &state);
    require(ok.success, "beforeUpload returning true must allow the upload");
    require(state.calls == 2, "a budget abort must not poison the negative cache — the retry re-runs the hook");
    require(cache.uploads() == 1, "proceeding through beforeUpload must upload exactly once");
}

int main() {
    testWicImageDecodeUploadAndReuse();
    testRepeatedImageDocumentChurnStabilizesCache();
    testMissingImageUsesNegativeCache();
    testOversizeImageRejectedBeforeUpload();
    testBeforeUploadHookGatesUpload();
    std::cout << "UIRepo D3D11 image regressions passed\n";
    return 0;
}
