#include "../Source/BOSS.hpp"
#include "../Source/Expression.hpp"

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
// BootstrapEngine refuses while that call is running.
std::mutex pauseMutex;
std::condition_variable pauseChanged;
bool callPaused = false;
bool pauseReleased = false;
bool pauseNextReset = false;

void pauseUntilReleased() {
  std::unique_lock<std::mutex> lock(pauseMutex);
  callPaused = true;
  pauseChanged.notify_all();
  pauseChanged.wait(lock, [] { return pauseReleased; });
  callPaused = false;
  pauseReleased = false;
}

BOSSExpression* customEntryPoint(BOSSExpression* /*unused*/) {
  return new BOSSExpression {boss::Expression(customEntryPointResult)};
}

BOSSExpression* answerGetEntryPoint(ComplexExpression const& request) {
  auto const& arguments = request.getDynamicArguments();
  auto const* mode = arguments.empty() ? nullptr : std::get_if<Symbol>(&arguments.front());
  if(mode != nullptr && mode->getName() == "Override") {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const address = reinterpret_cast<std::intptr_t>(&customEntryPoint);
    return new BOSSExpression {boss::Expression(static_cast<std::int64_t>(address))};
  }
  if(mode != nullptr && mode->getName() == "Zero") {
    return new BOSSExpression {boss::Expression(std::int64_t {0})};
  }
  return new BOSSExpression {boss::Expression(std::string("not an entry point"))};
}

// CMakeLists.txt builds this file three times, so GetEngineDescription can be tested with a
// string answer, no answer and a non-string answer in one pipeline.
BOSSExpression* describeEngine() {
#if defined(STUB_ENGINE_DESCRIPTION_IS_NOTHING)
  return nullptr;
#elif defined(STUB_ENGINE_DESCRIPTION_IS_NUMBER)
  constexpr std::int64_t numericDescription = 7;
  return new BOSSExpression {boss::Expression(numericDescription)};
#else
  return new BOSSExpression {boss::Expression(std::string("A stub engine for the BOSS tests.\n"))};
#endif
}
} // namespace

extern "C" void stubPauseNextReset() {
  std::scoped_lock const lock(pauseMutex);
  pauseNextReset = true;
}

extern "C" void stubWaitUntilCallPaused() {
  std::unique_lock<std::mutex> lock(pauseMutex);
  pauseChanged.wait(lock, [] { return callPaused; });
}

extern "C" void stubReleasePausedCall() {
  std::scoped_lock const lock(pauseMutex);
  pauseReleased = true;
  pauseChanged.notify_all();
}

extern "C" void reset() {
  bool pauseThisReset = false;
  {
    std::scoped_lock const lock(pauseMutex);
    pauseThisReset = pauseNextReset;
    pauseNextReset = false;
  }
  if(pauseThisReset) {
    pauseUntilReleased();
  }
}

// Answers GetEntryPoint(Override) with a custom entry point, GetEntryPoint(Zero) with 0 and any
// other GetEntryPoint request with a string, so the Tests binary can check how BootstrapEngine
// handles each result. GetEngineDescription is answered according to the build variant (see
// describeEngine). An expression whose head is Pause pauses first. Every other expression
// evaluates to defaultEntryPointResult.
extern "C" BOSSExpression* evaluate(BOSSExpression* expression) {
  auto const* request = std::get_if<ComplexExpression>(&expression->delegate);
  if(request != nullptr && request->getHead().getName() == "GetEntryPoint") {
    return answerGetEntryPoint(*request);
  }
  if(request != nullptr && request->getHead().getName() == "GetEngineDescription") {
    return describeEngine();
  }
  if(request != nullptr && request->getHead().getName() == "Pause") {
    pauseUntilReleased();
  }
  return new BOSSExpression {boss::Expression(defaultEntryPointResult)};
}
