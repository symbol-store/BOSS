#include <inttypes.h>
extern "C" {
#include "PortableBOSSSerialization.h"
}
namespace boss::serialization {
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

  template <typename T> static uint64_t countArguments(T const& /*unused*/) { return 1; }

  static uint64_t countArguments(boss::ComplexExpression const& input) {
    return countArgumentsInTuple(
               input.getStaticArguments(),
               std::make_index_sequence<
                   std::tuple_size_v<std::decay_t<decltype(input.getStaticArguments())>>>()) +
           std::accumulate(
               input.getDynamicArguments().begin(), input.getDynamicArguments().end(), 0,
               [](auto runningSum, auto const& argument) {
                 return runningSum +
                        std::visit(
                            [](auto const& argument) -> uint64_t {
                              if constexpr(boss::expressions::generic::isComplexExpression<
                                               decltype(argument)>) {
                                return 1 + countArguments(argument);
                              } else {
                                return 1;
                              }
                            },
                            argument);
               }) +
           input.getSpanArguments().size();
  }

  //////////////////////////////// Count Expressions ///////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  static uint64_t countExpressionsInTuple(TupleLike const& tuple,
                                          std::index_sequence<Is...> /*unused*/) {
    return (countExpressions(std::get<Is>(tuple)) + ... + 0);
  };

  template <typename T> static uint64_t countExpressions(T const& /*unused*/) { return 0; }

  static uint64_t countExpressions(boss::ComplexExpression const& input) {
    return 1 +
           countArgumentsInTuple(
               input.getStaticArguments(),
               std::make_index_sequence<
                   std::tuple_size_v<std::decay_t<decltype(input.getStaticArguments())>>>()) +
           std::accumulate(
               input.getDynamicArguments().begin(), input.getDynamicArguments().end(), 0,
               [](auto runningSum, auto const& argument) {
                 return runningSum +
                        std::visit(
                            [](auto const& argument) -> uint64_t {
                              if constexpr(boss::expressions::generic::isComplexExpression<
                                               decltype(argument)>) {
                                return countExpressions(argument);
                              } else {
                                return 0;
                              }
                            },
                            argument);
               });
  }

  //////////////////////////////   Flatten Arguments /////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  static void flattenArgumentsInTuple(Argument* buffer, TupleLike&& tuple,
                                      std::index_sequence<Is...> /*unused*/,
                                      uint64_t& argumentOutputI) {
    (flattenArguments(std::get<Is>(tuple), argumentOutputI), ...);
  };

  template <typename T>
  static uint64_t flattenArguments(Argument* buffer, T&& value, uint64_t& argumentOutputI) {
    buffer[argumentOutputI++] = value;
  }

  static void flattenArguments(Argument* buffer, uint64_t& argumentOutputI,
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
          std::for_each(dynamics.begin(), dynamics.end(),
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

                                  *makeExpression(expressions,
                                                  expressionOutputI++) = PortableBossExpression{
                                      .headOffset = argumentOutputI,
                                      .firstChildOffset = nextLayerOffset + childrenCountRunningSum,
                                      .lastChildOffset = nextLayerOffset + childrenCountRunningSum +
                                                         childrenCount - 1};
                                  *makeSymbolArgument(buffer, argumentOutputI++) =
                                      strdup(argument.getHead().getName().c_str());
                                  childrenCountRunningSum += childrenCount;
                                  children.push_back(std::move(argument));
                                } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                                   long long>) {
                                  *makeLongArgument(buffer, argumentOutputI++) = argument;
                                } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                                   boss::Symbol>) {
                                  *makeSymbolArgument(buffer, argumentOutputI++) =
                                      strdup(argument.getName().c_str());
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
      flattenArguments(buffer, argumentOutputI, std::move(children), expressions,
                       expressionOutputI);
    }
  }

  ////////////////////////////////   Surface Area ////////////////////////////////

public:
  explicit SerializedExpression(boss::ComplexExpression&& input)
      : root(allocateExpressionTree(countArguments(input) + 1, countExpressions(input))) {
    auto argumentIterator = uint64_t{};
    auto expressionIterator = uint64_t{};
    flattenedArguments()[argumentIterator++] =
        Argument{.type = Argument::SymbolType::SYMBOL,
                 .asString = strdup(input.getHead().getName().c_str())};
    auto inputs = std::vector<boss::ComplexExpression>();
    inputs.push_back(std::move(input));
    flattenArguments(flattenedArguments(), argumentIterator, std::move(inputs), expressionsBuffer(),
                     expressionIterator);
  }

  boss::expressions::ExpressionArguments
  deserializeArguments(uint64_t firstChildOffset, uint64_t lastChildOffset,
                       uint64_t unprocessedExpressionPointer) {
    boss::expressions::ExpressionArguments arguments;
    for(auto childIndex = firstChildOffset; childIndex <= lastChildOffset; childIndex++) {
      auto& arg = flattenedArguments()[childIndex];
      arguments.push_back(std::map<Argument::SymbolType, std::function<boss::Expression()>>{
          {Argument::LONG, [&] { return (arg.asLong); }},
          {Argument::DOUBLE, [&] { return (arg.asDouble); }},
          {Argument::SYMBOL,
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
               free(static_cast<void*>(arg.asString));
               return result;
             }
             auto result = boss::Symbol(arg.asString);
             free(static_cast<void*>(arg.asString));
             return result;
           }},

          {Argument::STRING, [&] {
             auto result = std::string(arg.asString);
             free(static_cast<void*>(arg.asString));
             return result;
           }}}.at(arg.type)());
    }
    return arguments;
  }

  boss::ComplexExpression deserialize() && {
    auto result = boss::ComplexExpression{
        boss::Symbol(std::string(flattenedArguments()[0].asString)),
        deserializeArguments(1, expressionsBuffer()[0].firstChildOffset - 1, 0)};
    free(static_cast<void*>(flattenedArguments()[0].asString));
    return result;
  };

  PortableBOSSExpressionRoot* extractRoot() && {
    auto root = this->root;
    this->root = nullptr;
    return root;
  };

  ~SerializedExpression() { freeExpressionTree(root); }
};
} // namespace boss::serialization
