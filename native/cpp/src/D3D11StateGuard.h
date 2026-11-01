#pragma once

#include <array>
#include <cstddef>

#include <d3d11.h>
#include <wrl/client.h>

namespace uirepo {

class ScopedD3D11State final {
public:
    explicit ScopedD3D11State(ID3D11DeviceContext* context) noexcept;
    ~ScopedD3D11State() noexcept;

    ScopedD3D11State(const ScopedD3D11State&) = delete;
    ScopedD3D11State& operator=(const ScopedD3D11State&) = delete;

private:
    static constexpr std::size_t kMaximumClassInstances = 256;

    ID3D11DeviceContext* context_{};
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets_{};
    ID3D11DepthStencilView* depthStencil_{};

    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
    D3D11_PRIMITIVE_TOPOLOGY topology_{D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED};
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
    UINT vertexStride_{};
    UINT vertexOffset_{};
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer_;
    DXGI_FORMAT indexFormat_{DXGI_FORMAT_UNKNOWN};
    UINT indexOffset_{};

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> geometryShader_;
    Microsoft::WRL::ComPtr<ID3D11HullShader> hullShader_;
    Microsoft::WRL::ComPtr<ID3D11DomainShader> domainShader_;
    std::array<ID3D11ClassInstance*, kMaximumClassInstances> vertexClasses_{};
    std::array<ID3D11ClassInstance*, kMaximumClassInstances> pixelClasses_{};
    std::array<ID3D11ClassInstance*, kMaximumClassInstances> geometryClasses_{};
    std::array<ID3D11ClassInstance*, kMaximumClassInstances> hullClasses_{};
    std::array<ID3D11ClassInstance*, kMaximumClassInstances> domainClasses_{};
    UINT vertexClassCount_{};
    UINT pixelClassCount_{};
    UINT geometryClassCount_{};
    UINT hullClassCount_{};
    UINT domainClassCount_{};

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pixelResource_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> pixelSampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexConstantBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> pixelConstantBuffer_;

    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
    FLOAT blendFactor_[4]{};
    UINT sampleMask_{0xFFFFFFFFu};
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState_;
    UINT stencilReference_{};
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterState_;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports_{};
    std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors_{};
    UINT viewportCount_{};
    UINT scissorCount_{};
    Microsoft::WRL::ComPtr<ID3D11Predicate> predicate_;
    BOOL predicateValue_{FALSE};
};

} // namespace uirepo
