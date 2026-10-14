#include "../Source/BOSS.hpp"
#include "../Source/Expression.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <variant>

namespace {
using boss::ComplexExpression;
using boss::Symbol;

constexpr std::int64_t defaultEntryPointResult = 1;
constexpr std::int64_t customEntryPointResult = 4242;

// Lets a test hold one engine call (an evaluation of Pause, or a reset armed with
// stubPauseNextReset) until the test calls stubReleasePausedCall, so it can check what
// BootstrapEngine refuses while that call is running. Every wait gives up after pauseTimeout, so a
// test that fails part-way cannot hang its run.
constexpr auto pauseTimeout = std::chrono::seconds(30);
std::mutex pauseMutex;
std::condition_variable pauseChanged;
bool callPaused = false;
bool pauseReleased = false;
bool pauseNextReset = false;
int resetCount = 0;

void pauseUntilReleased() {
  std::unique_lock<std::mutex> lock(pauseMutex);
  callPaused = true;
  pauseChanged.notify_all();
  pauseChanged.wait_for(lock, pauseTimeout, [] { return pauseReleased; });
  callPaused = false;
  pauseReleased = false;
}

BOSSExpression* customEntryPoint(BOSSExpression* /*unused*/) {
  return new BOSSExpression {boss::Expression(customEntryPointResult)};
}

BOSSExpression* answerGetEntryPoint(ComplexExpression const& request) {
  auto const& arguments = request.getDynamicArguments();
  auto const* mode = arguments.empty() ? nullptr : std::get_if<Symbol>(&arguments.front());
  // A request with no arguments gets the custom entry point too, so a test dispatching through
  // a plain StubTest symbol fails if BootstrapEngine asks that symbol for an entry point.
  if(arguments.empty() || (mode != nullptr && mode->getName() == "Override")) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const address = reinterpret_cast<std::intptr_t>(&customEntryPoint);
    return new BOSSExpression {boss::Expression(static_cast<std::int64_t>(address))};
  }
  if(mode != nullptr && mode->getName() == "Zero") {
    return new BOSSExpression {boss::Expression(std::int64_t {0})};
  }
  return new BOSSExpression {boss::Expression(std::string("not an entry point"))};
}

// Lets a test check that BootstrapEngine runs engine calls concurrently: each evaluation of
// Rendezvous waits until concurrentCallsToMeet of them are inside the engine at the same time,
// and gives up after rendezvousTimeout, so a dispatcher that serialized engine calls fails the
// test instead of hanging it.
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

extern "C" void reset() {
  bool pauseThisReset = false;
  {
    std::scoped_lock const lock(pauseMutex);
    pauseThisReset = pauseNextReset;
    pauseNextReset = false;
    ++resetCount;
  }
  if(pauseThisReset) {
    pauseUntilReleased();
  }
}

// Answers GetEntryPoint(Override) and GetEntryPoint() with a custom entry point,
// GetEntryPoint(Zero) with 0 and any other GetEntryPoint request with a string, so the Tests binary
// can check how BootstrapEngine handles each result. An expression whose head is Pause pauses
// first. Rendezvous evaluates to 1 if concurrentCallsToMeet calls met inside the engine, and to 0
// if they did not. Every other expression evaluates to defaultEntryPointResult.
extern "C" BOSSExpression* evaluate(BOSSExpression* expression) {
  auto const* request = std::get_if<ComplexExpression>(&expression->delegate);
  if(request != nullptr && request->getHead().getName() == "GetEntryPoint") {
    return answerGetEntryPoint(*request);
  }
  if(request != nullptr && request->getHead().getName() == "Pause") {
    pauseUntilReleased();
  }
  if(request != nullptr && request->getHead().getName() == "Rendezvous") {
    return new BOSSExpression {boss::Expression(std::int64_t {meetConcurrentCalls() ? 1 : 0})};
  }
  return new BOSSExpression {boss::Expression(defaultEntryPointResult)};
}
