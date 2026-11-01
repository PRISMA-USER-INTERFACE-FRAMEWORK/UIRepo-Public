#pragma once

/* Shared contract between the ABI harness, its stub providers, and its
   consumers.

   The point of this harness is that the consumers are compiled into separate
   binaries from the provider, exactly as a real plugin is. Nothing here is
   shared state or a common object file - the only thing crossing the boundary
   is the C table in NativeApi.h, which is what is under test. */

#include "uirepo/NativeApi.h"

#ifdef _WIN32
#define ABI_HARNESS_EXPORT extern "C" __declspec(dllexport)
#else
#define ABI_HARNESS_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/* The provider export, named as the real framework names it. */
using UIRepoResolverFn = const UIRepoNativeApiV1* (*)(uint32_t);

/* Every consumer exports this. It is handed a resolver rather than looking one
   up, so a single harness process can point two independently built consumers
   at the same provider - and at different providers - without either of them
   knowing the other exists.

   Returns 0 on success, or a non-zero step number identifying what failed. */
using UIRepoConsumerRunFn = int (*)(UIRepoResolverFn);

/* Values the stub providers return, so a consumer can prove a call actually
   crossed the boundary and came back with the right value rather than merely
   not crashing. */
namespace abi_harness {

inline constexpr UIRepoDocumentHandle kDocument = 0x0D0C0D0C00000011ull;
inline constexpr UIRepoNodeHandle kRootNode = 0x4E4F4445000000A1ull;
inline constexpr UIRepoNodeHandle kPanelNode = 0x4E4F4445000000B2ull;
inline constexpr UIRepoSurfaceHandle kSurface = 0x5355524600000031ull;

inline constexpr uint32_t kSurfaceWidth = 320;
inline constexpr uint32_t kSurfaceHeight = 200;
inline constexpr uint64_t kSurfaceGeneration = 7;

} // namespace abi_harness
