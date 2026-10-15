#include "../Source/BOSS.hpp"
#include "../Source/Expression.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <variant>

namespace {
using boss::ComplexExpression;

constexpr std::int64_t defaultEntryPointResult = 1;

/// Lets a test hold one engine call (an evaluation of Pause, or a reset armed with
/// stubPauseNextReset) until the test calls stubReleasePausedCall, so that the test can check
/// what BootstrapEngine refuses while that call is running. Every wait ends after
/// pauseTimeout, so a test that fails part-way cannot block the test run indefinitely.
constexpr auto pauseTimeout = std::chrono::seconds(30);
std::mutex pauseMutex;
std::condition_variable pauseChanged;
bool callPaused = false;
bool pauseReleased = false;
bool pauseNextReset = false;
int resetCount = 0;
bool throwFromNextReset = false;

void pauseUntilReleased() {
  std::unique_lock<std::mutex> lock(pauseMutex);
  callPaused = true;
  pauseChanged.notify_all();
  pauseChanged.wait_for(lock, pauseTimeout, [] { return pauseReleased; });
  callPaused = false;
  pauseReleased = false;
}

/// Lets a test check that BootstrapEngine runs engine calls concurrently: each evaluation of
/// Rendezvous waits until concurrentCallsToMeet evaluations are inside the engine at the same
/// time, and stops waiting after rendezvousTimeout, so that the test fails instead of
/// blocking if BootstrapEngine makes engine calls wait for each other.
constexpr int concurrentCallsToMeet = 4;
constexpr auto rendezvousTimeout = std::chrono::seconds(10);
std::mutex rendezvousMutex;
std::condition_variable rendezvousChanged;
int rendezvousArrivals = 0;
std::uint64_t rendezvousGeneration = 0;

bool meetConcurrentCalls() {
  std::unique_lock<std::mutex> lock(rendezvousMutex);
  auto const generation = rendezvousGeneration;
  if(++rendezvousArrivals == concurrentCallsToMeet) {
    rendezvousArrivals = 0;
    ++rendezvousGeneration;
    rendezvousChanged.notify_all();
    return true;
  }
  if(rendezvousChanged.wait_for(lock, rendezvousTimeout,
                                [generation] { return rendezvousGeneration != generation; })) {
    return true;
  }
  // Leave the rendezvous so that a later test starts with no arrivals.
  --rendezvousArrivals;
  return false;
}
} // namespace

extern "C" void stubPauseNextReset() {
  std::scoped_lock const lock(pauseMutex);
  pauseNextReset = true;
}

extern "C" int stubWaitUntilCallPaused() {
  std::unique_lock<std::mutex> lock(pauseMutex);
  return pauseChanged.wait_for(lock, pauseTimeout, [] { return callPaused; }) ? 1 : 0;
}

extern "C" void stubReleasePausedCall() {
  std::scoped_lock const lock(pauseMutex);
  pauseReleased = true;
  pauseChanged.notify_all();
}

extern "C" int stubResetCount() {
  std::scoped_lock const lock(pauseMutex);
  return resetCount;
}

extern "C" void stubThrowFromNextReset() {
  std::scoped_lock const lock(pauseMutex);
  throwFromNextReset = true;
}

extern "C" void reset() {
  bool pauseThisReset = false;
  bool throwThisReset = false;
  {
    std::scoped_lock const lock(pauseMutex);
    pauseThisReset = pauseNextReset;
    pauseNextReset = false;
    ++resetCount;
    throwThisReset = throwFromNextReset;
    throwFromNextReset = false;
  }
  if(pauseThisReset) {
    pauseUntilReleased();
  }
  if(throwThisReset) {
    throw std::runtime_error("stub reset failure");
  }
}

/// Evaluates every expression to defaultEntryPointResult, after pausing first when the
/// expression's head is Pause. Rendezvous evaluates to 1 if concurrentCallsToMeet calls met
/// inside the engine, and to 0 if they did not.
extern "C" BOSSExpression* evaluate(BOSSExpression* expression) {
  auto const* request = std::get_if<ComplexExpression>(&expression->delegate);
  if(request != nullptr && request->getHead().getName() == "Pause") {
    pauseUntilReleased();
  }
  if(request != nullptr && request->getHead().getName() == "Rendezvous") {
    return new BOSSExpression {std::int64_t {meetConcurrentCalls() ? 1 : 0}};
  }
  return new BOSSExpression {defaultEntryPointResult};
}
