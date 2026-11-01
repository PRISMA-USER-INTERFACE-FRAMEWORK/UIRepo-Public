#include "uirepo/FrameGenerationPolicy.h"
#include "uirepo/ImagePolicy.h"

#include <cstdlib>
#include <iostream>

namespace {

using uirepo::render::MaxImageDimension;
using uirepo::render::admitRgba8Image;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testValidImageAccounting() {
    const auto admission = admitRgba8Image(1920, 1080);
    require(admission.accepted, "valid RGBA8 image was rejected");
    require(admission.bytes == 1920u * 1080u * 4u, "RGBA8 byte accounting is wrong");
}

void testZeroDimensionsRejected() {
    require(!admitRgba8Image(0, 64).accepted, "zero width was accepted");
    require(!admitRgba8Image(64, 0).accepted, "zero height was accepted");
}

void testDimensionCapRejected() {
    require(
        !admitRgba8Image(MaxImageDimension + 1u, 1u).accepted,
        "width above framework image cap was accepted");
    require(
        !admitRgba8Image(1u, MaxImageDimension + 1u).accepted,
        "height above framework image cap was accepted");
}

void testCustomCap() {
    require(admitRgba8Image(256, 256, 256).accepted, "image at custom dimension cap was rejected");
    require(!admitRgba8Image(257, 256, 256).accepted, "image above custom dimension cap was accepted");
    require(!admitRgba8Image(1, 1, 0).accepted, "zero custom dimension cap was accepted");
}

void testFrameGenerationBorrowedBackbufferPolicy() {
    using uirepo::render::isKnownFrameGenerationModule;

    require(
        isKnownFrameGenerationModule("AAAFrameGeneration.dll"),
        "AAA frame-generation proxy must use borrowed-backbuffer policy");
    require(
        isKnownFrameGenerationModule("amd_fidelityfx_dx12.dll"),
        "AMD frame-generation proxy must use borrowed-backbuffer policy");
    require(
        isKnownFrameGenerationModule("nvngx_dlssg.dll"),
        "DLSS frame-generation proxy must use borrowed-backbuffer policy");
    require(
        isKnownFrameGenerationModule("dlssg_to_fsr3.dll"),
        "DLSSG-to-FSR3 proxy must use borrowed-backbuffer policy");
    require(
        isKnownFrameGenerationModule("NVNGX_DLSSG.DLL"),
        "frame-generation module matching must remain ASCII case-insensitive");
    require(
        !isKnownFrameGenerationModule("dxgi.dll"),
        "normal DXGI must retain standard COM ownership");
    require(
        !isKnownFrameGenerationModule("nvngx_dlss.dll"),
        "non-frame-generation DLSS module must not be classified as borrowed");
    require(
        !isKnownFrameGenerationModule(""),
        "empty module name must not be classified as a frame-generation proxy");
}

} // namespace

int main() {
    testValidImageAccounting();
    testZeroDimensionsRejected();
    testDimensionCapRejected();
    testCustomCap();
    testFrameGenerationBorrowedBackbufferPolicy();
    std::cout << "UIRepo image/frame-generation resource policy regressions passed\n";
    return 0;
}
