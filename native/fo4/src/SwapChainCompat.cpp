#include "SwapChainCompat.h"

#include "uirepo/FrameGenerationPolicy.h"

#include <Windows.h>

namespace uirepo::fo4::SwapChainCompat {
namespace {

[[nodiscard]] std::string moduleNameForAddress(void* address) noexcept {
    if (address == nullptr) {
        return "<null>";
    }

    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),
            &module) || module == nullptr) {
        return "<unknown>";
    }

    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            module,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return "<unknown>";
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    const auto slash = buffer.find_last_of(L"\\/");
    const std::wstring_view basename = slash == std::wstring::npos
        ? std::wstring_view(buffer)
        : std::wstring_view(buffer).substr(slash + 1);

    if (basename.empty()) {
        return "<unknown>";
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        basename.data(),
        static_cast<int>(basename.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return "<unknown>";
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    const int converted = WideCharToMultiByte(
        CP_UTF8,
        0,
        basename.data(),
        static_cast<int>(basename.size()),
        result.data(),
        required,
        nullptr,
        nullptr);
    return converted == required ? result : std::string{"<unknown>"};
}

} // namespace

bool isKnownFrameGenerationModule(std::string_view moduleName) noexcept {
    return render::isKnownFrameGenerationModule(moduleName);
}

BackbufferPolicy classifyBackbuffer(IDXGISwapChain* swapChain) noexcept {
    if (swapChain == nullptr) {
        return {};
    }

    auto** vtable = *reinterpret_cast<void***>(swapChain);
    if (vtable == nullptr || vtable[9] == nullptr) {
        return {.borrowed = false, .getBufferOwner = "<invalid-vtable>"};
    }

    BackbufferPolicy policy;
    policy.getBufferOwner = moduleNameForAddress(vtable[9]);
    policy.borrowed = isKnownFrameGenerationModule(policy.getBufferOwner);
    return policy;
}

} // namespace uirepo::fo4::SwapChainCompat
