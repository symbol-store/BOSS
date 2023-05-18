#include "BOSS.hpp"
#include <inttypes.h>
#include <utility>
extern "C" {
#include "PortableBOSSSerialization.h"
}
namespace boss::serialization {
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)

using Argument = PortableBossArgument;
using Expression = PortableBossExpression;
/**
 * Implements serialization/deserialization of a (complex) expression to/from a c-allocated buffer.
 * The buffer contains no pointers so it can be safely written to disk or passed to a different
 * processing using shared memory
 */
struct SerializedExpression {
  PortableBOSSExpressionRoot* root;
  uint64_t argumentCount() const { return root->argumentCount; };

  Argument* flattenedArguments() const { return getExpressionArguments(root); }
  Expression* expressionsBuffer() const { return getExpressionSubexpressions(root); }

  //////////////////////////////// Count Arguments ///////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  static uint64_t countArgumentsInTuple(TupleLike const& tuple,
                                        std::index_sequence<Is...> /*unused*/) {
    return (countArguments(std::get<Is>(tuple)) + ... + 0);
  };

  static uint64_t countArguments(boss::Expression const& input) {
    return std::visit(
        [](auto& input) -> size_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
            return 1 +
                   countArgumentsInTuple(
                       input.getStaticArguments(),
                       std::make_index_sequence<std::tuple_size_v<
                           std::decay_t<decltype(input.getStaticArguments())>>>()) +
                   std::accumulate(input.getDynamicArguments().begin(),
                                   input.getDynamicArguments().end(), 0,
                                   [](auto runningSum, auto const& argument) {
                                     return runningSum + countArguments(argument);
                                   }) +
                   input.getSpanArguments().size();
          }
          return 1;
        },
        input);
  }

  //////////////////////////////// Count Expressions ///////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  static uint64_t countExpressionsInTuple(TupleLike const& tuple,
                                          std::index_sequence<Is...> /*unused*/) {
    return (countExpressions(std::get<Is>(tuple)) + ... + 0);
  };

  template <typename T> static uint64_t countExpressions(T const& /*unused*/) { return 0; }

  static uint64_t countExpressions(boss::Expression const& input) {
    return std::visit(utilities::overload(
                          [](boss::ComplexExpression const& input) -> uint64_t {
                            return 1 +
                                   countExpressionsInTuple(
                                       input.getStaticArguments(),
                                       std::make_index_sequence<std::tuple_size_v<
                                           std::decay_t<decltype(input.getStaticArguments())>>>()) +
                                   std::accumulate(input.getDynamicArguments().begin(),
                                                   input.getDynamicArguments().end(), 0,
                                                   [](auto runningSum, auto const& argument) {
                                                     return runningSum + countExpressions(argument);
                                                   });
                          },
                          [](auto const&) -> uint64_t { return 0; }),
                      input);
  }

  //////////////////////////////   Flatten Arguments /////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  static void flattenArgumentsInTuple(Argument* buffer, TupleLike&& tuple,
                                      std::index_sequence<Is...> /*unused*/,
                                      uint64_t& argumentOutputI) {
    (flattenArguments(buffer, std::get<Is>(tuple), argumentOutputI), ...);
  };

  static uint64_t flattenArguments(Argument* buffer, uint64_t argumentOutputI,
                                   std::vector<boss::ComplexExpression>&& inputs,
                                   Expression* expressions, uint64_t& expressionOutputI) {
    auto nextLayerOffset =
        argumentOutputI +
        std::accumulate(inputs.begin(), inputs.end(), 0, [](auto count, auto const& expression) {
          return count +
                 std::tuple_size_v<std::decay_t<decltype(expression.getStaticArguments())>> +
                 expression.getDynamicArguments().size() + expression.getSpanArguments().size();
        });
    auto children = std::vector<boss::ComplexExpression>();
    auto childrenCountRunningSum = 0UL;

    std::for_each(
        std::move_iterator(inputs.begin()), std::move_iterator(inputs.end()),
        [&](boss::ComplexExpression&& input) {
          auto [head, statics, dynamics, spans] = std::move(input).decompose();

          flattenArgumentsInTuple(
              buffer, statics,
              std::make_index_sequence<std::tuple_size_v<std::decay_t<decltype(statics)>>>(),
              argumentOutputI);
          std::for_each(
              dynamics.begin(), dynamics.end(),
              [buffer, &argumentOutputI, &children, expressions, &expressionOutputI,
               nextLayerOffset, &childrenCountRunningSum](auto&& argument) {
                std::visit(
                    [&children, buffer, &argumentOutputI, expressions, &expressionOutputI,
                     nextLayerOffset, &childrenCountRunningSum](auto&& argument) {
                      if constexpr(boss::expressions::generic::isComplexExpression<
                                       decltype(argument)>) {
                        auto const childrenCount =
                            std::tuple_size_v<
                                std::decay_t<decltype(argument.getStaticArguments())>> +
                            argument.getDynamicArguments().size() +
                            argument.getSpanArguments().size();

                        *makeExpression(expressions, expressionOutputI++) = PortableBossExpression{
                            .headOffset = argumentOutputI,
                            .firstChildOffset = nextLayerOffset + childrenCountRunningSum,
                            .lastChildOffset =
                                nextLayerOffset + childrenCountRunningSum + childrenCount - 1};
                        *makeSymbolArgument(buffer, argumentOutputI++) =
                            strdup(argument.getHead().getName().c_str());
                        childrenCountRunningSum += childrenCount;
                        children.push_back(std::forward<decltype(argument)>(argument));
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         long long>) {
                        *makeLongArgument(buffer, argumentOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         boss::Symbol>) {
                        *makeSymbolArgument(buffer, argumentOutputI++) =
                            strdup(argument.getName().c_str());
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         std::string>) {
                        *makeStringArgument(buffer, argumentOutputI++) = strdup(argument.c_str());
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         double>) {
                        *makeDoubleArgument(buffer, argumentOutputI++) = argument;
                      } else {
                        __builtin_debugtrap();
                        throw std::runtime_error("unknown type");
                      }
                    },
                    argument);
              });
        });
    if(!children.empty()) {
      return flattenArguments(buffer, argumentOutputI, std::move(children), expressions,
                              expressionOutputI);
    }
    return argumentOutputI;
  }

  ////////////////////////////////   Surface Area ////////////////////////////////

public:
  explicit SerializedExpression(boss::Expression&& input)
      : SerializedExpression(
            allocateExpressionTree(countArguments(input), countExpressions(input))) {
    std::visit(utilities::overload(
                   [this](boss::ComplexExpression&& input) {
                     auto argumentIterator = uint64_t{};
                     auto expressionIterator = uint64_t{};
                     expressionsBuffer()[expressionIterator++] = {
                         .headOffset = 0,
                         .firstChildOffset = 1,
                         .lastChildOffset = input.getDynamicArguments().size()};
                     flattenedArguments()[argumentIterator++] =
                         Argument{.type = Argument::SymbolType::SYMBOL,
                                  .asString = strdup(input.getHead().getName().c_str())};
                     auto inputs = std::vector<boss::ComplexExpression>();
                     inputs.push_back(std::move(input));
                     flattenArguments(flattenedArguments(), argumentIterator, std::move(inputs),
                                      expressionsBuffer(), expressionIterator);
                   },
                   [this](expressions::atoms::Symbol&& input) {
                     flattenedArguments()[0] =
                         Argument{.type = Argument::SymbolType::SYMBOL,
                                  .asString = strdup(input.getName().c_str())};
                   },
                   [this](std::int64_t input) {
                     flattenedArguments()[0] =
                         Argument{.type = Argument::SymbolType::LONG, .asLong = input};
                   },
                   [this](std::double_t input) {
                     flattenedArguments()[0] =
                         Argument{.type = Argument::SymbolType::DOUBLE, .asDouble = input};
                   },
                   [](auto&&) {
                     throw std::logic_error("uncountered unknown type during serialization");
                   }),
               std::move(input));
  }

  explicit SerializedExpression(PortableBOSSExpressionRoot* root) : root(root) {}

  boss::expressions::ExpressionArguments
  deserializeArguments(uint64_t firstChildOffset, uint64_t lastChildOffset,
                       uint64_t unprocessedExpressionPointer) {
    boss::expressions::ExpressionArguments arguments;
    for(auto childIndex = firstChildOffset; childIndex <= lastChildOffset; childIndex++) {
      auto& arg = flattenedArguments()[childIndex];
      auto const functors =
          std::unordered_map<Argument::SymbolType, std::function<boss::Expression()>>{
              {Argument::SymbolType::LONG, [&] { return (arg.asLong); }},
              {Argument::SymbolType::DOUBLE, [&] { return (arg.asDouble); }},
              {Argument::SymbolType::SYMBOL,
               [&]() -> boss::Expression {
                 while(expressionsBuffer()[unprocessedExpressionPointer].headOffset < childIndex) {
                   unprocessedExpressionPointer++;
                 }
                 if(expressionsBuffer()[unprocessedExpressionPointer].headOffset == childIndex) {
                   auto result = boss::expressions::ComplexExpression(
                       boss::Symbol(arg.asString),
                       deserializeArguments(
                           expressionsBuffer()[unprocessedExpressionPointer].firstChildOffset,
                           expressionsBuffer()[unprocessedExpressionPointer].lastChildOffset,
                           unprocessedExpressionPointer + 1));
                   free(static_cast<void*>( // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
                       arg.asString));
                   return result;
                 }
                 auto result = boss::Symbol(arg.asString);
                 free(static_cast<void*>( // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
                     arg.asString));
                 return result;
               }},

              {Argument::SymbolType::STRING, [&] {
                 auto result = std::string(arg.asString);
                 free(static_cast<void*>( // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
                     arg.asString));
                 return result;
               }}};
      arguments.push_back(functors.at(arg.type)());
    }
    return arguments;
  }

  boss::Expression deserialize() && {
    switch(flattenedArguments()[0].type) {
    case PortableBossArgument::LONG:
      return flattenedArguments()[0].asLong;
    case PortableBossArgument::DOUBLE:
      return flattenedArguments()[0].asDouble;
    case PortableBossArgument::STRING:
      return flattenedArguments()[0].asString;
    case PortableBossArgument::SYMBOL:
      auto s = boss::Symbol(std::string(flattenedArguments()[0].asString));
      if(root->expressionCount == 0) {
        return s;
      }
      auto result = boss::ComplexExpression{
          s, deserializeArguments(1, expressionsBuffer()[0].lastChildOffset, 1)};
      free(static_cast<void*>( // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
          flattenedArguments()[0].asString));
      return result;
    }
  };

  PortableBOSSExpressionRoot* extractRoot() && {
    auto* root = this->root;
    this->root = nullptr;
    return root;
  };

  ~SerializedExpression() { freeExpressionTree(root); }
};

namespace url {
boss::Expression parse(std::string_view url, std::optional<boss::Expression>&& firstArgument = {});

}
// NOLINTEND(cppcoreguidelines-pro-type-union-access)
} // namespace boss::serialization
