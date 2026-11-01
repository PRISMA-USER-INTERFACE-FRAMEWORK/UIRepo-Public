#include "uirepo/DispatchQueue.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testCancelWaitsForExecutingOwner() {
    using namespace std::chrono_literals;
    using namespace uirepo::dispatch;

    Queue queue;
    queue.attach();

    require(queue.tryEnqueue([] {}, 77).accepted, "could not enqueue owner callback");
    queue.beginDrain();
    auto task = queue.takeNext();
    require(task.has_value() && task->owner == 77, "owner callback did not enter executing state");
    require(queue.isExecuting(77), "executing owner was not tracked");

    queue.cancelOwner(77);
    require(queue.isCancelled(77), "cancelled owner tombstone was not retained");
    require(
        !queue.waitUntilNotExecutingFor(77, 1ms),
        "owner cancellation reported safe while callback was still in flight");

    queue.finishExecuting();
    queue.endDrain();
    require(
        queue.waitUntilNotExecutingFor(77, 10ms),
        "owner did not become safe after executing callback completed");
}

void testCancelRemovesQueuedOwnerWorkOnly() {
    using namespace uirepo::dispatch;

    Queue queue;
    queue.attach();

    require(queue.tryEnqueue([] {}, 11).accepted, "could not enqueue first owner");
    require(queue.tryEnqueue([] {}, 22).accepted, "could not enqueue second owner");
    require(queue.tryEnqueue([] {}, 11).accepted, "could not enqueue repeated owner");

    queue.cancelOwner(11);
    queue.beginDrain();
    auto task = queue.takeNext();
    require(task.has_value() && task->owner == 22, "cancel removed unrelated owner work or left cancelled work queued");
    queue.finishExecuting();
    require(!queue.takeNext().has_value(), "cancelled owner work survived queue pruning");
    queue.endDrain();
}

void testDetachInvalidatesQueuedGeneration() {
    using namespace uirepo::dispatch;

    Queue queue;
    queue.attach();
    const auto generation = queue.generation();
    require(queue.tryEnqueue([] {}, 44).accepted, "could not enqueue pre-detach work");

    queue.detach();
    queue.attach();
    require(queue.generation() != generation, "dispatcher generation did not change across detach/attach");

    queue.beginDrain();
    require(!queue.takeNext().has_value(), "stale callback survived dispatcher generation change");
    queue.endDrain();
}

} // namespace

int main() {
    testCancelWaitsForExecutingOwner();
    testCancelRemovesQueuedOwnerWorkOnly();
    testDetachInvalidatesQueuedGeneration();
    std::cout << "UIRepo dispatch lifecycle regressions passed\n";
    return 0;
}
