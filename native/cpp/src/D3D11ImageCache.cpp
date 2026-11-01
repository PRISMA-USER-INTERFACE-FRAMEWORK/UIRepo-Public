#include "D3D11ImageCache.h"

#ifdef _WIN32

#include "uirepo/ImagePolicy.h"

#include <Windows.h>
#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace uirepo {
namespace {

class ScopedComApartment final {
public:
    ScopedComApartment() noexcept {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(result)) {
            initialized_ = true;
            usable_ = true;
        } else if (result == RPC_E_CHANGED_MODE) {
            usable_ = true;
        }
    }

    ~ScopedComApartment() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool usable() const noexcept { return usable_; }

private:
    bool initialized_{};
    bool usable_{};
};

[[nodiscard]] std::wstring widenPath(std::string_view source) {
    if (source.empty() || source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        source.data(),
        static_cast<int>(source.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        source.data(),
        static_cast<int>(source.size()),
        wide.data(),
        required);
    return converted == required ? wide : std::wstring{};
}

[[nodiscard]] bool sourceIsKnownOpaque(
    IWICImagingFactory* factory,
    IWICBitmapFrameDecode* frame) noexcept {
    if (factory == nullptr || frame == nullptr) {
        return false;
    }

    WICPixelFormatGUID sourceFormat{};
    if (FAILED(frame->GetPixelFormat(&sourceFormat))) {
        return false;
    }

    // WIC defines these RGB/BGR formats without an alpha channel. Some compatible
    // implementations do not expose reliable IWICPixelFormatInfo2 transparency
    // metadata, so recognize the intrinsically opaque formats before consulting it.
    if (IsEqualGUID(sourceFormat, GUID_WICPixelFormat24bppRGB) ||
        IsEqualGUID(sourceFormat, GUID_WICPixelFormat24bppBGR) ||
        IsEqualGUID(sourceFormat, GUID_WICPixelFormat32bppBGR)) {
        return true;
    }

    Microsoft::WRL::ComPtr<IWICComponentInfo> componentInfo;
    if (FAILED(factory->CreateComponentInfo(sourceFormat, componentInfo.GetAddressOf())) || !componentInfo) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICPixelFormatInfo2> pixelInfo;
    if (FAILED(componentInfo.As(&pixelInfo)) || !pixelInfo) {
        return false;
    }

    BOOL supportsTransparency = TRUE;
    return SUCCEEDED(pixelInfo->SupportsTransparency(&supportsTransparency)) &&
           supportsTransparency == FALSE;
}

} // namespace

D3D11ImageCache::D3D11ImageCache(
    ID3D11Device* device,
    std::size_t maxBytes,
    std::size_t maxEntries,
    std::size_t maxSingleImageBytes)
    : device_(device),
      cache_(maxBytes, maxEntries),
      negativeCache_(DefaultNegativeBytes, DefaultNegativeEntries),
      maxSingleImageBytes_(maxSingleImageBytes) {}

void D3D11ImageCache::rememberFailure(std::string_view source) noexcept {
    ++decodeFailures_;
    try {
        std::string key(source);
        const std::size_t bytes = std::max<std::size_t>(1, key.size() + 1);
        (void)negativeCache_.insert(std::move(key), std::uint8_t{1}, bytes);
    } catch (...) {
    }
}

D3D11ImageCache::ResolveResult D3D11ImageCache::resolve(
    std::string_view source,
    BeforeUpload beforeUpload,
    void* beforeUploadContext) noexcept {
    if (!device_ || source.empty()) {
        return {};
    }

    try {
        std::string key(source);
        if (auto* cached = cache_.find(key); cached != nullptr) {
            return {.texture = *cached, .success = true};
        }
        if (negativeCache_.find(key) != nullptr) {
            ++negativeHits_;
            return {};
        }

        Texture texture{};
        std::size_t bytes{};
        bool aborted = false;
        if (!decodeAndUpload(source, texture, bytes, beforeUpload, beforeUploadContext, aborted)) {
            if (!aborted) {
                rememberFailure(source);
            }
            return {};
        }

        ResolveResult result{.texture = texture, .success = true};
        (void)cache_.insert(std::move(key), std::move(texture), bytes);
        return result;
    } catch (...) {
        rememberFailure(source);
        return {};
    }
}

bool D3D11ImageCache::decodeAndUpload(
    std::string_view source,
    Texture& texture,
    std::size_t& bytes,
    BeforeUpload beforeUpload,
    void* beforeUploadContext,
    bool& aborted) noexcept {
    texture = {};
    bytes = 0;
    aborted = false;

    const std::wstring path = widenPath(source);
    if (path.empty()) {
        return false;
    }

    ScopedComApartment apartment;
    if (!apartment.usable()) {
        return false;
    }

    try {
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(result) || !factory) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        result = factory->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            decoder.GetAddressOf());
        if (FAILED(result) || !decoder) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(result) || !frame) {
            return false;
        }

        UINT width{};
        UINT height{};
        result = frame->GetSize(&width, &height);
        if (FAILED(result)) {
            return false;
        }

        const auto admission = render::admitRgba8Image(width, height);
        if (!admission.accepted || admission.bytes > static_cast<std::size_t>(UINT32_MAX)) {
            return false;
        }
        if (maxSingleImageBytes_ != 0 && admission.bytes > maxSingleImageBytes_) {
            ++oversizeRejections_;
            return false;
        }

        const bool opaqueSource = sourceIsKnownOpaque(factory.Get(), frame.Get());

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        result = factory->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(result) || !converter) {
            return false;
        }

        result = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(result)) {
            return false;
        }

        const std::size_t strideSize = static_cast<std::size_t>(width) * render::Rgba8BytesPerPixel;
        if (strideSize > UINT32_MAX) {
            return false;
        }
        const UINT stride = static_cast<UINT>(strideSize);

        std::vector<std::uint8_t> pixels(admission.bytes);
        result = converter->CopyPixels(
            nullptr,
            stride,
            static_cast<UINT>(pixels.size()),
            pixels.data());
        if (FAILED(result)) {
            return false;
        }

        if (opaqueSource) {
            for (std::size_t alpha = 3; alpha < pixels.size(); alpha += render::Rgba8BytesPerPixel) {
                pixels[alpha] = 255;
            }
        }

        if (beforeUpload != nullptr && !beforeUpload(admission.bytes, beforeUploadContext)) {
            aborted = true;
            return false;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = pixels.data();
        initialData.SysMemPitch = stride;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> uploaded;
        result = device_->CreateTexture2D(&description, &initialData, uploaded.GetAddressOf());
        if (FAILED(result) || !uploaded) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource;
        result = device_->CreateShaderResourceView(uploaded.Get(), nullptr, resource.GetAddressOf());
        if (FAILED(result) || !resource) {
            return false;
        }

        texture.resource = std::move(resource);
        texture.width = width;
        texture.height = height;
        bytes = admission.bytes;
        ++uploads_;
        return true;
    } catch (...) {
        texture = {};
        bytes = 0;
        return false;
    }
}

} // namespace uirepo

#endif