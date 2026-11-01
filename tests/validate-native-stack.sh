#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "native stack validation failed: $*" >&2
    exit 1
}

pin="$(tr -d '\r\n' < deps/commonlibf4.ref)"
[[ "$pin" =~ ^[0-9a-f]{40}$ ]] || fail "deps/commonlibf4.ref must contain one lowercase 40-character commit SHA"

# xmake is the one authoritative build graph. Do not reintroduce a parallel CMake graph.
test ! -e CMakeLists.txt || fail "root CMake graph must not be reintroduced; use xmake.lua"
test ! -e native/cpp/CMakeLists.txt || fail "native CMake graph must not be reintroduced; use xmake.lua"
test ! -e cmake/xwin-clang-cl.cmake || fail "obsolete CMake xwin toolchain must not be reintroduced"
grep -Fq 'target("uirepo_core")' xmake.lua || fail "xmake core target is missing"
grep -Fq 'target("uirepo_native_tree_foundation_tests")' xmake.lua || fail "xmake retained tree foundation regression target is missing"
grep -Fq 'target("uirepo_native_tree_mutation_tests")' xmake.lua || fail "xmake retained tree mutation regression target is missing"
grep -Fq 'target("uirepo_animation_tests")' xmake.lua || fail "xmake animation regression target is missing"
grep -Fq 'target("uirepo_control_registry_tests")' xmake.lua || fail "xmake control registry regression target is missing"
grep -Fq 'target("uirepo_registry_lifecycle_tests")' xmake.lua || fail "xmake registry lifecycle regression target is missing"
grep -Fq 'target("uirepo_dispatch_lifecycle_tests")' xmake.lua || fail "xmake dispatch lifecycle regression target is missing"
grep -Fq 'target("uirepo_focus_lease_tests")' xmake.lua || fail "xmake focus lease regression target is missing"
grep -Fq 'target("uirepo_input_lifecycle_tests")' xmake.lua || fail "xmake input lifecycle regression target is missing"
grep -Fq 'target("uirepo_bounded_cache_tests")' xmake.lua || fail "xmake bounded renderer cache regression target is missing"
grep -Fq 'target("uirepo_image_policy_tests")' xmake.lua || fail "xmake image resource policy regression target is missing"
grep -Fq 'target("uirepo_d3d11_tests")' xmake.lua || fail "xmake D3D11 regression target is missing"
grep -Fq 'set_languages("c++23")' xmake.lua || fail "xmake must require C++23"
if grep -R -n -E '(^|[[:space:]])(cmake|ctest)([[:space:]]|$)' .github/workflows scripts/ci-native-core.sh scripts/ci-xwin-build.sh scripts/ci-fo4-dll-build.sh; then
    fail "CI/build scripts must use the authoritative xmake graph"
fi

grep -Fq "local commonlib_pin = \"$pin\"" xmake.lua || fail "xmake CommonLib pin must match deps/commonlibf4.ref"
repo_url='https://github.com/G-A-R-D-E-N/commonlibf4-nomad.git'
for clone_script in scripts/setup-commonlib.ps1 scripts/ci-fo4-dll-build.sh scripts/ci-windows-stage0.ps1; do
    grep -Fq "$repo_url" "$clone_script" || fail "$clone_script must clone $repo_url"
done
grep -Fq 'remote set-url origin $Repository' scripts/setup-commonlib.ps1 || fail "setup-commonlib.ps1 must repoint an existing checkout's origin at the pinned repository"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || grep -Fq "printf 'commonlib_sha=%s\n' '$pin'" .github/workflows/ci.yml || fail "Stage 0 manifest commonlib_sha must match deps/commonlibf4.ref"
if grep -rIn -e 'PRISMA-USER-INTERFACE-FRAMEWORK/commonlib_NonVR' -e 'COMMONLIB_READ_TOKEN: ' .github scripts deps xmake.lua; then
    fail "the retired private CommonLib repository and the CI read-token secret must not be referenced"
fi
if grep -nE 'will fail|COMMONLIB_READ_TOKEN:[?=]|COMMONLIB_READ_TOKEN\?|-z "\$\{COMMONLIB_READ_TOKEN' scripts/ci-fo4-dll-build.sh; then
    fail "scripts/ci-fo4-dll-build.sh must not require the CommonLibF4 read token or warn that the clone will fail"
fi
grep -Fq 'add_rules("commonlibf4.plugin"' xmake.lua || fail "UIRepo_F4 must use Dear Modding commonlibf4.plugin"
grep -Fq 'set_pcxxheader("native/fo4/src/PCH.h")' xmake.lua || fail "UIRepo_F4 must use its CommonLib consumer PCH"
test -f native/fo4/src/PCH.h || fail "UIRepo_F4 consumer PCH is missing"
grep -Fq '#include <RE/Fallout.h>' native/fo4/src/PCH.h || fail "FO4 PCH must establish the CommonLib Fallout include contract"
grep -Fq 'COMMONLIB_RUNTIMECOUNT=3' xmake.lua || fail "CommonLib multi-runtime count must remain explicit"
grep -Fq 'UIREPO_COMMONLIBF4_VERIFIED_SHA' xmake.lua || fail "CI must be able to pass a preverified CommonLib SHA"
grep -Fq 'os.iorunv(' xmake.lua || fail "local xmake builds must verify the selected CommonLib checkout"
grep -Fq 'rev-parse", "HEAD"' xmake.lua || fail "local xmake builds must compare the actual CommonLib Git HEAD"
grep -Fq 'UIREPO_COMMONLIBF4_PIN' xmake.lua || fail "FO4 build must embed the verified CommonLib revision"
grep -Fq 'PluginVersionData::GetSingleton()' native/fo4/src/Plugin.cpp || fail "F4SE query must use generated CommonLib plugin metadata"
grep -Fq 'F4SE::RUNTIME_1_10_163' native/fo4/src/Plugin.cpp || fail "minimum Fallout runtime must match the Dear Modding template floor"
grep -Fq 'F4SE_PLUGIN_PRELOAD' native/fo4/src/Plugin.cpp || fail "NG/AE preload lifecycle must match the Dear Modding template"
grep -Fq 'F4SE_PLUGIN_LOAD' native/fo4/src/Plugin.cpp || fail "OG normal-load lifecycle must remain available"

# Retained UI tree foundation contract.
test -f native/cpp/tests/NativeTreeFoundationTests.cpp || fail "retained UI tree foundation regressions are missing"
test -f native/cpp/tests/NativeTreeMutationTests.cpp || fail "retained UI tree mutation regressions are missing"

# Portable animation primitives contract.
test -f native/cpp/include/uirepo/Animation.h || fail "portable animation header is missing"
test -f native/cpp/src/Animation.cpp || fail "portable animation implementation is missing"
test -f native/cpp/tests/AnimationTests.cpp || fail "animation regressions are missing"
grep -Fq 'Animation.cpp' xmake.lua || fail "animation implementation must be part of the uirepo_core target"

# .uipkg package container contract.
grep -Fq 'target("uirepo_package")' xmake.lua || fail "xmake uipkg package library target is missing"
grep -Fq 'target("uirepo_package_tests")' xmake.lua || fail "xmake uipkg package regression target is missing"
for header in Package PackageHash; do
    test -f "native/cpp/include/uirepo/$header.h" || fail "uipkg header $header.h is missing"
done
for source in PackageHash PackageManifest PackagePath PackageReader PackageWriter; do
    test -f "native/cpp/src/$source.cpp" || fail "uipkg source $source.cpp is missing"
    grep -Fq "native/cpp/src/$source.cpp" xmake.lua || fail "uipkg source $source.cpp must be part of the uirepo_package target"
done
for test_source in PackageTests PackageMalformedTests; do
    test -f "native/cpp/tests/$test_source.cpp" || fail "uipkg regression source $test_source.cpp is missing"
    grep -Fq "native/cpp/tests/$test_source.cpp" xmake.lua || fail "uipkg regression source $test_source.cpp must be part of uirepo_package_tests"
done
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || test -f docs/uipkg-format.md || fail "uipkg format document is missing"
grep -Fq 'uirepo_package_tests' scripts/ci-native-core.sh || fail "CI must run the uipkg package regressions"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || grep -Fq 'uirepo_package_tests' README.md || fail "README portable test list must include the uipkg package regressions"
if grep -nE '#[[:space:]]*include[[:space:]]*[<"](windows|d3d|dxgi|RE/|F4SE|commonlib|imgui)' native/cpp/src/Package*.cpp native/cpp/src/PackageInternal.h native/cpp/include/uirepo/Package*.h; then
    fail "the uipkg library must stay free of Windows, D3D, F4SE, CommonLib and UI toolkit headers"
fi
# Declarative UI format contract.
test -f native/cpp/include/uirepo/DeclarativeUi.h || fail "declarative UI public header is missing"
for source in DeclarativeFields DeclarativeJson DeclarativeParse DeclarativeSerialize DeclarativeStyle; do
    test -f "native/cpp/src/$source.cpp" || fail "declarative UI source $source.cpp is missing"
    grep -Fq "native/cpp/src/$source.cpp" xmake.lua || fail "$source.cpp must be part of the uirepo_declarative target"
done
test -f native/cpp/tests/DeclarativeTests.cpp || fail "declarative UI regressions are missing"
test -f native/cpp/tests/DeclarativeErrorTests.cpp || fail "declarative UI malformed-input table is missing"
test -f native/cpp/tests/DeclarativePercentTests.cpp || fail "declarative UI percent regressions are missing"
grep -Fq 'native/cpp/tests/DeclarativePercentTests.cpp' xmake.lua || fail "percent regressions must be part of the uirepo_declarative_tests target"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || test -f docs/declarative-ui-format.md || fail "declarative UI format documentation is missing"
grep -Fq 'target("uirepo_declarative")' xmake.lua || fail "xmake declarative UI library target is missing"
grep -Fq 'target("uirepo_declarative_tests")' xmake.lua || fail "xmake declarative UI regression target is missing"
grep -Fq 'uirepo_declarative_tests' scripts/ci-native-core.sh || fail "declarative UI regressions must run in the portable CI job"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || grep -Fq 'uirepo_declarative_tests' README.md || fail "README portable test list must include the declarative UI regressions"
grep -Fq 'uirepo_declarative_tests' scripts/ci-xwin-build.sh || fail "xwin CI must compile the declarative UI regressions"
grep -Fq 'kFormatVersion' native/cpp/include/uirepo/DeclarativeUi.h || fail "declarative UI format version must stay explicit"
if grep -n -E 'windows\.h|d3d11|dxgi|RE/Fallout|F4SE|ultralight|imgui' native/cpp/src/Declarative*.cpp native/cpp/src/Declarative*.h native/cpp/include/uirepo/DeclarativeUi.h; then
    fail "the declarative UI loader must stay portable and free of Windows, renderer and engine dependencies"
fi
for source in native/cpp/src/Declarative*.cpp native/cpp/src/Declarative*.h native/cpp/tests/Declarative*.cpp native/cpp/tests/Declarative*.h; do
    [ "$(wc -l < "$source")" -lt 800 ] || fail "$source must stay under 800 lines"
done

# Runtime package host contract.
test -f native/cpp/include/uirepo/PackageHost.h || fail "package host public header is missing"
test -f native/cpp/src/PackageHost.cpp || fail "package host implementation is missing"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || test -f docs/package-host.md || fail "package host documentation is missing"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || grep -Fq 'docs/package-host.md' README.md || fail "README must link the package host document"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || grep -Fq 'package-host.md' docs/architecture.md || fail "architecture document must link the package host document"
grep -Fq 'target("uirepo_package_host")' xmake.lua || fail "xmake package host library target is missing"
grep -Fq 'native/cpp/src/PackageHost.cpp' xmake.lua || fail "PackageHost.cpp must be part of the uirepo_package_host target"
grep -Fq 'target("uirepo_package_host_tests")' xmake.lua || fail "xmake package host regression target is missing"
for test_source in PackageHostTests PackageHostRefusalTests; do
    test -f "native/cpp/tests/$test_source.cpp" || fail "package host regression source $test_source.cpp is missing"
    grep -Fq "native/cpp/tests/$test_source.cpp" xmake.lua || fail "package host regression source $test_source.cpp must be part of uirepo_package_host_tests"
done
grep -Fq 'uirepo_package_host_tests' scripts/ci-native-core.sh || fail "package host regressions must run in the portable CI job"
grep -Fq 'uirepo_package_host_tests' scripts/ci-xwin-build.sh || fail "xwin CI must compile the package host regressions"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || grep -Fq 'uirepo_package_host_tests' README.md || fail "README portable test list must include the package host regressions"
test -f native/fo4/src/PackageHostRuntime.h || fail "package host runtime header is missing"
test -f native/fo4/src/PackageHostRuntime.cpp || fail "package host runtime implementation is missing"
grep -Fq 'target("uirepo_package_host_runtime_tests")' xmake.lua || fail "xmake package host runtime regression target is missing"
test -f native/cpp/tests/PackageHostRuntimeTests.cpp || fail "package host runtime regressions are missing"
grep -Fq 'native/fo4/src/PackageHostRuntime.cpp' xmake.lua || fail "PackageHostRuntime.cpp must be part of uirepo_package_host_runtime_tests"
grep -Fq 'uirepo_package_host_runtime_tests' scripts/ci-native-core.sh || fail "package host runtime regressions must run in the portable CI job"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || grep -Fq 'uirepo_package_host_runtime_tests' README.md || fail "README portable test list must include the package host runtime regressions"
grep -Fq 'adoptOwnedDocument' native/fo4/src/PackageHostRuntime.cpp || fail "the package host runtime must adopt documents under a per-package owner"
grep -Fq 'destroyOwnerDocuments' native/fo4/src/PackageHostRuntime.cpp || fail "the package host runtime must unload through owner-scoped destruction"
test -f native/fo4/src/PackageHostStartup.cpp || fail "package host startup hook is missing"
grep -Fq 'PackageHostStartup::run()' native/fo4/src/Plugin.cpp || fail "the plugin must start the package host at game data ready"
grep -Fq 'capabilityNamesFromAbiMask(nativeApiV2().v1.capabilities)' native/fo4/src/PackageHostStartup.cpp || fail "package capabilities must come from the published ABI mask"
grep -Fq 'add_deps("uirepo_render_d3d11", "uirepo_package_host")' xmake.lua || fail "UIRepo_F4 must link the package host"
if grep -n -E '#[[:space:]]*include[[:space:]]*[<"](windows|d3d|dxgi|RE/|F4SE|commonlib|imgui|spdlog)' native/fo4/src/PackageHostRuntime.cpp native/fo4/src/PackageHostRuntime.h; then
    fail "the package host runtime scanner must stay portable so its regressions run on Linux"
fi
if grep -n -E '#[[:space:]]*include[[:space:]]*[<"](windows|d3d|dxgi|RE/|F4SE|commonlib|imgui|spdlog)' native/cpp/src/PackageHost.cpp native/cpp/include/uirepo/PackageHost.h; then
    fail "the package host library must stay free of Windows, D3D, F4SE, CommonLib, logging and UI toolkit headers"
fi
for source in native/cpp/src/PackageHost*.cpp native/fo4/src/PackageHost*.cpp native/fo4/src/PackageHost*.h native/cpp/include/uirepo/PackageHost.h native/cpp/tests/PackageHost*.cpp native/cpp/tests/PackageHost*.h; do
    [ "$(wc -l < "$source")" -lt 800 ] || fail "$source must stay under 800 lines"
done

# Registry ownership/lifecycle contract.
test -f native/cpp/include/uirepo/RegistryTypes.h || fail "portable registry lifecycle types are missing"
test -f native/fo4/src/DocumentRegistryLifecycle.cpp || fail "registry lifecycle implementation is missing"
test -f native/cpp/tests/RegistryLifecycleTests.cpp || fail "registry lifecycle regressions are missing"
test -f native/cpp/tests/DispatchLifecycleTests.cpp || fail "dispatch lifecycle regressions are missing"
grep -Fq 'createOwnedDocument' native/fo4/src/DocumentRegistryLifecycle.cpp || fail "registry must associate documents with caller ownership"
grep -Fq 'destroyDocumentManaged' native/fo4/src/DocumentRegistryLifecycle.cpp || fail "managed document teardown is missing"
grep -Fq 'destroyOwnerDocuments' native/fo4/src/DocumentRegistryLifecycle.cpp || fail "caller-scoped document teardown is missing"
grep -Fq 'adoptOwnedDocument' native/fo4/src/DocumentRegistryLifecycle.cpp || fail "registry must adopt prebuilt document trees"
grep -Fq 'acceptsGeneration' native/fo4/src/DocumentRegistryLifecycle.cpp || fail "stale worker generation rejection is missing"
grep -Fq 'enumerateDocuments' native/fo4/src/DocumentRegistryLifecycle.cpp || fail "registry diagnostics enumeration is missing"
grep -Fq 'setOwnerCancelHook' native/fo4/src/Plugin.cpp || fail "registry teardown must be wired to owner callback cancellation"
grep -Fq 'GameThreadDispatcher::cancelOwner' native/fo4/src/Plugin.cpp || fail "registry teardown must wait out dispatcher owner callbacks"
grep -Fq 'createOwnedDocument' native/fo4/src/NativeApi.cpp || fail "consumer document creation must enter the owned registry path"
grep -Fq 'destroyDocumentManaged' native/fo4/src/NativeApi.cpp || fail "consumer document destruction must use managed teardown"

# Focus lease and recovery contract.
test -f native/cpp/include/uirepo/FocusLease.h || fail "portable focus lease state is missing"
test -f native/cpp/tests/FocusLeaseTests.cpp || fail "focus lease regressions are missing"
test -f native/fo4/src/FocusLeaseManager.cpp || fail "Fallout focus lease adapter is missing"
test -f native/fo4/src/DocumentRegistryKey.cpp || fail "document key callback routing is missing"
grep -Fq 'episodeGeneration' native/cpp/include/uirepo/FocusLease.h || fail "focus handoff generation tracking is missing"
grep -Fq 'advancePresentationFrame' native/cpp/include/uirepo/FocusLease.h || fail "bounded focus recovery policy is missing"
grep -Fq 'InputMask::apply' native/fo4/src/FocusLeaseManager.cpp || fail "focus lease must own the Fallout input mask"
grep -Fq 'GameThreadDispatcher::applyCursorLease' native/fo4/src/FocusLeaseManager.cpp || fail "focus lease must apply cursor ownership"
grep -Fq 'PauseHold::setPaused' native/fo4/src/FocusLeaseManager.cpp || fail "focus lease must drive pause policy"
grep -Fq 'GameThreadDispatcher::dispatchSafety' native/fo4/src/FocusLeaseManager.cpp || fail "focus resource mutations must cross the verified safety dispatcher"
grep -Fq 'syncFromRegistry' native/fo4/src/RenderHook.cpp || fail "presented frames must reconcile authoritative registry focus"
grep -Fq 'FocusLeaseManager::releaseAll' native/fo4/src/RenderHook.cpp || fail "render hook teardown must release the focus lease"
grep -Fq 'GetRawInputData' native/fo4/src/GameThreadDispatcher.cpp || fail "owned Escape must be intercepted on the WM_INPUT raw keyboard path"
grep -Fq 'raw.header.dwType != RIM_TYPEKEYBOARD' native/fo4/src/GameThreadDispatcher.cpp || fail "raw Escape interception must reject non-keyboard packets"
grep -Fq 'raw.data.keyboard.VKey != VK_ESCAPE' native/fo4/src/GameThreadDispatcher.cpp || fail "raw input ownership must be scoped to Escape only"
grep -Fq 'FocusLeaseManager::ownsEscape()' native/fo4/src/GameThreadDispatcher.cpp || fail "raw Escape must remain pass-through unless the focused lease opts in"
grep -Fq 'WM_SETCURSOR' native/fo4/src/GameThreadDispatcher.cpp || fail "cursor lease must coordinate client cursor ownership"
grep -Fq 'ClipCursor(nullptr)' native/fo4/src/GameThreadDispatcher.cpp || fail "cursor acquisition must clear stale Fallout clipping"
if grep -R -n -E 'ShowCursor[[:space:]]*\(' native/fo4 --include='*.cpp' --include='*.h'; then
    fail "cursor lease must not mutate Win32 ShowCursor reference counts"
fi

# Bounded renderer resource contract.
test -f native/cpp/include/uirepo/BoundedCache.h || fail "bounded renderer cache primitive is missing"
test -f native/cpp/tests/BoundedCacheTests.cpp || fail "bounded renderer cache regressions are missing"
grep -Fq 'maxBytes_' native/cpp/include/uirepo/BoundedCache.h || fail "renderer cache must enforce a byte budget"
grep -Fq 'maxEntries_' native/cpp/include/uirepo/BoundedCache.h || fail "renderer cache must enforce an entry budget"
grep -Fq 'evictions' native/cpp/include/uirepo/BoundedCache.h || fail "renderer cache must expose eviction telemetry"
grep -Fq 'rejections' native/cpp/include/uirepo/BoundedCache.h || fail "renderer cache must expose rejection telemetry"

# Production image pipeline contract.
test -f native/cpp/include/uirepo/ImagePolicy.h || fail "image admission policy is missing"
test -f native/cpp/tests/ImagePolicyTests.cpp || fail "image admission regressions are missing"
test -f native/cpp/src/D3D11ImageCache.h || fail "private D3D11 image cache is missing"
test -f native/cpp/src/D3D11ImageCache.cpp || fail "WIC image cache implementation is missing"
grep -Fq 'admitRgba8Image' native/cpp/include/uirepo/ImagePolicy.h || fail "image dimensions must be admitted before D3D allocation"
grep -Fq 'MaxImageDimension = 8192u' native/cpp/include/uirepo/ImagePolicy.h || fail "image dimension cap must remain explicit"
grep -Fq 'D3D11ImageCache.cpp' xmake.lua || fail "WIC image cache must be part of the xmake renderer target"
grep -Fq 'windowscodecs' xmake.lua || fail "WIC image pipeline must link WindowsCodecs"
grep -Fq 'ole32' xmake.lua || fail "WIC image pipeline must link COM runtime"
grep -Fq 'CreateDecoderFromFilename' native/cpp/src/D3D11ImageCache.cpp || fail "image pipeline must decode files through WIC"
grep -Fq 'GUID_WICPixelFormat32bppRGBA' native/cpp/src/D3D11ImageCache.cpp || fail "WIC image pipeline must normalize to RGBA8"
grep -Fq 'DXGI_FORMAT_R8G8B8A8_UNORM' native/cpp/src/D3D11ImageCache.cpp || fail "image upload format policy must remain explicit UNORM"
grep -Fq 'negativeCache_' native/cpp/src/D3D11ImageCache.h || fail "failed image decode must use bounded negative caching"
grep -Fq 'DefaultImageCacheBytes = 64u * 1024u * 1024u' native/cpp/include/uirepo/D3D11Renderer.h || fail "image cache byte budget must remain explicit"
grep -Fq 'imageCacheBytes' native/cpp/include/uirepo/D3D11Renderer.h || fail "renderer telemetry must expose image cache bytes"
grep -Fq 'drawImage(value, currentClip(), localStats)' native/cpp/src/D3D11Renderer.cpp || fail "DrawImageCommand must execute through the image renderer"

# Native SDK contract. Keep third-party consumers on an opaque, versioned C table instead of
# exposing STL/CommonLib/renderer implementation types across the DLL boundary.
test -f native/cpp/include/uirepo/NativeApi.h || fail "versioned native SDK header is missing"
test -f native/cpp/include/uirepo/InputRouter.h || fail "portable input router is missing"
test -f native/cpp/tests/NativeApiContractTests.cpp || fail "native ABI contract regression is missing"
test -f native/fo4/src/NativeApi.cpp || fail "native SDK export implementation is missing"
test -f native/fo4/src/DocumentRegistry.cpp || fail "owned native document registry is missing"
test -f native/fo4/src/ControllerInput.cpp || fail "CommonLib controller adapter is missing"
grep -Fq '#define UIREPO_NATIVE_API_VERSION 2u' native/cpp/include/uirepo/NativeApi.h || fail "native SDK ABI version must remain explicit"
grep -Fq 'typedef uint64_t UIRepoDocumentHandle;' native/cpp/include/uirepo/NativeApi.h || fail "documents must cross the ABI as opaque handles"
grep -Fq 'typedef uint64_t UIRepoNodeHandle;' native/cpp/include/uirepo/NativeApi.h || fail "nodes must cross the ABI as opaque handles"
grep -Fq 'uint32_t abiVersion;' native/cpp/include/uirepo/NativeApi.h || fail "native API table must publish its ABI version"
grep -Fq 'uint64_t capabilities;' native/cpp/include/uirepo/NativeApi.h || fail "native API table must publish capabilities"
grep -Fq 'setDocumentInputEnabled' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose document input ownership"
grep -Fq 'setNodeEventHandler' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose node event handlers"
grep -Fq 'focusNode' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose focus"
grep -Fq 'clearDocumentFocus' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose focus clearing"
grep -Fq 'routePointerEvent' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose pointer routing"
grep -Fq 'routeKeyEvent' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose keyboard routing"
grep -Fq 'setDocumentKeyEventHandler' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose focused document key callbacks"
grep -Fq 'setDocumentFocusRequirements' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose focus lease requirements"
grep -Fq 'UIREPO_CAP_DOCUMENT_KEY_EVENTS' native/cpp/include/uirepo/NativeApi.h || fail "native API must version document key event capability"
grep -Fq 'UIREPO_CAP_FOCUS_LEASE' native/cpp/include/uirepo/NativeApi.h || fail "native API must version focus lease capability"
grep -Fq 'createTextInput' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose text input creation"
grep -Fq 'getTextInputValue' native/cpp/include/uirepo/NativeApi.h || fail "native API must expose bounded text input reads"
grep -Fq 'UIRepo_GetNativeApi' native/cpp/include/uirepo/NativeApi.h || fail "native API resolver declaration is missing"
grep -Fq 'extern "C" __declspec(dllexport) const UIRepoNativeApiV1* UIRepo_GetNativeApi' native/fo4/src/NativeApi.cpp || fail "native API resolver must be an explicit C export from UIRepo_F4"
grep -Fq 'UIREPO_CAP_ENGINE_COMPOSITION' native/fo4/src/NativeApi.cpp || fail "SDK must advertise the engine compositor capability"
grep -Fq 'UIREPO_CAP_RENDERER_RECOVERY' native/fo4/src/NativeApi.cpp || fail "SDK must advertise renderer recovery only while it is implemented"
grep -Fq 'UIREPO_CAP_POINTER_INPUT' native/fo4/src/NativeApi.cpp || fail "pointer capability must be wired by the implementation"
grep -Fq 'UIREPO_CAP_KEYBOARD_INPUT' native/fo4/src/NativeApi.cpp || fail "keyboard capability must be wired by the implementation"
grep -Fq 'api.routePointerEvent = &apiRoutePointerEvent;' native/fo4/src/NativeApi.cpp || fail "pointer ABI slot must be explicitly assigned"
grep -Fq 'api.routeKeyEvent = &apiRouteKeyEvent;' native/fo4/src/NativeApi.cpp || fail "keyboard ABI slot must be explicitly assigned"
grep -Fq 'api.setDocumentKeyEventHandler = &apiSetDocumentKeyEventHandler;' native/fo4/src/NativeApi.cpp || fail "document key callback ABI slot must be explicitly assigned"
grep -Fq 'api.setDocumentFocusRequirements = &apiSetDocumentFocusRequirements;' native/fo4/src/NativeApi.cpp || fail "focus lease ABI slot must be explicitly assigned"
grep -Fq 'api.createTextInput = &apiCreateTextInput;' native/fo4/src/NativeApi.cpp || fail "text input creation ABI slot must be assigned"
grep -Fq 'api.getTextInputValue = &apiGetTextInputValue;' native/fo4/src/NativeApi.cpp || fail "text input read ABI slot must be assigned"
test -f native/cpp/include/uirepo/NativeApiBridge.h || fail "portable ABI v2 state, enumeration and dispatch bridge is missing"
test -f native/cpp/tests/NativeApiV2Tests.cpp || fail "ABI v2 bridge regressions are missing"
test -f native/fo4/src/NativeApiV2.cpp || fail "ABI v2 table implementation is missing"
grep -Fq 'target("uirepo_native_api_v2_tests")' xmake.lua || fail "xmake ABI v2 bridge regression target is missing"
grep -Fq 'uirepo_native_api_v2_tests' scripts/ci-native-core.sh || fail "ABI v2 bridge regression must run in the portable job"
[ "${UIREPO_PUBLIC_CI:-}" = 1 ] || grep -Fq 'uirepo_native_api_v2_tests' README.md || fail "README portable test list must include the ABI v2 bridge regression"
grep -Fq 'GameThreadDispatcher::dispatch(' native/fo4/src/NativeApiV2.cpp || fail "dispatchToGameThread must bridge the bounded game-thread dispatcher"
grep -Fq 'GameThreadDispatcher::isGameThread()' native/fo4/src/NativeApiV2.cpp || fail "isGameThread must report the verified dispatcher thread"
grep -Fq 'RegisterHandler(handler.get())' native/fo4/src/ControllerInput.cpp || fail "controller adapter must use the pinned CommonLib player input handler path"
grep -Fq 'HANDLED_RESULT::kStop' native/fo4/src/ControllerInput.cpp || fail "controller input may stop Fallout only after UIRepo handles it"

if grep -n -E 'std::|Microsoft::WRL|RE::|F4SE::|ID3D11' native/cpp/include/uirepo/NativeApi.h; then
    fail "public C ABI must not expose STL, CommonLib, F4SE, WRL, or D3D implementation types"
fi

# Engine composition should use Fallout's native D3D11 state as the primary source.
grep -Fq 'renderEngineFrame' native/fo4/src/RenderHook.cpp || fail "engine frame compositor entry point is missing"
grep -Fq 'BSGraphics::GetRendererData()' native/fo4/src/Runtime.cpp || fail "native renderer must acquire Fallout renderer state directly"
grep -Fq 'GetImmediateContext' native/fo4/src/Runtime.cpp || fail "native renderer must reacquire the actual D3D11 immediate context"
grep -Fq 'swapChainRenderTarget.rtView' native/fo4/src/Runtime.cpp || fail "engine compositor must use Fallout's active render target"
grep -Fq 'beforeResize' native/fo4/src/RenderHook.cpp || fail "native renderer must release retained targets before ResizeBuffers"

if grep -R -n -E 'F4SE_PLUGIN_VERSION|F4SEPlugin_Version' native/fo4 --include='*.cpp' --include='*.h'; then
    fail "FO4 sources must not duplicate plugin version metadata generated by commonlibf4.plugin"
fi

if grep -R -n -E 'CEF|Chromium|Ultralight|CoreCLR' native/cpp native/fo4 --include='*.cpp' --include='*.h'; then
    fail "native runtime must remain browser/CLR free"
fi

echo "native stack validation passed"
echo "authoritative build graph: xmake.lua"
echo "CommonLibF4 (commonlibf4-nomad) pin: $pin"
