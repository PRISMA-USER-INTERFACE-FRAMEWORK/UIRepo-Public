/* Minimal UIRepo consumer.

   Built entirely against the public SDK. It does not link the framework - the
   API is resolved at runtime from the already-loaded plugin - so this DLL has
   no build-time dependency on UIRepo at all.

   This sample deliberately uses the optional NativeApiCpp.h convenience layer
   after loading the flat ABI. The in-game F4SE sample continues to use the C
   table directly, so CI exercises both supported consumption styles.

   This is not an F4SE plugin. It exports one function and leaves the F4SE
   wiring to the consumer. */

#include "uirepo/NativeApiLoader.h"
#include "uirepo/NativeApiCpp.h"

#include <windows.h>

#include <cstdio>
#include <utility>

namespace {

using uirepo::sdk::Api;
using uirepo::sdk::native::Client;
using uirepo::sdk::native::Document;
using uirepo::sdk::native::Surface;

void log(const char* message) noexcept {
    std::printf("[uirepo-sample] %s\n", message);
}

void logFailure(const Client& client, const char* operation, std::uint32_t code) noexcept {
    std::printf(
        "[uirepo-sample] %s failed (%u): %s\n",
        operation,
        code,
        client.describe(code));
}

/* Builds a small hidden document and, if the framework offers offscreen
   surfaces, renders it once into a texture the framework owns. */
[[nodiscard]] bool buildDocument(const Api& loadedApi) noexcept {
    Client client(loadedApi.table());
    if (!client) {
        log("C++ wrapper refused the API table");
        return false;
    }

    auto documentResult = client.createDocument("sample-root");
    if (!documentResult) {
        logFailure(client, "createDocument", documentResult.code());
        return false;
    }
    Document document = std::move(documentResult).value();

    /* Documents are created visible. Anything that is not meant to appear on
       the flat HUD immediately should say so before the next frame. */
    auto hidden = client.setVisible(document.handle(), false);
    if (!hidden) {
        logFailure(client, "setVisible", hidden.code());
        return false;
    }

    auto rootResult = client.root(document.handle());
    if (!rootResult) {
        logFailure(client, "root", rootResult.code());
        return false;
    }

    auto panelResult = client.createPanel(document.handle(), rootResult.value(), "sample-panel");
    if (!panelResult) {
        logFailure(client, "createPanel", panelResult.code());
        return false;
    }

    /* size is intentionally left zero here. Client::setStyle repairs it before
       crossing the ABI, so a consumer cannot forget the exact-size rule. */
    UIRepoStyleV1 style{};
    style.width.kind = UIREPO_LENGTH_PERCENT;
    style.width.value = 100.0f;
    style.height.kind = UIREPO_LENGTH_PIXELS;
    style.height.value = 64.0f;
    style.padding = UIRepoThicknessV1{8.0f, 8.0f, 8.0f, 8.0f};
    style.background = UIRepoColorV1{16, 18, 22, 220};
    style.opacity = 1.0f;

    auto styled = client.setStyle(document.handle(), panelResult.value(), style);
    if (!styled) {
        logFailure(client, "setStyle", styled.code());
        return false;
    }

    auto textResult = client.createText(
        document.handle(),
        panelResult.value(),
        "sample-label",
        "Hello from the C++ SDK wrapper");
    if (!textResult) {
        logFailure(client, "createText", textResult.code());
        return false;
    }

    /* Offscreen surfaces are optional. Test the capability bit rather than
       assuming the framework version implies it. */
    if (client.has(UIREPO_CAP_OFFSCREEN_SURFACES)) {
        auto surfaceResult = client.createSurface(
            document.handle(),
            256,
            128,
            UIRepoColorV1{0, 0, 0, 0});
        if (!surfaceResult) {
            /* Not fatal: the renderer may be recovering or a budget may have
               refused the request. The retained document remains valid. */
            logFailure(client, "createSurface", surfaceResult.code());
        } else {
            Surface surface = std::move(surfaceResult).value();
            auto rendered = client.renderToSurface(document.handle(), surface.handle());
            if (!rendered) {
                logFailure(client, "renderToSurface", rendered.code());
            }

            auto info = client.surfaceInfo(surface.handle());
            if (info) {
                std::printf(
                    "[uirepo-sample] surface %ux%u generation %llu\n",
                    info.value().width,
                    info.value().height,
                    static_cast<unsigned long long>(info.value().generation));
            } else {
                logFailure(client, "surfaceInfo", info.code());
            }

            /* Surface and document are released automatically on scope exit. */
        }
    } else {
        log("framework does not offer offscreen surfaces");
    }

    return true;
}

} // namespace

/* Call this from an F4SE message handler, not from plugin load. See README.md. */
extern "C" __declspec(dllexport) int UIRepoSample_Run() noexcept {
    const Api api = uirepo::sdk::loadNativeApi();
    if (!api) {
        log(uirepo::sdk::describe(api.status()));
        return 0;
    }

    std::printf(
        "[uirepo-sample] framework ABI v%u, capabilities 0x%llx\n",
        api.providerVersion(),
        static_cast<unsigned long long>(api->capabilities));

    if (api.health() != uirepo::sdk::ProviderHealth::Ready) {
        log("renderer is not ready yet; keep retained state and retry renderer work later");
        return 0;
    }

    return buildDocument(api) ? 1 : 0;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    return TRUE;
}
