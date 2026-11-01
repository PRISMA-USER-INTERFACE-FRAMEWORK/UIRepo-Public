#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "Stage 4 offscreen surface contract failed: $*" >&2
    exit 1
}

header=native/cpp/include/uirepo/D3D11SurfaceManager.h
source=native/cpp/src/D3D11SurfaceManager.cpp
runtime_header=native/fo4/src/Runtime.h
runtime_surface=native/fo4/src/RuntimeSurface.cpp
render_hook=native/fo4/src/RenderHook.cpp
registry_header=native/fo4/src/DocumentRegistry.h
registry_lifecycle=native/fo4/src/DocumentRegistryLifecycle.cpp
registry_surface=native/fo4/src/DocumentRegistrySurface.cpp
xmake=xmake.lua
windows_ci=scripts/ci-windows-stage0.ps1
xwin_ci=scripts/ci-xwin-build.sh

test -f "$header" || fail "D3D11 surface manager header is missing"
test -f "$source" || fail "D3D11 surface manager implementation is missing"
test -f native/cpp/tests/D3D11SurfaceTests.cpp || fail "surface WARP regression is missing"
test -f native/cpp/tests/D3D11SurfaceDeviceTests.cpp || fail "surface device-rebuild regression is missing"

grep -Fq 'DefaultMaxLiveBytes' "$header" || fail "surface manager needs a hard live-byte budget"
grep -Fq 'DefaultMaxPoolBytes' "$header" || fail "surface pool needs a hard byte budget"
grep -Fq 'DefaultMaxPoolEntries' "$header" || fail "surface pool needs a hard entry budget"
grep -Fq 'expectedGeneration' "$header" || fail "borrowed RTV/SRV access must be generation checked"
grep -Fq 'rebindDevice' "$header" || fail "surface manager must support device recreation"
grep -Fq 'D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE' "$source" || fail "offscreen texture must own both RTV and SRV bind capabilities"
grep -Fq 'stats_.liveBytes > maxLiveBytes_ - bytes' "$source" || fail "aggregate live resource admission must fail before exceeding budget"
grep -Fq 'clearResource(resource)' "$source" || fail "new or pooled resources must be cleared before exposure"
grep -Fq 'surface.generation' "$source" || fail "surface resource replacement must advance generation"
grep -Fq 'stats_.deviceRebuilds' "$source" || fail "device rebuilds must be observable"
grep -Fq 'renderer.target()' "$source" || fail "offscreen rendering must snapshot the existing renderer target"
grep -Fq 'renderer.setTarget(previousTarget.Get(), previousSize)' "$source" || fail "offscreen rendering must restore the existing renderer target"

grep -Fq 'SurfaceReleaseHook' "$registry_header" || fail "registry must expose a surface resource release boundary"
grep -Fq 'setSurfaceReleaseHook' "$registry_lifecycle" || fail "registry surface release hook is not implemented"
grep -Fq 'releaseHook(surface, releaseUserData)' "$registry_lifecycle" || fail "surface detach must notify the runtime resource owner"
grep -Fq 'snapshotDisplayList' "$registry_surface" || fail "runtime needs a document display-list snapshot for offscreen rendering"
grep -Fq 'entry->visible || entry->inputEnabled' "$registry_surface" || fail "offscreen layout must reject documents sharing live flat/input bounds"

grep -Fq 'createOffscreenSurface' "$runtime_header" || fail "runtime offscreen creation API is missing"
grep -Fq 'renderDocumentToSurface' "$runtime_header" || fail "runtime offscreen render API is missing"
grep -Fq 'borrowOffscreenSurfaceShaderResource' "$runtime_header" || fail "runtime borrowed SRV path is missing"
grep -Fq 'shutdownOffscreenSurfaces' "$runtime_header" || fail "runtime explicit offscreen shutdown API is missing"
grep -Fq 'registry.setSurfaceReleaseHook' "$runtime_surface" || fail "runtime must register as the registry surface resource owner"
grep -Fq 'registry.surfaceDocument(surface) != document' "$runtime_surface" || fail "offscreen rendering must reject cross-document surface use"
grep -Fq 'snapshotDisplayList' "$runtime_surface" || fail "offscreen rendering must render the owning document at surface size"
grep -Fq 'GameThreadDispatcher::isGameThread()' "$runtime_surface" || fail "GPU surface operations must fail closed off the verified Fallout thread"
grep -Fq 'GameThreadDispatcher::dispatchSafety' "$runtime_surface" || fail "off-thread destruction must defer GPU release through the reserved safety queue"
grep -Fq 'setSurfaceReleaseHook(nullptr, nullptr)' "$runtime_surface" || fail "offscreen shutdown must detach the registry resource callback"
grep -Fq 'surfaceManager_->shutdown()' "$runtime_surface" || fail "offscreen shutdown must release live and pooled GPU resources"
if [ "$(grep -Fc 'Runtime::instance().shutdownOffscreenSurfaces();' "$render_hook")" -lt 2 ]; then
    fail "every explicit renderer shutdown path must clear offscreen GPU surfaces first"
fi

surface_tests=native/cpp/tests/D3D11SurfaceTests.cpp
lifecycle_tests=native/cpp/tests/RegistryLifecycleTests.cpp

grep -Fq 'testOffscreenDisplayListIsolation' "$lifecycle_tests" || fail "display-list isolation rule needs portable executable coverage"
grep -Fq 'testTransparentAndOpaqueClearPolicy' "$surface_tests" || fail "transparent and opaque clear policy needs pixel-level coverage"
grep -Fq 'testMultiSurfaceIsolation' "$surface_tests" || fail "concurrent surfaces need pixel-level isolation coverage"
grep -Fq 'testPooledResourceDoesNotLeakPreviousPixels' "$surface_tests" || fail "pooled resource reuse must be proven not to leak previous pixels"
grep -Fq 'testLowMemoryPoolDrainAndRetry' "$surface_tests" || fail "low-memory pool drain and retry needs coverage"
grep -Fq 'testSurfaceResourceShapeAndByteAccounting' "$surface_tests" || fail "byte accounting must be pinned to the real resource shape"
grep -Fq 'testRepeatedCreateDestroyAccounting' "$surface_tests" || fail "repeated create/destroy needs stable-accounting coverage"
grep -Fq 'testDocumentRenderedToMultipleSurfaces' "$surface_tests" || fail "one document rendered to multiple surfaces needs an explicit tested policy"

# No MSAA and no mip chain: the byte budget is exact only while this holds.
grep -Fq 'textureDesc.SampleDesc.Count = 1' "$source" || fail "offscreen surfaces must not be multisampled"
grep -Fq 'textureDesc.MipLevels = 1' "$source" || fail "offscreen surfaces must not allocate a mip chain"

native_api_header=native/cpp/include/uirepo/NativeApi.h
native_api=native/fo4/src/NativeApi.cpp
contract_tests=native/cpp/tests/NativeApiContractTests.cpp

grep -Fq 'UIREPO_CAP_OFFSCREEN_SURFACES' "$native_api_header" || fail "offscreen surfaces need a capability bit consumers can test"
grep -Fq 'UIRepoSurfaceHandle' "$native_api_header" || fail "public offscreen ABI must name surfaces by handle"
grep -Fq 'createOffscreenSurface' "$native_api_header" || fail "public offscreen creation entry point is missing"
grep -Fq 'renderDocumentToSurface' "$native_api_header" || fail "public offscreen render entry point is missing"
grep -Fq 'UIREPO_CAP_OFFSCREEN_SURFACES' "$native_api" || fail "offscreen capability bit is not advertised by the implementation"
grep -Fq 'sizeof(UIRepoSurfaceInfoV1)' "$contract_tests" || fail "public offscreen structs need ABI layout coverage"

# The offscreen ABI names surfaces by handle only. No D3D11 device, context,
# texture, or view type may ever cross the public boundary.
if grep -Fq 'ID3D11' "$native_api_header"; then
    fail "public ABI header must not expose D3D11 types"
fi
if grep -Fq 'd3d11.h' "$native_api_header"; then
    fail "public ABI header must not require the D3D11 headers"
fi

grep -Fq 'uirepo_d3d11_surface_tests' "$xmake" || fail "surface regression target is missing from xmake"
grep -Fq 'uirepo_d3d11_surface_device_tests' "$xmake" || fail "surface device regression target is missing from xmake"
grep -Fq "'uirepo_d3d11_surface_tests'" "$windows_ci" || fail "native Windows CI must execute surface regressions"
grep -Fq "'uirepo_d3d11_surface_device_tests'" "$windows_ci" || fail "native Windows CI must execute device-rebuild surface regressions"
grep -Fq 'uirepo_d3d11_surface_tests' "$xwin_ci" || fail "xwin CI must compile the surface regression"
grep -Fq 'uirepo_d3d11_surface_device_tests' "$xwin_ci" || fail "xwin CI must compile the device-rebuild surface regression"

echo "Stage 4 bounded offscreen surface contract passed"
