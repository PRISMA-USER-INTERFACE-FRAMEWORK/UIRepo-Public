#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "ABI compatibility contract failed: $*" >&2
    exit 1
}

header=native/cpp/include/uirepo/NativeApi.h
implementation=native/fo4/src/NativeApi.cpp
implementation_v2=native/fo4/src/NativeApiV2.cpp
implementation_files=(native/fo4/src/NativeApi*.cpp)
contract_tests=native/cpp/tests/NativeApiContractTests.cpp
loader=sdk/include/uirepo/NativeApiLoader.h

test -f "$header" || fail "public ABI header is missing"
test -f "$implementation" || fail "ABI implementation is missing"
test -f "$implementation_v2" || fail "ABI v2 implementation is missing"
test -f "$contract_tests" || fail "ABI contract regression is missing"
test -f "$loader" || fail "SDK loader is missing"

# A published struct never changes shape, so every entry point requires an
# exact size. A `<` comparison would silently accept a struct from a different
# published shape.
if grep -nE '(source|desc|out)->size *< *sizeof' "${implementation_files[@]}"; then
    fail "struct size checks must be exact; published structs never grow"
fi
if ! grep -qE '(source|desc|out)->size *!= *sizeof' "${implementation_files[@]}"; then
    fail "entry points taking a versioned struct must validate its exact size"
fi
grep -Fq 'out->size != sizeof(UIRepoDocumentStateV1)' "$implementation_v2" ||
    fail "getDocumentState must validate the exact size of UIRepoDocumentStateV1"

# An older consumer must keep working against a newer framework, and the only
# refusals are version zero and anything newer than this build provides.
grep -Fq 'requestedVersion > UIREPO_NATIVE_API_VERSION' "$implementation" ||
    fail "version negotiation must refuse only newer-than-supported consumers"
if grep -Fq 'requestedVersion != UIREPO_NATIVE_API_VERSION' "$implementation"; then
    fail "exact version matching locks out older consumers against a newer framework"
fi
if grep -Eq 'requestedVersion *(==|!=|<) *(1|UIREPO_NATIVE_API_VERSION_V1)' "$implementation"; then
    fail "an older requested version must receive the same table, not a special case"
fi
grep -Fq 'requestedVersion == 0' "$implementation" ||
    fail "version zero must be refused"
grep -Fq 'nativeApiV2().v1' "$implementation" ||
    fail "every accepted version must receive the v2 table, whose first member is the v1 table"
grep -Fq 'api.v1.size = sizeof(UIRepoNativeApiV2)' "$implementation_v2" ||
    fail "the v2 table must report its own size"
grep -Fq 'api.v1.abiVersion = UIREPO_NATIVE_API_VERSION_V2' "$implementation_v2" ||
    fail "the v2 table must report ABI version 2"

# The frozen-layout assertions are the actual enforcement of the model.
# The value lives only in the static_assert - repeating it here would mean
# editing the same constant twice on every legitimate append.
grep -Eq 'sizeof\(UIRepoNativeApiV1\) == [0-9]+' "$contract_tests" ||
    fail "the function table size must be frozen"
for symbol in UIRepoStyleV1 UIRepoKeyEventV1 UIRepoPointerEventV1 UIRepoFocusRequirementsV1 \
              UIRepoTextLayoutV1 UIRepoSurfaceDescV1 UIRepoSurfaceInfoV1 UIRepoSurfaceStatsV1; do
    grep -Fq "sizeof($symbol) ==" "$contract_tests" ||
        fail "published struct $symbol has no frozen-size assertion"
done

# Every table entry keeps its offset, which is what makes append-only safe.
for anchor in isRendererReady createDocument routePointerEvent setTextLayout \
              createOffscreenSurface getOffscreenSurfaceStats; do
    grep -Fq "offsetof(UIRepoNativeApiV1, $anchor) ==" "$contract_tests" ||
        fail "table entry $anchor has no frozen-offset assertion"
done

grep -Fq 'UIREPO_NATIVE_API_VERSION' "$header" || fail "public header must declare an ABI version"

# v2 extends the frozen v1 table; every new struct and entry needs a frozen assertion.
grep -Fq 'UIRepoNativeApiV1 v1;' "$header" || fail "UIRepoNativeApiV2 must begin with the unchanged v1 table"
grep -Fq 'sizeof(UIRepoNativeApiV2) ==' "$contract_tests" || fail "the v2 table size must be frozen"
grep -Fq 'sizeof(UIRepoDocumentStateV1) ==' "$contract_tests" || fail "published struct UIRepoDocumentStateV1 has no frozen-size assertion"
grep -Fq 'offsetof(UIRepoNativeApiV2, v1) == 0' "$contract_tests" || fail "the v1 prefix must stay at offset zero of v2"
for anchor in getDocumentState enumerateDocuments isGameThread dispatchToGameThread; do
    grep -Fq "offsetof(UIRepoNativeApiV2, $anchor) ==" "$contract_tests" ||
        fail "table entry $anchor has no frozen-offset assertion"
done
for field in size visible inputEnabled focused health role order ownerId generation; do
    grep -Fq "offsetof(UIRepoDocumentStateV1, $field) ==" "$contract_tests" ||
        fail "UIRepoDocumentStateV1.$field has no frozen-offset assertion"
done
for bit in DOCUMENT_STATE DOCUMENT_ENUMERATION GAME_THREAD_DISPATCH; do
    grep -Fq "UIREPO_CAP_$bit ==" "$contract_tests" || fail "capability UIREPO_CAP_$bit has no frozen-value assertion"
    grep -Fq "UIREPO_CAP_$bit" "$implementation_v2" || fail "capability UIREPO_CAP_$bit is not advertised by the v2 table"
done
# isGameThread is a pure query: recording a result would overwrite a pending failure.
if sed -n '/apiIsGameThread/,/^}/p' "$implementation_v2" | grep -Eq 'api::(setResult|refuse|succeed|report)'; then
    fail "isGameThread must not record a result"
fi

# Negotiation and cross-module struct passing are proven by execution, not by
# inspection: the harness must exist and must be wired into the portable job.
harness=native/cpp/tests/abi/Harness.cpp
test -f "$harness" || fail "ABI negotiation harness is missing"
test -f native/cpp/tests/abi/ProviderV2.cpp || fail "a newer-provider stub is required to test old-consumer/new-provider"
test -f native/cpp/tests/abi/ConsumerDocuments.cpp || fail "first independently built consumer is missing"
test -f native/cpp/tests/abi/ConsumerSurfaces.cpp || fail "second independently built consumer is missing"
grep -Fq 'run-abi-harness.sh' scripts/ci-native-core.sh || fail "the ABI harness must run in the portable job"
test -f native/cpp/tests/abi/ConsumerState.cpp || fail "the v2 consumer is missing"
grep -Fq 'sizeof(UIRepoNativeApiV2) > sizeof(UIRepoNativeApiV1)' native/cpp/tests/abi/ProviderV2.cpp ||
    fail "the newer-provider stub must actually hand out a larger table"
grep -Fq 'UIRepoNativeApiV2 api{}' native/cpp/tests/abi/ProviderV2.cpp ||
    fail "the newer-provider stub must hand out the real UIRepoNativeApiV2 layout"
grep -Fq 'UIREPO_NATIVE_API_VERSION_V1' native/cpp/tests/abi/ConsumerDocuments.cpp ||
    fail "the documents consumer must request v1, as a consumer built before v2 would"
grep -Fq 'UIREPO_NATIVE_API_VERSION_V1' native/cpp/tests/abi/ConsumerSurfaces.cpp ||
    fail "the surfaces consumer must request v1, as a consumer built before v2 would"
grep -Fq 'VersionRefused' native/cpp/tests/abi/ConsumerState.cpp ||
    fail "the v2 consumer must prove a v1-only provider is refused cleanly"
grep -Fq 'UIREPO_NATIVE_API_VERSION_V1' "$loader" ||
    fail "the loader must keep requesting v1 by default so v1 behaviour is unchanged"

# Provider health is intentionally derived from the frozen V1 contract rather
# than growing the table just to restate information it already contains. No
# accepted table means unavailable; an accepted table with renderer readiness
# false means degraded; readiness true means ready.
grep -Fq 'enum class ProviderHealth' "$loader" ||
    fail "the SDK loader must expose the provider health states"
grep -Fq 'ProviderHealth health() const noexcept' "$loader" ||
    fail "the SDK loader must derive provider health from the accepted table"
grep -Fq 'ProviderHealth::Unavailable' "$loader" ||
    fail "provider health must distinguish an unavailable provider"
grep -Fq 'ProviderHealth::Degraded' "$loader" ||
    fail "provider health must distinguish a loaded provider with no renderer"
grep -Fq 'ProviderHealth::Ready' "$loader" ||
    fail "provider health must distinguish a render-ready provider"
grep -Fq 'table_->isRendererReady()' "$loader" ||
    fail "provider health must use the existing V1 readiness contract"
grep -Fq 'ProviderHealth::Unavailable' "$harness" ||
    fail "the ABI harness must execute the unavailable provider-health path"
grep -Fq 'ProviderHealth::Degraded' "$harness" ||
    fail "the ABI harness must execute the degraded provider-health path"
grep -Fq 'ProviderHealth::Ready' "$harness" ||
    fail "the ABI harness must execute the ready provider-health path"

# Result codes. Consumers switch on these values, so they are as frozen as the
# structs: new codes append, existing ones never move.
result_header=native/fo4/src/ApiResult.h
test -f "$result_header" || fail "result-code storage is missing"
grep -Fq 'UIREPO_CAP_RESULT_CODES' "$header" || fail "result codes need a capability bit consumers can test"
grep -Fq 'getLastResult' "$header" || fail "public header must expose getLastResult"
grep -Fq 'describeResult' "$header" || fail "public header must expose describeResult"
grep -Fq 'UIREPO_RESULT_OK == 0' "$contract_tests" || fail "result-code values must be frozen"
grep -Fq 'UIREPO_RESULT_OUT_OF_MEMORY == 11' "$contract_tests" || fail "result-code values must be frozen"
grep -Fq 'UIREPO_CAP_RESULT_CODES' "$implementation" || fail "result-code capability is not advertised by the implementation"

# Per-thread, or one thread's failure becomes another thread's answer.
grep -Fq 'thread_local' "$result_header" || fail "last-result storage must be per-thread"

# describeResult is documented never to return null, including for a code from
# a newer consumer that this build has never heard of.
grep -Fq 'unrecognised result code' "$result_header" ||
    fail "describeResult must return a usable string for an unknown code"
grep -Fq 'uirepo_api_result_tests' scripts/ci-native-core.sh ||
    fail "result-code semantics must be exercised in the portable job"

# The storage lives in the provider module and is read by a separately
# compiled consumer. Only the harness covers that.
grep -Fq 'getLastResult' native/cpp/tests/abi/StubProvider.h ||
    fail "the harness providers must record result codes"
grep -Fq 'UIREPO_RESULT_INVALID_STRUCT_SIZE' native/cpp/tests/abi/ConsumerDocuments.cpp ||
    fail "a consumer must assert a classified refusal across the module boundary"
grep -Fq 'UIREPO_CAP_RESULT_CODES' native/cpp/tests/abi/Harness.cpp ||
    fail "the harness must check result codes against both providers"

echo "ABI compatibility contract passed"
