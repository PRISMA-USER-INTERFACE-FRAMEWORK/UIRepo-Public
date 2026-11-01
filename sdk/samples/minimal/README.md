# Minimal UIRepo consumer

A DLL that resolves the UIRepo API at runtime, builds a small hidden document,
and renders it once into an offscreen surface if the framework offers them.

It is built only against the packaged public headers:

- `include/uirepo/NativeApi.h`
- `include/uirepo/NativeApiLoader.h`
- `include/uirepo/NativeApiCpp.h`

No framework sources, no import library, no link-time dependency.

The sample loads the flat ABI with `NativeApiLoader.h`, then uses the optional
C++ wrapper for retained-object construction and ownership. The in-game F4SE
sample intentionally continues to call the C table directly, so CI covers both
consumption styles.

## Why it is not an F4SE plugin

The F4SE half, including plugin metadata, the message listener, and
CommonLibF4, has nothing to do with the UIRepo ABI. Requiring it would mean
this sample could only build where CommonLibF4 is present. It exports one
function instead:

```cpp
extern "C" __declspec(dllexport) int UIRepoSample_Run();
```

Call it from your own plugin once the framework is loaded:

```cpp
void MessageHandler(F4SE::MessagingInterface::Message* message) {
    if (message->type == F4SE::MessagingInterface::kGameDataReady) {
        UIRepoSample_Run();
    }
}
```

Do not call it from `F4SEPlugin_Load`. The framework plugin may not be loaded
yet, and renderer-dependent work may not be ready.

## What it demonstrates

- resolving the API and reporting why it failed
- using provider health instead of treating temporary renderer loss as absence
- wrapping the accepted table in `uirepo::sdk::native::Client`
- typed document, node, and surface handles
- automatic `size` repair before versioned structs cross the ABI
- `Result<T>` failure codes captured immediately after a failed call
- RAII cleanup for documents and offscreen surfaces
- testing capability bits instead of inferring support from the ABI version
- treating an unavailable offscreen surface as non-fatal
