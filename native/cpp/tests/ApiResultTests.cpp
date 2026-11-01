#include "ApiResult.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

using namespace uirepo::fo4;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testRecordingAndClearing() {
    api::setResult(UIREPO_RESULT_OK);
    require(api::lastResult() == UIREPO_RESULT_OK, "initial result must be ok");

    require(api::refuse(UIREPO_RESULT_WRONG_THREAD) == 0, "refuse must yield the ABI false value");
    require(api::lastResult() == UIREPO_RESULT_WRONG_THREAD, "refuse did not record its cause");

    require(api::succeed() == 1, "succeed must yield the ABI true value");
    require(api::lastResult() == UIREPO_RESULT_OK, "success must clear a previously recorded failure");

    require(api::refuseHandle(UIREPO_RESULT_BUDGET_EXCEEDED) == 0, "refuseHandle must yield a zero handle");
    require(api::lastResult() == UIREPO_RESULT_BUDGET_EXCEEDED, "refuseHandle did not record its cause");

    require(api::report(true, UIREPO_RESULT_INVALID_HANDLE) == 1, "report(true) must yield true");
    require(api::lastResult() == UIREPO_RESULT_OK, "report(true) must record success");
    require(api::report(false, UIREPO_RESULT_INVALID_HANDLE) == 0, "report(false) must yield false");
    require(api::lastResult() == UIREPO_RESULT_INVALID_HANDLE, "report(false) did not record its cause");
}

void testDescribeIsAlwaysUsable() {
    for (std::uint32_t code = 0; code <= UIREPO_RESULT_OUT_OF_MEMORY; ++code) {
        const char* text = api::describeResult(code);
        require(text != nullptr, "describeResult returned null for a known code");
        require(std::strlen(text) > 0, "describeResult returned an empty string for a known code");
    }

    // A consumer built against a newer header can hand us a code this build
    // has never heard of. It must still get a usable string.
    for (const std::uint32_t code : {UIREPO_RESULT_OUT_OF_MEMORY + 1u, 4096u, 0xFFFFFFFFu}) {
        const char* text = api::describeResult(code);
        require(text != nullptr, "describeResult returned null for an unknown code");
        require(std::strlen(text) > 0, "describeResult returned an empty string for an unknown code");
    }

    require(std::strcmp(api::describeResult(UIREPO_RESULT_OK), "ok") == 0, "ok must describe as ok");
}

void testResultIsPerThread() {
    api::setResult(UIREPO_RESULT_STATE_CONFLICT);

    UIRepoResultV1 observedInThread = UIREPO_RESULT_UNKNOWN;
    std::thread worker([&] {
        // A fresh thread must not inherit another thread's failure.
        observedInThread = api::lastResult();
        api::setResult(UIREPO_RESULT_NOT_READY);
    });
    worker.join();

    require(observedInThread == UIREPO_RESULT_OK, "a new thread inherited another thread's result");
    require(
        api::lastResult() == UIREPO_RESULT_STATE_CONFLICT,
        "another thread's failure overwrote this thread's result");
}

} // namespace

int main() {
    testRecordingAndClearing();
    testDescribeIsAlwaysUsable();
    testResultIsPerThread();
    std::cout << "UIRepo API result-code regressions passed\n";
    return 0;
}
