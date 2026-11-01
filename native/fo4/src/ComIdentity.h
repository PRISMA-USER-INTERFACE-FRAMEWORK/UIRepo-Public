#pragma once

/* COM object identity.

   Two interface pointers name the same object only when
   QueryInterface(IID_IUnknown) on both yields the same pointer. Comparing
   derived interface pointers directly - ID3D11Device* against ID3D11Device*,
   say - happens to work on Microsoft's D3D11, whose GetDevice returns the very
   pointer the application holds. It is not guaranteed by COM, and it does not
   hold everywhere: under DXVK the interface the engine stored and the one
   GetDevice returns can be different slices of the same device, so a raw
   comparison reports a mismatch that is not real.

   This is its own header because the failure it prevents is invisible on
   Microsoft's implementation, which means the regression that covers it has to
   construct the situation deliberately. */

#ifdef _WIN32

#include <unknwn.h>
#include <wrl/client.h>

namespace uirepo::fo4 {

[[nodiscard]] inline bool sameComObject(IUnknown* left, IUnknown* right) noexcept {
    if (left == right) {
        return true;
    }
    if (left == nullptr || right == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<IUnknown> leftIdentity;
    Microsoft::WRL::ComPtr<IUnknown> rightIdentity;
    if (FAILED(left->QueryInterface(IID_PPV_ARGS(leftIdentity.GetAddressOf()))) ||
        FAILED(right->QueryInterface(IID_PPV_ARGS(rightIdentity.GetAddressOf())))) {
        return false;
    }
    return leftIdentity.Get() == rightIdentity.Get();
}

} // namespace uirepo::fo4

#endif
