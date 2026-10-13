#include "../Source/BOSS.hpp"
#include "../Source/Expression.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace {
using boss::ComplexExpression;
using boss::Symbol;

constexpr std::int64_t defaultEntryPointResult = 1;
constexpr std::int64_t customEntryPointResult = 4242;

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

// Answers GetEntryPoint(Override) with a custom entry point, GetEntryPoint(Zero) with 0 and any
// other GetEntryPoint request with a string, so the Tests binary can check how BootstrapEngine
// handles each result. GetEngineDescription is answered according to the build variant
// (see describeEngine), and every other expression evaluates to defaultEntryPointResult.
extern "C" BOSSExpression* evaluate(BOSSExpression* expression) {
  auto const* request = std::get_if<ComplexExpression>(&expression->delegate);
  if(request != nullptr && request->getHead().getName() == "GetEntryPoint") {
    return answerGetEntryPoint(*request);
  }
  if(request != nullptr && request->getHead().getName() == "GetEngineDescription") {
    return describeEngine();
  }
  return new BOSSExpression {boss::Expression(defaultEntryPointResult)};
}
