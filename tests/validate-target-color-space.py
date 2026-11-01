#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / "native/cpp/include/uirepo/D3D11Renderer.h").read_text(encoding="utf-8")
runtime = (root / "native/fo4/src/Runtime.cpp").read_text(encoding="utf-8")
xmake = (root / "xmake.lua").read_text(encoding="utf-8")
windows_ci = (root / "scripts/ci-windows-stage0.ps1").read_text(encoding="utf-8")
xwin_ci = (root / "scripts/ci-xwin-build.sh").read_text(encoding="utf-8")
renderer_cpp = (root / "native/cpp/src/D3D11Renderer.cpp").read_text(encoding="utf-8")

required_header = (
    "enum class D3D11TargetColorSpace",
    "DXGI_FORMAT targetFormat() const noexcept",
    "target_->GetDesc(&description)",
    "classifyTargetColorSpace",
    "DXGI_FORMAT_R8G8B8A8_UNORM",
    "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB",
    "DXGI_FORMAT_B8G8R8A8_UNORM",
    "DXGI_FORMAT_B8G8R8A8_UNORM_SRGB",
)
for token in required_header:
    assert token in header, f"missing target color-space renderer contract: {token}"

assert "resource format {}, rtv format {}, color-space {}" in runtime
assert runtime.count("targetColorSpaceName(colorSpace)") == 2
assert "CreateRenderTargetView" in runtime

for content, label in ((xmake, "xmake"), (windows_ci, "native Windows CI"), (xwin_ci, "xwin CI")):
    assert "uirepo_d3d11_target_color_space_tests" in content, f"missing target color-space test in {label}"

# Slice A is observational only. Output-changing transfer/correction logic belongs
# to Slice B after the real Fallout target format is measured.
assert "UNORM_SRGB" not in renderer_cpp
assert "LinearToSrgb" not in renderer_cpp
assert "SrgbToLinear" not in renderer_cpp

print("target color-space diagnostics contract OK")
