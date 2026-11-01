#include "uirepo/BoundedCache.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <atomic>
#include <unknwn.h>
#include <wrl/client.h>
#endif

namespace {

using uirepo::render::BoundedLruCache;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testHitMissAndHardByteBudget() {
    BoundedLruCache<std::string, int> cache(10, 8);

    require(cache.find("missing") == nullptr, "cache miss returned a value");
    require(cache.stats().misses == 1, "cache miss telemetry was not recorded");

    require(cache.insert("a", 1, 6), "first cache insert failed");
    require(cache.insert("b", 2, 4), "second cache insert failed");
    require(cache.stats().bytes == 10 && cache.stats().entries == 2, "cache accounting did not reach its byte budget");

    const int* value = cache.find("a");
    require(value != nullptr && *value == 1, "cache hit returned the wrong value");
    require(cache.stats().hits == 1, "cache hit telemetry was not recorded");

    require(cache.insert("c", 3, 5), "budgeted insertion after eviction failed");
    require(cache.stats().bytes <= cache.maxBytes(), "cache exceeded its hard byte budget");
    require(cache.stats().entries == 1, "byte pressure did not evict enough entries");
    require(cache.find("c") != nullptr, "new entry disappeared after budget eviction");
    require(cache.stats().evictions == 2, "byte-budget eviction telemetry is wrong");
}

void testLruTouchControlsVictim() {
    BoundedLruCache<std::string, int> cache(100, 2);
    require(cache.insert("a", 1, 1), "could not insert LRU a");
    require(cache.insert("b", 2, 1), "could not insert LRU b");

    require(cache.find("a") != nullptr, "could not touch LRU a");
    require(cache.insert("c", 3, 1), "could not insert LRU c");

    require(cache.find("a") != nullptr, "recently touched entry was evicted");
    require(cache.find("b") == nullptr, "least-recently-used entry survived eviction");
    require(cache.find("c") != nullptr, "new LRU entry is missing");
}

void testReplacementAndOversizedRejection() {
    BoundedLruCache<std::string, int> cache(16, 4);
    require(cache.insert("value", 1, 4), "initial replacement entry failed");
    require(cache.insert("value", 2, 12), "replacement entry failed");
    require(cache.stats().entries == 1 && cache.stats().bytes == 12, "replacement double-counted cache resources");
    require(*cache.find("value") == 2, "replacement kept the stale value");

    const auto beforeBytes = cache.stats().bytes;
    require(!cache.insert("too-large", 3, 17), "oversized resource bypassed the hard byte budget");
    require(cache.stats().bytes == beforeBytes, "oversized rejection changed live byte accounting");
    require(cache.stats().rejections == 1, "oversized rejection telemetry is wrong");
}

void testEvictionReleasesOwnedResource() {
    BoundedLruCache<int, std::shared_ptr<int>> cache(8, 1);
    auto first = std::make_shared<int>(7);
    std::weak_ptr<int> firstLifetime = first;

    require(cache.insert(1, first, 4), "could not insert owned resource");
    first.reset();
    require(!firstLifetime.expired(), "cache did not retain inserted resource");

    require(cache.insert(2, std::make_shared<int>(8), 4), "could not evict owned resource");
    require(firstLifetime.expired(), "eviction did not release owned resource");

    cache.clear();
    require(cache.stats().entries == 0 && cache.stats().bytes == 0, "clear left live cache accounting behind");
}

void testBareMoveOnlyValueRoundtrips() {
    BoundedLruCache<int, std::unique_ptr<int>> cache(1024, 8);
    auto owned = std::make_unique<int>(42);
    const int* raw = owned.get();
    require(cache.insert(1, std::move(owned), 16), "move-only value insert failed");

    auto* found = cache.find(1);
    require(found != nullptr, "move-only value entry not found");
    require(found->get() == raw && **found == 42, "bare move-only value was not stored intact");

    auto* again = cache.find(1);
    require(again != nullptr && again->get() == raw, "second find lost the stored move-only value");
}

struct ReleasingAddressOf {
    int payload{};
    int addressOfCalls{};
    // Mirrors WRL::ComPtr::operator& (releasing the held pointer as a side effect):
    // a correct container must take the stored value's address WITHOUT invoking this.
    ReleasingAddressOf* operator&() noexcept {
        ++addressOfCalls;
        payload = 0;
        return this;
    }
};

void testValueWithOverloadedAddressOfSurvivesFind() {
    BoundedLruCache<int, ReleasingAddressOf> cache(1024, 8);
    require(cache.insert(1, ReleasingAddressOf{.payload = 99}, 16), "overloaded-address-of insert failed");

    auto* found = cache.find(1);
    require(found != nullptr, "overloaded-address-of entry not found");
    require(found->addressOfCalls == 0, "find invoked the value's overloaded operator&");
    require(found->payload == 99, "find corrupted the stored value via its overloaded operator&");
}

struct ConstReleasingAddressOf {
    int payload{};
    mutable int addressOfCalls{};
    // const operator& (exercises the const find() overload; ComPtr's operator& is non-const).
    const ConstReleasingAddressOf* operator&() const noexcept {
        ++addressOfCalls;
        return this;
    }
};

void testConstFindDoesNotInvokeOverloadedAddressOf() {
    BoundedLruCache<int, ConstReleasingAddressOf> cache(1024, 8);
    require(cache.insert(1, ConstReleasingAddressOf{.payload = 77}, 16), "const-find insert failed");

    const auto& constCache = cache;
    const auto* found = constCache.find(1);
    require(found != nullptr, "const find did not locate the entry");
    require(found->payload == 77, "const find returned the wrong entry");
    require(found->addressOfCalls == 0, "const find invoked the value's overloaded operator&");
}

#ifdef _WIN32
struct DummyUnknown : IUnknown {
    std::atomic<ULONG> refs{1};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override {
        if (object != nullptr) {
            *object = nullptr;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --refs;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }
};

void testComPtrValueRoundtrips() {
    using Microsoft::WRL::ComPtr;
    BoundedLruCache<int, ComPtr<IUnknown>> cache(1024, 8);
    ComPtr<IUnknown> object;
    object.Attach(new DummyUnknown());
    IUnknown* const raw = object.Get();
    require(cache.insert(1, object, 16), "ComPtr value insert failed");

    auto* found = cache.find(1);
    require(found != nullptr, "ComPtr value entry not found");
    require(found->Get() == raw, "find nulled the stored ComPtr via its overloaded operator&");

    auto* again = cache.find(1);
    require(again != nullptr && again->Get() == raw, "second find lost the stored ComPtr");
}
#endif

} // namespace

int main() {
    testHitMissAndHardByteBudget();
    testLruTouchControlsVictim();
    testReplacementAndOversizedRejection();
    testEvictionReleasesOwnedResource();
    testBareMoveOnlyValueRoundtrips();
    testValueWithOverloadedAddressOfSurvivesFind();
    testConstFindDoesNotInvokeOverloadedAddressOf();
#ifdef _WIN32
    testComPtrValueRoundtrips();
#endif
    std::cout << "UIRepo bounded renderer cache regressions passed\n";
    return 0;
}
