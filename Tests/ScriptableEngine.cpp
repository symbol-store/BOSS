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
using boss::Symbol;

constexpr std::int64_t defaultEntryPointResult = 1;
constexpr std::int64_t customEntryPointResult = 4242;

struct PauseState {
  static constexpr auto timeout = std::chrono::seconds(30);

  std::mutex mutex;
  std::condition_variable changed;
  bool callPaused = false;
  bool released = false;
  bool pauseNextReset = false;
  bool pauseNextDescription = false;
  int resetCount = 0;
  bool throwFromNextReset = false;
  bool throwFromNextEvaluation = false;

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
  state.changed.wait_for(lock, PauseState::timeout, [&state] { return state.released; });
  state.callPaused = false;
  state.released = false;
}

BOSSExpression* customEntryPoint(BOSSExpression* /*unused*/) {
  return new BOSSExpression {customEntryPointResult};
}

/// Answers span-carrying and argument-less requests with the custom entry point, so a dropped
/// span or an unwanted request changes the result.
BOSSExpression* answerGetEntryPoint(ComplexExpression const& request) {
  auto const answerWithCustomEntryPoint = [] {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const address = reinterpret_cast<std::intptr_t>(&customEntryPoint);
    return new BOSSExpression {static_cast<std::int64_t>(address)};
  };
  if(!request.getSpanArguments().empty()) {
    return answerWithCustomEntryPoint();
  }
  auto const& arguments = request.getDynamicArguments();
  auto const* mode = arguments.empty() ? nullptr : std::get_if<Symbol>(&arguments.front());
  if(arguments.empty() || (mode != nullptr && mode->getName() == "Override")) {
    return answerWithCustomEntryPoint();
  }
  if(mode != nullptr && mode->getName() == "Zero") {
    return new BOSSExpression {std::int64_t {0}};
  }
  return new BOSSExpression {"not an entry point"};
}

BOSSExpression* describeEngine() {
#if defined(SCRIPTABLE_ENGINE_DESCRIPTION_IS_NUMBER)
  constexpr std::int64_t numericDescription = 7;
  return new BOSSExpression {numericDescription};
#else
  return new BOSSExpression {"A scriptable engine for the BOSS tests.\n"};
#endif
}

struct CallBarrier {
  static constexpr int callsToWaitFor = 4;
  static constexpr auto timeout = std::chrono::seconds(10);

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
  if(++barrier.arrivals == CallBarrier::callsToWaitFor) {
    barrier.arrivals = 0;
    ++barrier.generation;
    barrier.changed.notify_all();
    return true;
  }
  if(barrier.changed.wait_for(lock, CallBarrier::timeout, [&barrier, generation] {
       return barrier.generation != generation;
     })) {
    return true;
  }
  --barrier.arrivals;
  return false;
}
} // namespace

extern "C" void scriptablePauseNextReset() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  state.pauseNextReset = true;
}

extern "C" void scriptablePauseNextDescription() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  state.pauseNextDescription = true;
}

extern "C" int scriptableWaitUntilCallPaused() {
  auto& state = PauseState::singleton();
  std::unique_lock<std::mutex> lock(state.mutex);
  return state.changed.wait_for(lock, PauseState::timeout, [&state] { return state.callPaused; })
             ? 1
             : 0;
}

extern "C" void scriptableReleasePausedCall() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  state.released = true;
  state.changed.notify_all();
}

extern "C" int scriptableResetCount() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  return state.resetCount;
}

extern "C" void scriptableThrowFromNextReset() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  state.throwFromNextReset = true;
}

extern "C" void scriptableThrowFromNextEvaluation() {
  auto& state = PauseState::singleton();
  std::scoped_lock const lock(state.mutex);
  state.throwFromNextEvaluation = true;
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
    state.throwFromNextEvaluation = false;
    state.pauseNextDescription = false;
  }
  if(pauseThisReset) {
    pauseUntilReleased();
  }
  if(throwThisReset) {
    throw std::runtime_error("scriptable engine reset failure");
  }
}

extern "C" BOSSExpression* evaluate(BOSSExpression* expression) {
  bool throwThisEvaluation = false;
  {
    auto& state = PauseState::singleton();
    std::scoped_lock const lock(state.mutex);
    throwThisEvaluation = state.throwFromNextEvaluation;
    state.throwFromNextEvaluation = false;
  }
  if(throwThisEvaluation) {
    throw std::runtime_error("scriptable engine evaluation failure");
  }
  if(expression == nullptr) {
    return nullptr;
  }
  auto const* request = std::get_if<ComplexExpression>(&expression->delegate);
  if(request != nullptr && request->getHead().getName() == "GetEntryPoint") {
    return answerGetEntryPoint(*request);
  }
  if(request != nullptr && request->getHead().getName() == "GetEngineDescription") {
    auto& state = PauseState::singleton();
    bool pauseThisDescription = false;
    {
      std::scoped_lock const lock(state.mutex);
      pauseThisDescription = state.pauseNextDescription;
      state.pauseNextDescription = false;
    }
    if(pauseThisDescription) {
      pauseUntilReleased();
    }
    return describeEngine();
  }
  if(request != nullptr && request->getHead().getName() == "Pause") {
    pauseUntilReleased();
  }
  if(request != nullptr && request->getHead().getName() == "Barrier") {
    return new BOSSExpression {std::int64_t {waitAtCallBarrier() ? 1 : 0}};
  }
  if(request != nullptr && request->getHead().getName() == "AnswerNothing") {
    return nullptr;
  }
  return new BOSSExpression {defaultEntryPointResult};
}
