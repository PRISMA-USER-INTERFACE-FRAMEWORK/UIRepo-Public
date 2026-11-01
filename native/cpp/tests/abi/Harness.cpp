/* Drives two independently built consumers against two providers.

   What this proves that a compile-time check cannot:

   - a consumer compiled separately from the provider can call through the C
     table, pass structs by pointer in both directions, and get the expected
     values back
   - a consumer built against ABI v1 keeps working, unrecompiled, against a
     provider that reports v2 and hands out a larger table
   - a consumer asking for a version the provider does not implement is refused
     cleanly rather than handed a table it would read off the end of
   - two consumers can share one process against one provider without
     interfering

   The providers are stubs, so this says nothing about the framework's
   behaviour. It is entirely about the boundary. */

#include "AbiHarness.h"
#include "uirepo/NativeApiLoader.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
using Module = HMODULE;
#else
#include <dlfcn.h>
using Module = void*;
#endif

namespace {

[[noreturn]] void fail(const std::string& message) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    std::exit(1);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

uint8_t rendererNotReady() noexcept {
    return 0;
}

Module openModule(const char* path) {
#ifdef _WIN32
    return ::LoadLibraryA(path);
#else
    return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void* findSymbol(Module module, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(::GetProcAddress(module, name));
#else
    return ::dlsym(module, name);
#endif
}

UIRepoResolverFn loadProvider(const char* path) {
    Module module = openModule(path);
    require(module != nullptr, std::string("could not load provider ") + path);
    void* symbol = findSymbol(module, "UIRepo_GetNativeApi");
    require(symbol != nullptr, std::string("provider has no UIRepo_GetNativeApi: ") + path);
    return reinterpret_cast<UIRepoResolverFn>(symbol);
}

void* loadSymbol(const char* path, const char* name) {
    Module module = openModule(path);
    require(module != nullptr, std::string("could not load module ") + path);
    void* symbol = findSymbol(module, name);
    require(symbol != nullptr, std::string(path) + " has no " + name);
    return symbol;
}

UIRepoConsumerRunFn loadConsumer(const char* path) {
    return reinterpret_cast<UIRepoConsumerRunFn>(loadSymbol(path, "UIRepoConsumer_Run"));
}

void runConsumer(UIRepoConsumerRunFn consumer, UIRepoResolverFn provider, const char* label) {
    const int step = consumer(provider);
    if (step != 0) {
        fail(std::string(label) + " failed at step " + std::to_string(step));
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(
            stderr,
            "usage: %s <provider_v1> <provider_v2> <consumer_documents> <consumer_surfaces> <consumer_state>\n",
            argv[0]);
        return 2;
    }

    UIRepoResolverFn providerV1 = loadProvider(argv[1]);
    UIRepoResolverFn providerV2 = loadProvider(argv[2]);
    UIRepoConsumerRunFn documents = loadConsumer(argv[3]);
    UIRepoConsumerRunFn surfaces = loadConsumer(argv[4]);
    UIRepoConsumerRunFn state = loadConsumer(argv[5]);
    const auto runGameThreadQueue =
        reinterpret_cast<int (*)()>(loadSymbol(argv[2], "UIRepoStub_RunGameThreadQueue"));
    const auto verifyDispatched =
        reinterpret_cast<int (*)()>(loadSymbol(argv[5], "UIRepoConsumer_VerifyDispatched"));
    const auto expectRefused =
        reinterpret_cast<UIRepoConsumerRunFn>(loadSymbol(argv[5], "UIRepoConsumer_ExpectRefused"));

    // Baseline: both consumers against the provider they were built for.
    runConsumer(documents, providerV1, "documents consumer against v1 provider");
    runConsumer(surfaces, providerV1, "surfaces consumer against v1 provider");

    // The old-consumer/new-provider case. Neither consumer is recompiled.
    runConsumer(documents, providerV2, "documents consumer against v2 provider");
    runConsumer(surfaces, providerV2, "surfaces consumer against v2 provider");

    // Two consumers sharing one provider, interleaved, must not interfere.
    for (int round = 0; round < 8; ++round) {
        runConsumer(documents, providerV1, "interleaved documents consumer");
        runConsumer(surfaces, providerV2, "interleaved surfaces consumer");
    }

    runConsumer(state, providerV2, "state consumer against v2 provider");
    require(runGameThreadQueue() == 4, "the v2 provider did not queue every dispatched callback");
    require(verifyDispatched() == 0, "dispatched callbacks did not run in order on the game thread");
    require(runGameThreadQueue() == 0, "the game-thread queue replayed work");
    runConsumer(expectRefused, providerV1, "v2 consumer against v1-only provider");

    // A v2 table must be larger and must report v2 while staying usable as v1.
    {
        const UIRepoNativeApiV1* v1 = providerV1(UIREPO_NATIVE_API_VERSION_V1);
        const UIRepoNativeApiV1* v2 = providerV2(UIREPO_NATIVE_API_VERSION_V2);
        require(v1 != nullptr && v2 != nullptr, "providers refused the ABI version they implement");
        require(providerV2(UIREPO_NATIVE_API_VERSION_V1) == v2, "the v2 provider must serve one table to v1 and v2 requests");
        require(v1->abiVersion == 1u, "v1 provider did not report ABI version 1");
        require(v2->abiVersion == 2u, "v2 provider did not report ABI version 2");
        require(v2->size > v1->size, "v2 provider did not hand out a larger table");
        require(v1->size == sizeof(UIRepoNativeApiV1), "v1 table size does not match the published struct");
        require(v2->size == sizeof(UIRepoNativeApiV2), "v2 table size does not match the published struct");
        require(
            uirepo::sdk::Api::adoptV2(v2).status() == uirepo::sdk::LoadStatus::Ok,
            "a v2 consumer refused a v2 table");
        require(
            uirepo::sdk::Api::adoptV2(v1).status() == uirepo::sdk::LoadStatus::VersionRefused,
            "a v2 consumer accepted a v1-only table");
        require(
            uirepo::sdk::Api::adopt(v2).status() == uirepo::sdk::LoadStatus::Ok,
            "a v1 consumer refused a larger v2 table");
    }

    // Provider presence and renderer readiness are separate lifecycle facts.
    // A compatible table that is temporarily not render-ready is degraded,
    // not absent, so consumers retain their document/node handles and retry
    // renderer-dependent work later.
    {
        require(
            uirepo::sdk::Api{}.health() == uirepo::sdk::ProviderHealth::Unavailable,
            "an API with no table did not report provider unavailable");

        const UIRepoNativeApiV1* v1 = providerV1(UIREPO_NATIVE_API_VERSION_V1);
        const UIRepoNativeApiV1* v2 = providerV2(UIREPO_NATIVE_API_VERSION_V2);
        require(
            uirepo::sdk::Api::adopt(v1).health() == uirepo::sdk::ProviderHealth::Ready,
            "ready v1 provider did not report ready health");
        require(
            uirepo::sdk::Api::adopt(v2).health() == uirepo::sdk::ProviderHealth::Ready,
            "ready v2 provider did not report ready health");

        UIRepoNativeApiV1 degraded = *v1;
        degraded.isRendererReady = &rendererNotReady;
        require(
            uirepo::sdk::Api::adopt(&degraded).health() == uirepo::sdk::ProviderHealth::Degraded,
            "loaded provider with an unavailable renderer did not report degraded health");

        UIRepoNativeApiV1 missingProbe = *v1;
        missingProbe.isRendererReady = nullptr;
        require(
            uirepo::sdk::Api::adopt(&missingProbe).health() == uirepo::sdk::ProviderHealth::Degraded,
            "loaded provider with a missing readiness probe did not fail safe as degraded");
    }

    // A consumer newer than the provider is refused, not served a short table.
    {
        require(providerV1(2u) == nullptr, "v1 provider served a v2 request");
        require(providerV1(99u) == nullptr, "v1 provider served an absurd future request");
        require(providerV2(3u) == nullptr, "v2 provider served a v3 request");
        require(
            uirepo::sdk::Api::adopt(providerV1(2u)).status() == uirepo::sdk::LoadStatus::VersionRefused,
            "a refused negotiation did not report VersionRefused");
    }

    // Result codes are readable through the table by a module that did not
    // record them, from both the current and the future provider.
    {
        const UIRepoNativeApiV1* tables[] = {
            providerV1(UIREPO_NATIVE_API_VERSION_V1),
            providerV2(UIREPO_NATIVE_API_VERSION_V2),
        };
        for (const UIRepoNativeApiV1* table : tables) {
            require(table != nullptr, "provider refused the current ABI version");
            require(
                (table->capabilities & UIREPO_CAP_RESULT_CODES) != 0,
                "provider does not advertise result codes");
            require(table->getLastResult != nullptr, "getLastResult entry is missing");
            require(table->describeResult != nullptr, "describeResult entry is missing");

            const char* known = table->describeResult(UIREPO_RESULT_INVALID_HANDLE);
            require(known != nullptr && known[0] != '\0', "describeResult gave nothing for a known code");

            // A consumer built against a newer header may hand back a code the
            // provider has never heard of.
            const char* unknown = table->describeResult(0xDEADBEEFu);
            require(unknown != nullptr && unknown[0] != '\0', "describeResult gave nothing for an unknown code");
        }
    }

    // Version zero is never valid.
    require(providerV1(0u) == nullptr, "v1 provider accepted version zero");
    require(providerV2(0u) == nullptr, "v2 provider accepted version zero");

    // A table shorter than the consumer's own is refused rather than read.
    {
        UIRepoNativeApiV1 truncated{};
        truncated.size = sizeof(UIRepoNativeApiV1) - 8;
        truncated.abiVersion = 1u;
        require(
            uirepo::sdk::Api::adopt(&truncated).status() == uirepo::sdk::LoadStatus::TableTooSmall,
            "a short table was not refused");
    }

    std::printf("UIRepo ABI negotiation and multi-consumer regressions passed\n");
    return 0;
}
