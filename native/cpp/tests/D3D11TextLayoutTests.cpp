#include "uirepo/Core.h"
#include "uirepo/D3D11Renderer.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace uirepo;

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
};

Fixture createFixture(UINT width = 256, UINT height = 128) {
    Fixture fixture;
    constexpr D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL actual{};
    require(
        SUCCEEDED(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            requested,
            static_cast<UINT>(std::size(requested)),
            D3D11_SDK_VERSION,
            fixture.device.GetAddressOf(),
            &actual,
            fixture.context.GetAddressOf())),
        "could not create WARP device");

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(
            &description, nullptr, fixture.texture.GetAddressOf())),
        "could not create text-layout target texture");
    require(
        SUCCEEDED(fixture.device->CreateRenderTargetView(
            fixture.texture.Get(), nullptr, fixture.target.GetAddressOf())),
        "could not create text-layout target view");
    return fixture;
}

bool targetHasInk(const Fixture& fixture) {
    D3D11_TEXTURE2D_DESC description{};
    fixture.texture->GetDesc(&description);

    D3D11_TEXTURE2D_DESC stagingDescription = description;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> staging;
    require(
        SUCCEEDED(fixture.device->CreateTexture2D(
            &stagingDescription, nullptr, staging.GetAddressOf())),
        "could not create text-layout staging texture");

    fixture.context->CopyResource(staging.Get(), fixture.texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(
        SUCCEEDED(fixture.context->Map(
            staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
        "could not map text-layout staging texture");

    bool hasInk = false;
    for (UINT y = 0; y < description.Height && !hasInk; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                          static_cast<std::size_t>(y) * mapped.RowPitch;
        for (UINT x = 0; x < description.Width * 4u; ++x) {
            if (row[x] != 0) {
                hasInk = true;
                break;
            }
        }
    }

    fixture.context->Unmap(staging.Get(), 0);
    return hasInk;
}

void testWidthAwareDirectWriteMeasurement() {
    DirectWriteTextMeasurer measurer;
    const std::string text = "Fallout native retained text wraps across several words";
    const Size noWrap = measurer.measure(text, 20.0f, 80.0f, TextWrapping::NoWrap);
    const Size wrapped = measurer.measure(text, 20.0f, 80.0f, TextWrapping::Wrap);

    require(noWrap.width > 80.0f, "NoWrap measurement unexpectedly honored wrap width");
    require(wrapped.width <= 80.5f, "wrapped DirectWrite width exceeded requested measure width");
    require(wrapped.height > noWrap.height, "wrapped DirectWrite text did not gain line height");

    const Size singleLine = measurer.measure("Line one", 18.0f, 200.0f, TextWrapping::NoWrap);
    const Size explicitLines = measurer.measure(
        "Line one\nLine two",
        18.0f,
        200.0f,
        TextWrapping::NoWrap);
    require(singleLine.width > 0.0f && singleLine.height > 0.0f, "single-line DirectWrite measurement failed");
    require(explicitLines.height > singleLine.height, "explicit newline did not produce multiline DirectWrite height");

    const Size cyrillic = measurer.measure("Настройки сохранены", 18.0f, 240.0f, TextWrapping::Wrap);
    const Size cjk = measurer.measure("設定を保存しました", 18.0f, 240.0f, TextWrapping::Wrap);
    const Size arabic = measurer.measure("الإعدادات محفوظة", 18.0f, 240.0f, TextWrapping::Wrap);
    const Size combining = measurer.measure("Cafe\xCC\x81", 18.0f, 240.0f, TextWrapping::Wrap);
    require(cyrillic.width > 0.0f && cyrillic.height > 0.0f, "Cyrillic DirectWrite measurement failed");
    require(cjk.width > 0.0f && cjk.height > 0.0f, "CJK DirectWrite measurement failed");
    require(arabic.width > 0.0f && arabic.height > 0.0f, "Arabic DirectWrite measurement failed");
    require(combining.width > 0.0f && combining.height > 0.0f, "combining-mark DirectWrite measurement failed");

    const Size invalid = measurer.measure(
        std::string("\xF0\x28\x8C\x28", 4),
        20.0f,
        80.0f,
        TextWrapping::Wrap);
    require(invalid.width == 0.0f && invalid.height == 0.0f, "invalid UTF-8 width measurement did not fail safely");
}

void testRetainedWrapAlignmentAndEllipsisRender() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.target.Get(), {256.0f, 128.0f});

    auto root = std::make_unique<StackNode>("root", Axis::Column);
    root->style().alignItems = Align::Start;

    auto& wrapped = root->emplaceChild<TextNode>(
        "wrapped",
        "A production menu needs wrapped localized text that stays inside its retained bounds.");
    wrapped.style().width = Length::Pixels(110.0f);
    wrapped.style().height = Length::Pixels(45.0f);
    wrapped.setFontSize(18.0f);
    wrapped.setTextLayout(TextLayoutOptions{
        .wrapping = TextWrapping::Wrap,
        .alignment = TextAlignment::Center,
        .overflow = TextOverflow::Ellipsis,
    });

    auto& trailing = root->emplaceChild<TextNode>("trailing", "Trailing");
    trailing.style().width = Length::Pixels(110.0f);
    trailing.style().height = Length::Pixels(24.0f);
    trailing.setFontSize(16.0f);
    trailing.setTextLayout(TextLayoutOptions{
        .wrapping = TextWrapping::NoWrap,
        .alignment = TextAlignment::End,
        .overflow = TextOverflow::Clip,
    });

    Document document(std::move(root), &renderer.textMeasurer());
    document.layout({256.0f, 128.0f});

    D3D11RenderStats stats{};
    require(renderer.render(document.buildDisplayList(), &stats), "text-layout renderer rejected retained policy commands");
    require(stats.textCommands == 2, "text-layout renderer did not execute both text commands");
    require(stats.glyphRuns >= 2, "text-layout renderer did not shape both retained text nodes");
    require(stats.drawCalls >= 2, "text-layout renderer telemetry missed text draws");
}

void testMultilingualShapingProducesPixels() {
    Fixture fixture = createFixture(512, 256);
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.target.Get(), {512.0f, 256.0f});

    constexpr float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    fixture.context->ClearRenderTargetView(fixture.target.Get(), clear);

    auto root = std::make_unique<StackNode>("root", Axis::Column);
    root->style().alignItems = Align::Start;
    root->style().gap = 4.0f;

    const char* samples[] = {
        "Settings saved",
        "Настройки сохранены",
        "設定を保存しました",
        "الإعدادات محفوظة",
        "Cafe\xCC\x81",
    };

    for (std::size_t index = 0; index < std::size(samples); ++index) {
        auto& text = root->emplaceChild<TextNode>(
            "localized-" + std::to_string(index),
            samples[index]);
        text.style().width = Length::Pixels(460.0f);
        text.style().height = Length::Pixels(36.0f);
        text.setFontSize(20.0f);
        text.setColor(Color{240, 240, 240, 255});
        text.setTextLayout(TextLayoutOptions{
            .wrapping = TextWrapping::Wrap,
            .alignment = TextAlignment::Start,
            .overflow = TextOverflow::Clip,
        });
    }

    Document document(std::move(root), &renderer.textMeasurer());
    document.layout({512.0f, 256.0f});

    D3D11RenderStats stats{};
    require(renderer.render(document.buildDisplayList(), &stats), "multilingual DirectWrite render failed");
    require(stats.textCommands == std::size(samples), "multilingual renderer skipped a retained text command");
    require(stats.glyphRuns >= std::size(samples), "multilingual renderer did not shape every sample");
    require(stats.drawCalls >= std::size(samples), "multilingual renderer did not issue expected draws");
    require(targetHasInk(fixture), "multilingual DirectWrite render produced no output pixels");
}

DrawTextCommand textCmd(
    std::string_view text,
    Rect bounds,
    float fontSize,
    Color color,
    TextLayoutOptions options) {
    DrawTextCommand command{};
    command.bounds = bounds;
    command.opacity = 1.0f;
    command.zIndex = 0;
    command.text = text;
    command.fontSize = fontSize;
    command.color = color;
    command.layout = options;
    return command;
}

D3D11RenderStats renderOne(D3D11Renderer& renderer, const DrawTextCommand& command) {
    std::vector<DrawCommand> commands{command};
    D3D11RenderStats stats{};
    require(renderer.render(commands, &stats), "text-layout cache render failed");
    return stats;
}

D3D11Renderer makeTextRenderer(Fixture& fixture, std::size_t cacheBytes, std::size_t cacheEntries) {
    return D3D11Renderer(
        fixture.device.Get(),
        fixture.context.Get(),
        D3D11Renderer::DefaultGlyphCacheBytes,
        D3D11Renderer::DefaultGlyphCacheEntries,
        D3D11Renderer::DefaultImageCacheBytes,
        D3D11Renderer::DefaultImageCacheEntries,
        D3D11Renderer::DefaultLiveImageBudgetBytes,
        D3D11Renderer::DefaultSingleImageBudgetBytes,
        cacheBytes,
        cacheEntries);
}

void testDrawLayoutCacheHitMissMatrix() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer(fixture.device.Get(), fixture.context.Get());
    renderer.setTarget(fixture.target.Get(), {256.0f, 128.0f});

    const std::string text = "Cache this label";
    const std::string other = "A different label";
    const Rect bounds{4.0f, 4.0f, 160.0f, 28.0f};
    const Color white{255, 255, 255, 255};
    const TextLayoutOptions opts{TextWrapping::NoWrap, TextAlignment::Start, TextOverflow::Clip};
    const DrawTextCommand base = textCmd(text, bounds, 18.0f, white, opts);

    const D3D11RenderStats first = renderOne(renderer, base);
    require(
        first.textLayoutCacheMisses == 1 && first.textLayoutCacheHits == 0,
        "first text render must miss the layout cache");
    require(
        first.textCommands == 1 && first.glyphRuns >= 1 && first.drawCalls >= 1,
        "first text render must still shape and draw");

    const D3D11RenderStats second = renderOne(renderer, base);
    require(
        second.textLayoutCacheHits == 1 && second.textLayoutCacheMisses == 0,
        "identical text render must hit the layout cache");
    require(
        second.glyphRuns >= 1 && second.drawCalls >= 1,
        "a cache hit must still draw the cached layout, not skip it");

    DrawTextCommand moved = base;
    moved.bounds = Rect{60.0f, 12.0f, 160.0f, 28.0f};
    require(
        renderOne(renderer, moved).textLayoutCacheHits == 1,
        "moving text (x/y only, same width/height) must reuse the cached layout");

    DrawTextCommand recolored = base;
    recolored.color = Color{255, 0, 0, 255};
    recolored.opacity = 0.5f;
    require(
        renderOne(renderer, recolored).textLayoutCacheHits == 1,
        "recoloring / changing opacity must reuse the cached layout");

    DrawTextCommand retexted = base;
    retexted.text = other;
    require(renderOne(renderer, retexted).textLayoutCacheMisses == 1, "changed text must miss");

    DrawTextCommand resized = base;
    resized.fontSize = 22.0f;
    require(renderOne(renderer, resized).textLayoutCacheMisses == 1, "changed font size must miss");

    DrawTextCommand widened = base;
    widened.bounds = Rect{4.0f, 4.0f, 90.0f, 28.0f};
    require(renderOne(renderer, widened).textLayoutCacheMisses == 1, "changed width must miss");

    DrawTextCommand taller = base;
    taller.bounds = Rect{4.0f, 4.0f, 160.0f, 44.0f};
    require(renderOne(renderer, taller).textLayoutCacheMisses == 1, "changed height must miss");

    DrawTextCommand wrapped = base;
    wrapped.layout = TextLayoutOptions{TextWrapping::Wrap, TextAlignment::Start, TextOverflow::Clip};
    require(renderOne(renderer, wrapped).textLayoutCacheMisses == 1, "changed wrapping must miss");

    DrawTextCommand centered = base;
    centered.layout = TextLayoutOptions{TextWrapping::NoWrap, TextAlignment::Center, TextOverflow::Clip};
    require(renderOne(renderer, centered).textLayoutCacheMisses == 1, "changed alignment must miss");

    DrawTextCommand ellipsis = base;
    ellipsis.layout = TextLayoutOptions{TextWrapping::NoWrap, TextAlignment::Start, TextOverflow::Ellipsis};
    require(renderOne(renderer, ellipsis).textLayoutCacheMisses == 1, "changed overflow must miss");
}

void testDrawLayoutCacheEvicts() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer = makeTextRenderer(fixture, D3D11Renderer::DefaultTextLayoutCacheBytes, 1);
    renderer.setTarget(fixture.target.Get(), {256.0f, 128.0f});

    const std::string alpha = "Alpha";
    const std::string beta = "Beta";
    const Rect bounds{4.0f, 4.0f, 120.0f, 24.0f};
    const Color white{255, 255, 255, 255};
    const TextLayoutOptions opts{TextWrapping::NoWrap, TextAlignment::Start, TextOverflow::Clip};

    require(
        renderOne(renderer, textCmd(alpha, bounds, 18.0f, white, opts)).textLayoutCacheMisses == 1,
        "first entry must miss");
    const D3D11RenderStats betaStats = renderOne(renderer, textCmd(beta, bounds, 18.0f, white, opts));
    require(
        betaStats.textLayoutCacheMisses == 1 && betaStats.textLayoutCacheEvictions == 1,
        "a second entry in a one-entry cache must miss and evict the first");
    require(
        renderOne(renderer, textCmd(alpha, bounds, 18.0f, white, opts)).textLayoutCacheMisses == 1,
        "the evicted entry must miss again, not hit");
}

void testDrawLayoutCacheRejectionStillRenders() {
    Fixture fixture = createFixture();
    D3D11Renderer renderer = makeTextRenderer(fixture, 8u, 512u);
    renderer.setTarget(fixture.target.Get(), {256.0f, 128.0f});
    constexpr float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    fixture.context->ClearRenderTargetView(fixture.target.Get(), clear);

    const std::string text = "Uncached but still visible";
    const TextLayoutOptions opts{TextWrapping::NoWrap, TextAlignment::Start, TextOverflow::Clip};

    const D3D11RenderStats stats =
        renderOne(renderer, textCmd(text, Rect{4.0f, 4.0f, 220.0f, 28.0f}, 20.0f, Color{255, 255, 255, 255}, opts));
    require(stats.textLayoutCacheRejections >= 1, "an entry heavier than the whole budget must be rejected");
    require(stats.textLayoutCacheEntries == 0, "a rejected layout must not become resident");
    require(stats.glyphRuns >= 1 && stats.drawCalls >= 1, "rejection must degrade to uncached rendering");
    require(targetHasInk(fixture), "rejected-but-rendered text must still produce pixels");

    const D3D11RenderStats again =
        renderOne(renderer, textCmd(text, Rect{4.0f, 4.0f, 220.0f, 28.0f}, 20.0f, Color{255, 255, 255, 255}, opts));
    require(
        again.textLayoutCacheHits == 0 && again.textLayoutCacheRejections >= 1,
        "a persistently rejected layout must never become a cache hit");
}

} // namespace

int main() {
    testWidthAwareDirectWriteMeasurement();
    testRetainedWrapAlignmentAndEllipsisRender();
    testMultilingualShapingProducesPixels();
    testDrawLayoutCacheHitMissMatrix();
    testDrawLayoutCacheEvicts();
    testDrawLayoutCacheRejectionStillRenders();
    std::cout << "UIRepo DirectWrite text layout regressions passed\n";
    return 0;
}
