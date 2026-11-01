/* A framework that implements exactly ABI v1 - the current published one. */

#include "StubProvider.h"

namespace {

UIRepoNativeApiV1 makeTable() noexcept {
    UIRepoNativeApiV1 api{};
    api.size = sizeof(UIRepoNativeApiV1);
    api.abiVersion = 1u;
    abi_harness::stub::fillV1(api);
    return api;
}

const UIRepoNativeApiV1 kTable = makeTable();

} // namespace

/* No explicit dllexport here: NativeApi.h already declares this symbol, and
   UIREPO_NATIVE_API_BUILD makes that declaration the exported one. Adding a
   second __declspec would be a linkage mismatch, which MSVC rejects. */
extern "C" const UIRepoNativeApiV1* UIRepo_GetNativeApi(uint32_t requestedVersion) noexcept {
    // Same negotiation rule as the real framework.
    if (requestedVersion == 0 || requestedVersion > 1u) {
        return nullptr;
    }
    return &kTable;
}
