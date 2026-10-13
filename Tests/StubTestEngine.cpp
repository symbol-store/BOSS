#include "../Source/BOSS.hpp"
#include "../Source/Expression.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <variant>

namespace {
using boss::ComplexExpression;

constexpr std::int64_t defaultEntryPointResult = 1;

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

// Evaluates every expression to defaultEntryPointResult, after pausing first when the
// expression's head is Pause.
extern "C" BOSSExpression* evaluate(BOSSExpression* expression) {
  auto const* request = std::get_if<ComplexExpression>(&expression->delegate);
  if(request != nullptr && request->getHead().getName() == "Pause") {
    pauseUntilReleased();
  }
  return new BOSSExpression {boss::Expression(defaultEntryPointResult)};
}
