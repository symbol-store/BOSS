#include "Expression.hpp"
#include "Utilities.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
extern "C" {
#include "PortableBOSSSerialization.h"
}
namespace boss::serialization {

// Detection trait for user-defined serializeCustomAtom(T const&) -> uint64_t overloads.
// Found via ADL: define serializeCustomAtom in the same namespace as the custom atom type.
namespace detail {
template <typename T>
auto detectCustomAtomSerializer(T const& t, int)
    -> decltype(serializeCustomAtom(t), uint64_t {});
template <typename T> void detectCustomAtomSerializer(T const&, ...);
} // namespace detail

template <typename T>
static constexpr bool hasCustomAtomSerializer =
    !std::is_void_v<decltype(detail::detectCustomAtomSerializer(std::declval<T const&>(), 0))>;
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)

static_assert(
    std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_BOOL, boss::Expression>, bool>,
    "type ids wrong");
static_assert(
    std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_CHAR, boss::Expression>, std::int8_t>,
    "type ids wrong");
static_assert(
    std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_INT, boss::Expression>, std::int32_t>,
    "type ids wrong");
static_assert(
    std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_LONG, boss::Expression>, std::int64_t>,
    "type ids wrong");
static_assert(
    std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_FLOAT, boss::Expression>, std::float_t>,
    "type ids wrong");
static_assert(std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_DOUBLE, boss::Expression>,
                             std::double_t>,
              "type ids wrong");
static_assert(
    std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_STRING, boss::Expression>, std::string>,
    "type ids wrong");
static_assert(std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_SYMBOL, boss::Expression>,
                             boss::Symbol>,
              "type ids wrong");

using Argument = PortableBOSSArgumentValue;
using ArgumentType = PortableBOSSArgumentType;
using Expression = PortableBOSSExpression;
using RootExpression = PortableBOSSRootExpression;
/**
 * Implements serialization/deserialization of a (complex) expression to/from a c-allocated buffer.
 * The buffer contains no pointers so it can be safely written to disk or passed to a different
 * processing using shared memory
 */
template <void* (*allocateFunction)(size_t) = std::malloc,
          void* (*reallocateFunction)(void*, size_t) = std::realloc,
          void (*freeFunction)(void*) = std::free>
struct SerializedExpression {
  RootExpression* root = nullptr;
  size_t argumentCount() const { return root->argumentCount; };
  size_t expressionCount() const { return root->expressionCount; };

  Argument* flattenedArguments() const { return getExpressionArguments(root); }
  ArgumentType* flattenedArgumentTypes() const { return getArgumentTypes(root); }
  Expression* expressionsBuffer() const { return getExpressionSubexpressions(root); }

  //////////////////////////////// Count Arguments ///////////////////////////////

  template <typename TupleLike, size_t... Is>
  static size_t countArgumentsInTuple(TupleLike const& tuple,
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

  template <typename TupleLike, size_t... Is>
  static size_t countExpressionsInTuple(TupleLike const& tuple,
                                        std::index_sequence<Is...> /*unused*/) {
    return (countExpressions(std::get<Is>(tuple)) + ... + 0);
  };

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

  // Overloads for ExpressionWithAdditionalCustomAtoms (used when iterating extended dynamics).
  template <typename... AdditionalCustomAtoms>
  static uint64_t countArguments(
      boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
          AdditionalCustomAtoms...> const& input) {
    return std::visit(
        [](auto const& a) -> uint64_t {
          using T = std::decay_t<decltype(a)>;
          if constexpr(boss::expressions::generic::isComplexExpression<T>) {
            return countArguments(a);
          }
          return 1;
        },
        input);
  }

  template <typename... AdditionalCustomAtoms>
  static uint64_t countExpressions(
      boss::expressions::generic::ExpressionWithAdditionalCustomAtoms<
          AdditionalCustomAtoms...> const& input) {
    return std::visit(
        [](auto const& a) -> uint64_t {
          using T = std::decay_t<decltype(a)>;
          if constexpr(boss::expressions::generic::isComplexExpression<T>) {
            return countExpressions(a);
          }
          return 0;
        },
        input);
  }

  // Overloads for ComplexExpressionWithAdditionalCustomAtoms (static-argument expressions).
  // Static args each occupy one argument slot; complex-expression statics are not yet counted.
  template <typename StaticArgsTuple, typename... AdditionalCustomAtoms>
  static uint64_t countArguments(
      boss::expressions::generic::ComplexExpressionWithAdditionalCustomAtoms<
          StaticArgsTuple, AdditionalCustomAtoms...> const& input) {
    return 1 + std::tuple_size_v<StaticArgsTuple> +
           std::accumulate(input.getDynamicArguments().begin(), input.getDynamicArguments().end(),
                           size_t{0}, [](size_t sum, auto const& arg) {
                             return sum + countArguments(arg);
                           }) +
           input.getSpanArguments().size();
  }

  template <typename StaticArgsTuple, typename... AdditionalCustomAtoms>
  static uint64_t countExpressions(
      boss::expressions::generic::ComplexExpressionWithAdditionalCustomAtoms<
          StaticArgsTuple, AdditionalCustomAtoms...> const& input) {
    return 1 + std::accumulate(input.getDynamicArguments().begin(),
                               input.getDynamicArguments().end(), size_t{0},
                               [](size_t sum, auto const& arg) {
                                 return sum + countExpressions(arg);
                               });
  }

  //////////////////////////////   Flatten Arguments /////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  void flattenArgumentsInTuple(TupleLike&& tuple, std::index_sequence<Is...> /*unused*/,
                               uint64_t& /*argumentOutputI*/) {
    std::bind([&](auto&&... a) { flattenArguments(a...); }, std::forward<decltype(tuple)>(tuple));
  };

  uint64_t flattenArguments(uint64_t argumentOutputI, std::vector<boss::ComplexExpression>&& inputs,
                            uint64_t& expressionOutputI) {
    auto const nextLayerOffset =
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
        [this, &argumentOutputI, &children, &expressionOutputI, nextLayerOffset,
         &childrenCountRunningSum](boss::ComplexExpression&& input) {
          auto [head, statics, dynamics, spans] = std::move(input).decompose();
          flattenArgumentsInTuple(
              statics,
              std::make_index_sequence<std::tuple_size_v<std::decay_t<decltype(statics)>>>(),
              argumentOutputI);
          std::for_each(
              std::make_move_iterator(dynamics.begin()), std::make_move_iterator(dynamics.end()),
              [this, &argumentOutputI, &children, &expressionOutputI, nextLayerOffset,
               &childrenCountRunningSum](auto&& argument) {
                std::visit(
                    [this, &children, &argumentOutputI, &expressionOutputI, nextLayerOffset,
                     &childrenCountRunningSum](auto&& argument) {
                      if constexpr(boss::expressions::generic::isComplexExpression<
                                       decltype(argument)>) {
                        auto const childrenCount =
                            std::tuple_size_v<
                                std::decay_t<decltype(argument.getStaticArguments())>> +
                            argument.getDynamicArguments().size() +
                            argument.getSpanArguments().size();
                        auto const headOffset = argumentOutputI;
                        auto const startChildOffset = nextLayerOffset + childrenCountRunningSum;
                        auto const endChildOffset =
                            nextLayerOffset + childrenCountRunningSum + childrenCount;
                        auto storedString = storeString(&root, argument.getHead().getName().c_str(),
                                                        reallocateFunction);
                        *makeExpression(root, expressionOutputI) =
                            PortableBOSSExpression {storedString, startChildOffset, endChildOffset};
                        *makeExpressionArgument(root, argumentOutputI++) = expressionOutputI++;
                        auto head = viewString(root, storedString);
                        childrenCountRunningSum += childrenCount;
                        children.push_back(std::forward<decltype(argument)>(argument));
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>, bool>) {
                        *makeBoolArgument(root, argumentOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         int8_t>) {
                        *makeCharArgument(root, argumentOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         int32_t>) {
                        *makeIntArgument(root, argumentOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         int64_t>) {
                        *makeLongArgument(root, argumentOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         float_t>) {
                        *makeFloatArgument(root, argumentOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         double_t>) {
                        *makeDoubleArgument(root, argumentOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         std::string>) {
                        auto storedString =
                            storeString(&root, argument.c_str(), reallocateFunction);
                        *makeStringArgument(root, argumentOutputI++) = storedString;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         boss::Symbol>) {
                        auto storedString =
                            storeString(&root, argument.getName().c_str(), reallocateFunction);
                        *makeSymbolArgument(root, argumentOutputI++) = storedString;
                      } else {
                        throw std::runtime_error("unknown type");
                      }
                    },
                    std::forward<decltype(argument)>(argument));
              });
        });
    auto _ = std::move(inputs); // make clang-tidy happy
    if(!children.empty()) {
      return flattenArguments(argumentOutputI, std::move(children), expressionOutputI);
    }
    return argumentOutputI;
  }

  ////////////////////////////////   Surface Area ////////////////////////////////

public:
  explicit SerializedExpression(boss::Expression&& input)
      : SerializedExpression(allocateExpressionTree(countArguments(input), countExpressions(input),
                                                    allocateFunction)) {
    std::visit(utilities::overload(
                   [this](boss::ComplexExpression&& input) {
                     auto argumentIterator = uint64_t {};
                     auto expressionIterator = uint64_t {};
                     auto const headOffset = 0;
                     auto const startChildOffset = 1;
                     auto const endChildOffset =
                         startChildOffset + input.getDynamicArguments().size();
                     auto storedString =
                         storeString(&root, input.getHead().getName().c_str(), reallocateFunction);
                     *makeExpression(root, expressionIterator) =
                         PortableBOSSExpression {storedString, startChildOffset, endChildOffset};
                     *makeExpressionArgument(root, argumentIterator++) = expressionIterator++;
                     auto inputs = std::vector<boss::ComplexExpression>();
                     inputs.push_back(std::move(input));
                     flattenArguments(argumentIterator, std::move(inputs), expressionIterator);
                   },
                   [this](expressions::atoms::Symbol&& input) {
                     auto storedString =
                         storeString(&root, std::move(input).getName().c_str(), reallocateFunction);
                     *makeSymbolArgument(root, 0) = storedString;
                   },
                   [this](bool input) { *makeBoolArgument(root, 0) = input; },
                   [this](std::int8_t input) { *makeCharArgument(root, 0) = input; },
                   [this](std::int32_t input) { *makeIntArgument(root, 0) = input; },
                   [this](std::int64_t input) { *makeLongArgument(root, 0) = input; },
                   [this](std::float_t input) { *makeFloatArgument(root, 0) = input; },
                   [this](std::double_t input) { *makeDoubleArgument(root, 0) = input; },
                   [](auto&&) {
                     throw std::logic_error("uncountered unknown type during serialization");
                   }),
               std::move(input));
  }

  // Promote static args to dynamics, then construct SerializedExpression from the result.
  // Called for expressions with non-empty StaticArgsTuple.
  template <typename StaticArgsTuple, typename... AdditionalCustomAtoms>
  static SerializedExpression makeFromStatic(
      boss::expressions::generic::ComplexExpressionWithAdditionalCustomAtoms<
          StaticArgsTuple, AdditionalCustomAtoms...>&& input) {
    using ExprType = boss::expressions::generic::ComplexExpressionWithAdditionalCustomAtoms<
        StaticArgsTuple, AdditionalCustomAtoms...>;
    // Under the assumption that custom atoms are never base expression types,
    // StaticAtomsTuple is always identical to StaticArgsTuple, and the extended
    // constructor handles the promoted type directly.
    using PromotedType =
        typename ExprType::template WithStaticTypesAsAtoms<
            typename ExprType::StaticAtomsTuple>::ComplexExpression;
    return SerializedExpression(std::move(input).operator PromotedType());
  }

  // Constructor for expressions with non-empty static args: promote statics to dynamics first.
  template <typename StaticArgsTuple, typename... AdditionalCustomAtoms,
            std::enable_if_t<(std::tuple_size_v<StaticArgsTuple> > 0), int> = 0>
  explicit SerializedExpression(
      boss::expressions::generic::ComplexExpressionWithAdditionalCustomAtoms<
          StaticArgsTuple, AdditionalCustomAtoms...>&& input)
      : SerializedExpression(makeFromStatic(std::move(input))) {}

  // Constructor for extended expressions (no static args, additional atoms in dynamics).
  // Serializes like boss::Expression but visits the extended variant so custom atoms
  // can be dispatched via serializeCustomAtom.
  template <typename... AdditionalCustomAtoms,
            std::enable_if_t<(sizeof...(AdditionalCustomAtoms) > 0), int> = 0>
  explicit SerializedExpression(
      boss::expressions::generic::ComplexExpressionWithAdditionalCustomAtoms<
          std::tuple<>, AdditionalCustomAtoms...>&& input)
      : SerializedExpression(
            allocateExpressionTree(countArguments(input), countExpressions(input),
                                   allocateFunction)) {
    auto argumentIterator = uint64_t{};
    auto expressionIterator = uint64_t{};
    auto const startChildOffset = uint64_t{1};
    auto const endChildOffset = startChildOffset + input.getDynamicArguments().size();
    auto storedString = storeString(&root, input.getHead().getName().c_str(), reallocateFunction);
    *makeExpression(root, expressionIterator) =
        PortableBOSSExpression{storedString, startChildOffset, endChildOffset};
    *makeExpressionArgument(root, argumentIterator++) = expressionIterator++;
    auto const nextLayerOffset = endChildOffset;
    auto children = std::vector<boss::ComplexExpression>();
    auto childrenCountRunningSum = 0UL;
    std::for_each(
        std::make_move_iterator(input.getDynamicArguments().begin()),
        std::make_move_iterator(input.getDynamicArguments().end()),
        [this, &argumentIterator, &children, &expressionIterator, nextLayerOffset,
         &childrenCountRunningSum](auto&& argument) {
          std::visit(
              [this, &children, &argumentOutputI = argumentIterator, &expressionOutputI =
                                                                         expressionIterator,
               nextLayerOffset, &childrenCountRunningSum](auto&& argument) {
                using ArgT = std::decay_t<decltype(argument)>;
                if constexpr(std::is_same_v<ArgT, boss::ComplexExpression>) {
                  auto const childrenCount = argument.getDynamicArguments().size() +
                                             argument.getSpanArguments().size();
                  auto const headOffset = argumentOutputI;
                  auto const startChildOffset = nextLayerOffset + childrenCountRunningSum;
                  auto const endChildOffset = startChildOffset + childrenCount;
                  auto storedString = storeString(&root, argument.getHead().getName().c_str(),
                                                  reallocateFunction);
                  *makeExpression(root, expressionOutputI) =
                      PortableBOSSExpression{storedString, startChildOffset, endChildOffset};
                  *makeExpressionArgument(root, argumentOutputI++) = expressionOutputI++;
                  childrenCountRunningSum += childrenCount;
                  children.push_back(std::forward<decltype(argument)>(argument));
                } else if constexpr(std::is_same_v<ArgT, bool>) {
                  *makeBoolArgument(root, argumentOutputI++) = argument;
                } else if constexpr(std::is_same_v<ArgT, int8_t>) {
                  *makeCharArgument(root, argumentOutputI++) = argument;
                } else if constexpr(std::is_same_v<ArgT, int32_t>) {
                  *makeIntArgument(root, argumentOutputI++) = argument;
                } else if constexpr(std::is_same_v<ArgT, int64_t>) {
                  *makeLongArgument(root, argumentOutputI++) = argument;
                } else if constexpr(std::is_same_v<ArgT, float_t>) {
                  *makeFloatArgument(root, argumentOutputI++) = argument;
                } else if constexpr(std::is_same_v<ArgT, double_t>) {
                  *makeDoubleArgument(root, argumentOutputI++) = argument;
                } else if constexpr(std::is_same_v<ArgT, std::string>) {
                  auto storedString = storeString(&root, argument.c_str(), reallocateFunction);
                  *makeStringArgument(root, argumentOutputI++) = storedString;
                } else if constexpr(std::is_same_v<ArgT, boss::Symbol>) {
                  auto storedString =
                      storeString(&root, argument.getName().c_str(), reallocateFunction);
                  *makeSymbolArgument(root, argumentOutputI++) = storedString;
                } else if constexpr(hasCustomAtomSerializer<ArgT>) {
                  *makeLongArgument(root, argumentOutputI++) =
                      static_cast<int64_t>(serializeCustomAtom(argument));
                } else {
                  throw std::runtime_error("unknown type");
                }
              },
              std::forward<decltype(argument)>(argument));
        });
    if(!children.empty()) {
      flattenArguments(argumentIterator, std::move(children), expressionIterator);
    }
  }

  explicit SerializedExpression(RootExpression* root) : root(root) {}

  boss::expressions::ExpressionArguments deserializeArguments(size_t startChildOffset,
                                                              size_t endChildOffset) {
    boss::expressions::ExpressionArguments arguments;
    for(auto childIndex = startChildOffset; childIndex < endChildOffset; childIndex++) {
      auto const& arg = flattenedArguments()[childIndex];
      auto const& type = flattenedArgumentTypes()[childIndex];
      auto const functors = std::unordered_map<ArgumentType, std::function<boss::Expression()>> {
          {ArgumentType::ARGUMENT_TYPE_BOOL, [&] { return (arg.asBool); }},
          {ArgumentType::ARGUMENT_TYPE_CHAR, [&] { return (arg.asChar); }},
          {ArgumentType::ARGUMENT_TYPE_INT, [&] { return (arg.asInt); }},
          {ArgumentType::ARGUMENT_TYPE_LONG, [&] { return (arg.asLong); }},
          {ArgumentType::ARGUMENT_TYPE_FLOAT, [&] { return (arg.asFloat); }},
          {ArgumentType::ARGUMENT_TYPE_DOUBLE, [&] { return (arg.asDouble); }},
          {ArgumentType::ARGUMENT_TYPE_SYMBOL,
           [&arg, this] { return boss::Symbol(viewString(root, arg.asString)); }},
          {ArgumentType::ARGUMENT_TYPE_EXPRESSION,
           [&arg, this]() -> boss::Expression {
             auto result = boss::expressions::ComplexExpression(
                 boss::Symbol(
                     viewString(root, expressionsBuffer()[arg.asExpression].symbolNameOffset)),
                 deserializeArguments(expressionsBuffer()[arg.asExpression].startChildOffset,
                                      expressionsBuffer()[arg.asExpression].endChildOffset));
             return result;
           }},
          {ArgumentType::ARGUMENT_TYPE_STRING,
           [&arg, this] { return std::string(viewString(root, arg.asString)); }}};
      arguments.push_back(functors.at(type)());
    }
    return arguments;
  }

  template <typename... Types> class variant {
    size_t const* typeTag;
    void* value;

  public:
    variant(size_t const* typeTag, void* value) : typeTag(typeTag), value(value) {}
  };

  class LazilyDeserializedExpression {
    SerializedExpression const& buffer;
    size_t argumentIndex;

    template <typename T> T as(Argument const& arg) const;
    template <> bool as<bool>(Argument const& arg) const { return arg.asBool; };
    template <> std::int8_t as<std::int8_t>(Argument const& arg) const { return arg.asChar; };
    template <> std::int32_t as<std::int32_t>(Argument const& arg) const { return arg.asInt; };
    template <> std::int64_t as<std::int64_t>(Argument const& arg) const { return arg.asLong; };
    template <> std::float_t as<std::float_t>(Argument const& arg) const { return arg.asFloat; };
    template <> std::double_t as<std::double_t>(Argument const& arg) const { return arg.asDouble; };
    template <> std::string as<std::string>(Argument const& arg) const {
      return viewString(buffer.root, arg.asString);
    };
    template <> boss::Symbol as<boss::Symbol>(Argument const& arg) const {
      return boss::Symbol(viewString(buffer.root, arg.asString));
    };

  public:
    LazilyDeserializedExpression(SerializedExpression const& buffer, size_t argumentIndex)
        : buffer(buffer), argumentIndex(argumentIndex) {}

    bool operator==(boss::Expression const& other) const {
      if(other.index() != buffer.flattenedArgumentTypes()[argumentIndex]) {
        return false;
      }
      auto const& argument = buffer.flattenedArguments()[argumentIndex];
      return std::visit(utilities::overload(
                            [&argument, this](boss::ComplexExpression const& e) {
                              auto expressionPosition = argument.asExpression;
                              assert(expressionPosition < buffer.expressionCount());
                              auto& startChildOffset =
                                  buffer.expressionsBuffer()[expressionPosition].startChildOffset;
                              auto& endChildOffset =
                                  buffer.expressionsBuffer()[expressionPosition].endChildOffset;
                              auto numberOfChildren = endChildOffset - startChildOffset;
                              if(numberOfChildren != e.getArguments().size()) {
                                return false;
                              }
                              auto result = true;
                              for(auto i = 0U; i < numberOfChildren; i++) {
                                auto subExpressionPosition = startChildOffset + i;
                                result &=
                                    (LazilyDeserializedExpression(buffer, subExpressionPosition) ==
                                     e.getDynamicArguments().at(i));
                              }
                              return result;
                            },
                            [&argument, this](auto v) { return as<decltype(v)>(argument) == v; }),
                        other);
      ;
    }
  };

  LazilyDeserializedExpression lazilyDeserialize() & { return {*this, 0}; };

  boss::Expression deserialize() && {
    switch(flattenedArgumentTypes()[0]) {
    case ArgumentType::ARGUMENT_TYPE_BOOL:
      return flattenedArguments()[0].asBool;
    case ArgumentType::ARGUMENT_TYPE_CHAR:
      return flattenedArguments()[0].asChar;
    case ArgumentType::ARGUMENT_TYPE_INT:
      return flattenedArguments()[0].asInt;
    case ArgumentType::ARGUMENT_TYPE_LONG:
      return flattenedArguments()[0].asLong;
    case ArgumentType::ARGUMENT_TYPE_FLOAT:
      return flattenedArguments()[0].asFloat;
    case ArgumentType::ARGUMENT_TYPE_DOUBLE:
      return flattenedArguments()[0].asDouble;
    case ArgumentType::ARGUMENT_TYPE_STRING:
      return viewString(root, flattenedArguments()[0].asString);
    case ArgumentType::ARGUMENT_TYPE_SYMBOL:
      return boss::Symbol(viewString(root, flattenedArguments()[0].asString));
    case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
      auto s = boss::Symbol(viewString(root, flattenedArguments()[0].asString));
      if(root->expressionCount == 0) {
        return s;
      }
      auto result = boss::ComplexExpression {
          s, deserializeArguments(1, expressionsBuffer()[0].endChildOffset)};
      return result;
    }
  };

  RootExpression* extractRoot() && {
    auto* root = this->root;
    this->root = nullptr;
    return root;
  };

  SerializedExpression(SerializedExpression&& other) noexcept
      : root(std::exchange(other.root, nullptr)) {}
  SerializedExpression(SerializedExpression const&) = delete;
  SerializedExpression& operator=(SerializedExpression&& other) noexcept {
    if(this != &other) {
      freeExpressionTree(root, freeFunction);
      root = std::exchange(other.root, nullptr);
    }
    return *this;
  }
  SerializedExpression& operator=(SerializedExpression const&) = delete;
  ~SerializedExpression() { freeExpressionTree(root, freeFunction); }
};

// NOLINTEND(cppcoreguidelines-pro-type-union-access)
} // namespace boss::serialization
