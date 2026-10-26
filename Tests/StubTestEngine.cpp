#include "../Source/BOSS.hpp"
#include "../Source/Expression.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <variant>

namespace {
using boss::ComplexExpression;
using boss::Symbol;

constexpr std::int64_t defaultEntryPointResult = 1;
constexpr std::int64_t customEntryPointResult = 4242;

/// Lets a test hold one engine call (an evaluation of Pause, or a reset armed with
/// stubPauseNextReset) until the test calls stubReleasePausedCall, so that the test can check
/// what BootstrapEngine refuses while that call is running. Every wait ends after
/// pauseTimeout, so a test that fails part-way cannot block the test run indefinitely.
constexpr auto pauseTimeout = std::chrono::seconds(30);
struct PauseState {
  std::mutex mutex;
  std::condition_variable changed;
  bool callPaused = false;
  bool released = false;
  bool pauseNextReset = false;
  int resetCount = 0;
  bool throwFromNextReset = false;

  static PauseState& singleton() {
    static PauseState state;
    return state;
  }
};

void pauseUntilReleased() {
  auto& state = PauseState::singleton();
  std::unique_lock<std::mutex> lock(state.mutex);
  state.callPaused = true;
  state.changed.notify_all();
  state.changed.wait_for(lock, pauseTimeout, [&state] { return state.released; });
  state.callPaused = false;
  state.released = false;
}

BOSSExpression* customEntryPoint(BOSSExpression* /*unused*/) {
  return new BOSSExpression {customEntryPointResult};
}

BOSSExpression* answerGetEntryPoint(ComplexExpression const& request) {
  auto const answerWithCustomEntryPoint = [] {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const address = reinterpret_cast<std::intptr_t>(&customEntryPoint);
    return new BOSSExpression {static_cast<std::int64_t>(address)};
  };
  // answerGetEntryPoint answers every request with a span argument with the custom entry
  // point, whatever the dynamic arguments are, so that the test fails if BootstrapEngine
  // leaves the engine spec's span arguments out of the request.
  if(!request.getSpanArguments().empty()) {
    return answerWithCustomEntryPoint();
  }
  auto const& arguments = request.getDynamicArguments();
  auto const* mode = arguments.empty() ? nullptr : std::get_if<Symbol>(&arguments.front());
  // answerGetEntryPoint also answers a request without arguments with the custom entry
  // point, so that a test that dispatches through a plain StubTest symbol fails if
  // BootstrapEngine sends GetEntryPoint for that symbol.
  if(arguments.empty() || (mode != nullptr && mode->getName() == "Override")) {
    return answerWithCustomEntryPoint();
  }
  if(mode != nullptr && mode->getName() == "Zero") {
    return new BOSSExpression {std::int64_t {0}};
  }
  return new BOSSExpression {"not an entry point"};
}

/// Lets a test check that BootstrapEngine runs engine calls concurrently: each evaluation of
/// Barrier waits until callsToWaitFor evaluations are inside the engine at the same
/// time, and stops waiting after barrierTimeout, so that the test fails instead of
/// blocking if BootstrapEngine makes engine calls wait for each other.
constexpr int callsToWaitFor = 4;
constexpr auto barrierTimeout = std::chrono::seconds(10);
struct CallBarrier {
  std::mutex mutex;
  std::condition_variable changed;
  int arrivals = 0;
  std::uint64_t generation = 0;

  static CallBarrier& singleton() {
    static CallBarrier state;
    return state;
  }
};

bool waitAtCallBarrier() {
  auto& barrier = CallBarrier::singleton();
  std::unique_lock<std::mutex> lock(barrier.mutex);
  auto const generation = barrier.generation;
  if(++barrier.arrivals == callsToWaitFor) {
    barrier.arrivals = 0;
    ++barrier.generation;
    barrier.changed.notify_all();
    return true;
  }
  if(barrier.changed.wait_for(lock, barrierTimeout, [&barrier, generation] {
       return barrier.generation != generation;
     })) {
    return true;
  }
  // Leaves the barrier so that a later test starts with no arrivals.
  --barrier.arrivals;
  return false;
}
} // namespace

extern "C" void stubPauseNextReset() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  state.pauseNextReset = true;
}

extern "C" int stubWaitUntilCallPaused() {
  auto& state = PauseState::singleton();
  std::unique_lock<std::mutex> lock(state.mutex);
  return state.changed.wait_for(lock, pauseTimeout, [&state] { return state.callPaused; }) ? 1 : 0;
}

extern "C" void stubReleasePausedCall() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  state.released = true;
  state.changed.notify_all();
}

extern "C" int stubResetCount() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  return state.resetCount;
}

extern "C" void stubThrowFromNextReset() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  state.throwFromNextReset = true;
}

extern "C" void reset() {
  auto& state = PauseState::singleton();
  bool pauseThisReset = false;
  bool throwThisReset = false;
  {
    std::scoped_lock const lock(state.mutex);
    pauseThisReset = state.pauseNextReset;
    state.pauseNextReset = false;
    ++state.resetCount;
    throwThisReset = state.throwFromNextReset;
    state.throwFromNextReset = false;
  }
  if(pauseThisReset) {
    pauseUntilReleased();
  }
  if(throwThisReset) {
    throw std::runtime_error("stub reset failure");
  }
}

/// Answers GetEntryPoint(Override), GetEntryPoint() and any GetEntryPoint request with a span
/// argument with a custom entry point, GetEntryPoint(Zero) with 0 and any other GetEntryPoint
/// request with a string, so that the tests can check how BootstrapEngine handles each result.
/// Evaluates Barrier to 1 if callsToWaitFor calls were inside the engine at the same time, and
/// to 0 if they were not. Evaluates every other expression to defaultEntryPointResult, after
/// pausing first when the expression's head is Pause.
extern "C" BOSSExpression* evaluate(BOSSExpression* expression) {
  auto const* request = std::get_if<ComplexExpression>(&expression->delegate);
  if(request != nullptr && request->getHead().getName() == "GetEntryPoint") {
    return answerGetEntryPoint(*request);
  }
  if(request != nullptr && request->getHead().getName() == "Pause") {
    pauseUntilReleased();
  }
  if(request != nullptr && request->getHead().getName() == "Barrier") {
    return new BOSSExpression {std::int64_t {waitAtCallBarrier() ? 1 : 0}};
  }
  return new BOSSExpression {defaultEntryPointResult};
}
