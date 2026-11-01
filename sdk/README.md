# UIRepo consumer SDK

Everything needed to build a plugin against UIRepo, and nothing else.

```
include/uirepo/NativeApi.h         the ABI: types, capability bits, function table
include/uirepo/NativeApiLoader.h   optional header-only loader (C++17)
include/uirepo/NativeApiCpp.h      optional typed/RAII C++ wrapper (C++17)
include/uirepo/NativeApiCppTypes.h value types shared by the wrapper (handles, Result, struct factories)
samples/minimal/                   a working C++ wrapper consumer
SDK_VERSION.txt                    ABI version and the framework source SHA
```

## There is no link-time dependency

You do not link `UIRepo_F4.dll`, and there is no import library. The framework is a Fallout 4 plugin that is already loaded in the process; the API is a function table resolved at runtime through one export.

That means adding UIRepo to a plugin is one include directory and no linker changes:

```lua
add_includedirs("path/to/uirepo-sdk/include")
```

## Include order matters if you use CommonLibF4

UIRepo itself builds against **G-A-R-D-E-N/commonlibf4-nomad**. `NativeApiLoader.h` includes `<windows.h>`, and that CommonLibF4 fork refuses to be included *after* the Windows API with `Windows API detected. Please move any Windows API includes after CommonLib, or remove them.`

So CommonLib comes first:

```cpp
#include <F4SE/F4SE.h>              // CommonLibF4 first
#include "uirepo/NativeApiLoader.h" // then the UIRepo loader
```

Reverse those and it will not compile. If you resolve `UIRepo_GetNativeApi` yourself and skip the loader, the question does not arise because `NativeApi.h` and `NativeApiCpp.h` do not include Windows headers.

## Your plugin needs F4SEPlugin_Query on the OG runtime

The `commonlibf4.plugin` rule from commonlibf4-nomad generates an `F4SEPlugin_Version` struct for you. That is the Next-Gen style, and **on the OG runtime (1.10.163, F4SE 0.6.23) it is not enough**: F4SE uses the older `Query` + `Load` interface and ignores the version struct.

A plugin exporting only `Load` is rejected at startup with:

```
UIRepoSample_F4.dll does not appear to be an F4SE plugin
```

which names no symbol and looks nothing like a missing export. See `samples/f4se-plugin/src/main.cpp` for the query implementation to copy.

## Getting the API

```cpp
#include "uirepo/NativeApiLoader.h"

const auto api = uirepo::sdk::loadNativeApi();
if (!api) {
    logger::warn("UIRepo unavailable: {}", uirepo::sdk::describe(api.status()));
    return;
}
```

The loader is convenience only. It is written purely against the C ABI, and resolving `UIRepo_GetNativeApi` yourself costs you nothing. It exists because three mistakes are easy to make by hand and all three fail at runtime rather than at load: calling `LoadLibrary` on a module the host already loaded, assuming a non-null table means a feature is present, and forgetting that a newer framework returns a larger table.

## Optional C++ wrapper

`NativeApiCpp.h` is a second convenience layer over the same public C table. It adds no privileged access and no link dependency.

```cpp
#include "uirepo/NativeApiCpp.h"

uirepo::sdk::native::Client client(api.table());
auto documentResult = client.createDocument("inventory");
if (!documentResult) {
    logger::warn("createDocument failed: {}", client.describe(documentResult.code()));
    return;
}

auto document = std::move(documentResult).value();
auto root = client.root(document.handle());
```

The wrapper is deliberately non-throwing. Calls return `Result<T>` and capture `getLastResult()` immediately on failure when result codes are available. Documents and offscreen surfaces are move-only RAII objects and release their UIRepo ownership on destruction. `DocumentHandle`, `NodeHandle`, and `SurfaceHandle` are different C++ types, so a node cannot be passed accidentally where a surface is required.

Versioned structs are repaired before crossing the ABI. This is valid:

```cpp
UIRepoStyleV1 style{}; // size may remain zero here
style.opacity = 1.0f;
auto styled = client.setStyle(document.handle(), panel, style);
```

The wrapper sets `style.size = sizeof(style)` before the call. Factory helpers such as `makeStyle()`, `makeTextLayout()`, `makeFocusRequirements()`, and `makeSurfaceDesc()` are also available when you want a correctly initialized struct before populating it.

The wrapper is optional and not itself ABI-frozen. The flat C table remains the compatibility boundary.

## Provider health

Provider presence and renderer readiness are separate.

```cpp
switch (api.health()) {
case uirepo::sdk::ProviderHealth::Unavailable:
    return; // no compatible API table
case uirepo::sdk::ProviderHealth::Degraded:
    // UIRepo is loaded, but rendering is temporarily unavailable.
    // Keep retained handles and retry renderer-dependent work later.
    break;
case uirepo::sdk::ProviderHealth::Ready:
    break;
}
```

`Degraded` is normal during startup and renderer/device recovery. It does **not** invalidate document or node handles. Do not tear down and rebuild a retained interface just because renderer readiness temporarily drops. UIRepo owns renderer recreation; the consumer keeps its retained state.

`Api::health()` uses the existing V1 `isRendererReady()` entry instead of growing the ABI. A manually resolved C/C++ consumer can derive the same three-state contract: no accepted table means unavailable, a loaded table with `isRendererReady() == 0` means degraded, and nonzero means ready.

`health()` is an API call and can replace the per-thread `getLastResult()` value. If another call just failed, read `getLastResult()` before querying health. `NativeApiCpp.h` does that capture inside each failed wrapper call.

See [Provider health and renderer recovery](../docs/provider-health.md).

## When to call

Timing is the most common source of bugs.

| What | When |
|---|---|
| `loadNativeApi()` | after the framework plugin has loaded, normally from an F4SE message handler rather than your initial `Load` body |
| `loadNativeApi(L"UIRepo_F4.dll", UIREPO_NATIVE_API_VERSION_V2)` | same timing; use it only for the v2 state, enumeration and dispatch entries. A v1-only framework refuses it with `LoadStatus::VersionRefused`, so keep the default `loadNativeApi()` when you need nothing from v2 |
| document/node creation and retained mutations | any time after a compatible API table is available |
| `api.health()` / `isRendererReady()` | when deciding whether renderer-dependent work can run now |
| offscreen surface calls | on the render thread only; they fail closed elsewhere and return `UIREPO_RESULT_NOT_READY` while the renderer is unavailable |

There is no callback that tells you the renderer became ready. Keep retained state and poll on a later frame when renderer-dependent work is needed.

## Two rules that catch people

**Capability bits, not version numbers.** Test `api.has(UIREPO_CAP_...)` or `client.has(UIREPO_CAP_...)` before using an optional area. A capability can be compiled out or unavailable on a given runtime while the ABI version is unchanged, so inferring support from the version is wrong even when it happens to work.

**`size` is exact.** Every struct with a `size` field must have it set to `sizeof` that struct, and a mismatch is refused rather than tolerated. A published struct never changes shape. New fields arrive as a new struct and a new table entry, so `size` is a self-describing tag, not a negotiation. The C++ wrapper handles this automatically for the calls it wraps. See [ABI compatibility](../docs/abi-compatibility.md).

```cpp
UIRepoStyleV1 style{};
style.size = sizeof(style);   // required when using the C table directly
```

## Finding out why a call failed

Every C call reports success as 0/1 or a zero handle. When one fails, `getLastResult()` says why:

```cpp
const UIRepoSurfaceHandle surface = api->createOffscreenSurface(document, &desc);
if (surface == 0) {
    const uint32_t why = api->getLastResult();
    if (why == UIREPO_RESULT_NOT_READY) {
        return;                       // expected after load/recovery; retry later
    }
    logger::warn("offscreen surface refused: {}", api->describeResult(why));
}
```

The distinction matters. `UIREPO_RESULT_NOT_READY` means try again later; `UIREPO_RESULT_BUDGET_EXCEEDED` means this request will never succeed unless you ask for less; `UIREPO_RESULT_WRONG_THREAD` means the call site is wrong. Treating all three as "it failed" produces either a retry loop or a silently missing UI.

Three things to know:

- The value is **per-thread** and is overwritten by your next call on that thread. Capture it immediately after the failure, not later.
- `describeResult` never returns null, including for a code your header knows but this framework build does not. It is a log string, not a user-facing message.
- `UIREPO_RESULT_UNKNOWN` means the framework refused and did not classify the cause. It is honest rather than exhaustive because some causes are decided deeper than the ABI layer can see.

Guarded by `UIREPO_CAP_RESULT_CODES`; test the bit before relying on it. The C++ wrapper falls back to `UIREPO_RESULT_UNKNOWN` when the provider does not expose result codes.

## Compatibility

A plugin built against an older SDK keeps working against a newer framework: the function table only grows at the end, so the prefix you know stays valid. The reverse is refused cleanly at load. A plugin built against a newer SDK than the installed framework gets `LoadStatus::VersionRefused` rather than a crash on a missing entry.

## Documents are created visible

`createDocument` returns a visible document. If it is not meant to appear immediately, call `setDocumentVisible(document, 0)` or `client.setVisible(document.handle(), false)` before the next rendered frame. Offscreen rendering additionally *requires* the document to be hidden and input-inactive because rendering it at surface size would rewrite the hit-test geometry it is using on the flat HUD.

## Building the sample

From the framework repository:

```bash
bash scripts/package-sdk.sh
xmake build uirepo_sdk_sample
```

The sample builds against the staged package rather than the source tree, so anything missing from the package breaks the sample build instead of being quietly supplied by the repository.
