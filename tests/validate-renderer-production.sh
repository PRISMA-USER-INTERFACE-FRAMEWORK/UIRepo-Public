#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "renderer production validation failed: $*" >&2
    exit 1
}

# Bounded glyph/image resource policy.
test -f native/cpp/include/uirepo/BoundedCache.h || fail "bounded cache primitive is missing"
test -f native/cpp/include/uirepo/ImagePolicy.h || fail "image admission policy is missing"
test -f native/cpp/src/D3D11ImageCache.cpp || fail "WIC image cache implementation is missing"
test -f native/cpp/tests/D3D11ImageTests.cpp || fail "WIC image regression target source is missing"
grep -Fq 'DefaultGlyphCacheBytes = 16u * 1024u * 1024u' native/cpp/include/uirepo/D3D11Renderer.h || fail "glyph byte budget changed without contract update"
grep -Fq 'DefaultGlyphCacheEntries = 2048u' native/cpp/include/uirepo/D3D11Renderer.h || fail "glyph entry budget changed without contract update"
grep -Fq 'DefaultImageCacheBytes = 64u * 1024u * 1024u' native/cpp/include/uirepo/D3D11Renderer.h || fail "image byte budget changed without contract update"
grep -Fq 'DefaultImageCacheEntries = 256u' native/cpp/include/uirepo/D3D11Renderer.h || fail "image entry budget changed without contract update"
grep -Fq 'CreateDecoderFromFilename' native/cpp/src/D3D11ImageCache.cpp || fail "WIC file decode path is missing"
grep -Fq 'GUID_WICPixelFormat32bppRGBA' native/cpp/src/D3D11ImageCache.cpp || fail "WIC decode must normalize to RGBA8"
grep -Fq 'DXGI_FORMAT_R8G8B8A8_UNORM' native/cpp/src/D3D11ImageCache.cpp || fail "image upload format policy must remain explicit UNORM"
grep -Fq 'negativeCache_' native/cpp/src/D3D11ImageCache.h || fail "failed image decode negative cache is missing"
grep -Fq 'drawImage(value' native/cpp/src/D3D11Renderer.cpp || fail "DrawImageCommand is not executed"
grep -Fq 'testWicImageDecodeUploadAndReuse' native/cpp/tests/D3D11ImageTests.cpp || fail "WIC decode/upload regression is missing"
grep -Fq 'testMissingImageUsesNegativeCache' native/cpp/tests/D3D11ImageTests.cpp || fail "WIC negative-cache regression is missing"

# Production text layout policy.
test -f native/cpp/src/D3D11TextLayout.h || fail "DirectWrite text policy header is missing"
test -f native/cpp/src/D3D11TextLayout.cpp || fail "DirectWrite text policy implementation is missing"
test -f native/cpp/tests/D3D11TextLayoutTests.cpp || fail "DirectWrite text layout regression is missing"
grep -Fq 'TextWrapping' native/cpp/include/uirepo/Core.h || fail "retained text wrapping policy is missing"
grep -Fq 'TextAlignment' native/cpp/include/uirepo/Core.h || fail "retained text alignment policy is missing"
grep -Fq 'TextOverflow' native/cpp/include/uirepo/Core.h || fail "retained text overflow policy is missing"
grep -Fq 'CreateEllipsisTrimmingSign' native/cpp/src/D3D11TextLayout.cpp || fail "DirectWrite ellipsis policy is missing"
grep -Fq 'SetTextAlignment' native/cpp/src/D3D11TextLayout.cpp || fail "DirectWrite text alignment is missing"
grep -Fq 'SetWordWrapping' native/cpp/src/D3D11TextLayout.cpp || fail "DirectWrite wrapping is missing"
grep -Fq 'class D3D11TextLayoutCache' native/cpp/src/D3D11TextLayout.h || fail "configured text-layout cache is missing"
grep -Fq 'textLayoutCache_->acquire' native/cpp/src/D3D11Renderer.cpp || fail "renderer does not route text through the configured layout cache"
grep -Fq 'configureTextLayout' native/cpp/src/D3D11TextLayout.cpp || fail "cached text layouts do not apply retained text policy"
grep -Fq 'command.layout' native/cpp/src/D3D11TextLayout.cpp || fail "cached text policy is not command-driven"

# Public ABI remains append-only and capability aligned.
grep -Fq 'UIREPO_CAP_TEXT_LAYOUT' native/cpp/include/uirepo/NativeApi.h || fail "text-layout capability bit is missing"
grep -Fq 'UIRepoTextLayoutV1' native/cpp/include/uirepo/NativeApi.h || fail "POD text-layout ABI struct is missing"
grep -Fq 'setTextLayout' native/cpp/include/uirepo/NativeApi.h || fail "text-layout ABI setter is missing"
grep -Fq 'api.setTextLayout = &apiSetTextLayout;' native/fo4/src/NativeApi.cpp || fail "text-layout API table slot is not assigned"
grep -Fq '(kApiV1.capabilities & UIREPO_CAP_TEXT_LAYOUT)' native/fo4/src/NativeApi.cpp || fail "text-layout capability-to-slot assertion is missing"
grep -Fq 'setTextLayout' native/fo4/src/DocumentRegistryTextLayout.cpp || fail "registry text-layout implementation is missing"

# Authoritative xmake graph must compile every production renderer unit/test.
grep -Fq 'native/cpp/src/D3D11TextLayout.cpp' xmake.lua || fail "xmake renderer target omits DirectWrite text policy"
grep -Fq 'native/cpp/src/D3D11ImageCache.cpp' xmake.lua || fail "xmake renderer target omits WIC cache"
grep -Fq 'target("uirepo_d3d11_image_tests")' xmake.lua || fail "xmake image regression target is missing"
grep -Fq 'target("uirepo_d3d11_text_tests")' xmake.lua || fail "xmake DirectWrite layout test target is missing"
grep -Fq 'uirepo_d3d11_image_tests' scripts/ci-xwin-build.sh || fail "xwin CI does not compile image regressions"
grep -Fq 'uirepo_d3d11_text_tests' scripts/ci-xwin-build.sh || fail "xwin CI does not compile DirectWrite layout regressions"

echo "renderer production validation passed"
