#include "BOSS.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include "Utilities.hpp"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <inttypes.h>
#include <iostream>
#include <iterator>
#include <omp.h>
#include <optional>
#include <string.h>
#include <type_traits>
#include <typeinfo>
#include <unordered_set>
#include <utility>
#include <variant>
#ifndef _MSC_VER
#include <cxxabi.h>
#include <memory>
#endif

// #define SERIALIZATION_DEBUG

extern "C" {
#include "PortableBOSSSerialization.h"
}

template <class T, class U> inline constexpr bool is_same_v = std::is_same<T, U>::value;

template <typename T> void print_type_name() {
  const char* typeName = typeid(T).name();

#ifndef _MSC_VER
  // Demangle the type name on GCC/Clang
  int status = -1;
  std::unique_ptr<char, void (*)(void*)> res{
      abi::__cxa_demangle(typeName, nullptr, nullptr, &status), std::free};
  std::cout << (status == 0 ? res.get() : typeName) << std::endl;
#else
  // On MSVC, typeid().name() returns a human-readable name.
  std::cout << typeName << std::endl;
#endif
}

namespace boss::serialization {
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)

static_assert(
    std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_BOOL, boss::Expression>, bool>,
    "type ids wrong");
static_assert(
    std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_CHAR, boss::Expression>, std::int8_t>,
    "type ids wrong");
static_assert(
    std::is_same_v<std::variant_alternative_t<ARGUMENT_TYPE_SHORT, boss::Expression>, std::int16_t>,
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

using std::literals::string_literals::operator""s; // NOLINT(misc-unused-using-decls) clang-tidy bug
using boss::utilities::operator""_;                // NOLINT(misc-unused-using-decls) clang-tidy bug

using Argument = PortableBOSSArgumentValue;
using ArgumentType = PortableBOSSArgumentType;
using Expression = PortableBOSSExpression;
using RootExpression = PortableBOSSRootExpression;

static const uint8_t& ArgumentType_RLE_MINIMUM_SIZE = PortableBOSSArgumentType_RLE_MINIMUM_SIZE;
static const uint8_t& ArgumentType_RLE_BIT = PortableBOSSArgumentType_RLE_BIT;
static const uint8_t& ArgumentType_MASK = PortableBOSSArgumentType_MASK;

constexpr uint64_t Argument_BOOL_SIZE = PortableBOSSArgument_BOOL_SIZE;
constexpr uint64_t Argument_CHAR_SIZE = PortableBOSSArgument_CHAR_SIZE;
constexpr uint64_t Argument_SHORT_SIZE = PortableBOSSArgument_SHORT_SIZE;
constexpr uint64_t Argument_INT_SIZE = PortableBOSSArgument_INT_SIZE;
constexpr uint64_t Argument_LONG_SIZE = PortableBOSSArgument_LONG_SIZE;
constexpr uint64_t Argument_FLOAT_SIZE = PortableBOSSArgument_FLOAT_SIZE;
constexpr uint64_t Argument_DOUBLE_SIZE = PortableBOSSArgument_DOUBLE_SIZE;
constexpr uint64_t Argument_STRING_SIZE = PortableBOSSArgument_STRING_SIZE;
constexpr uint64_t Argument_EXPRESSION_SIZE = PortableBOSSArgument_EXPRESSION_SIZE;

/**
 * Implements serialization/deserialization of a (complex) expression to/from a c-allocated buffer.
 * The buffer contains no pointers so it can be safely written to disk or passed to a different
 * processing using shared memory
 */
template <void* (*allocateFunction)(size_t) = std::malloc,
          void* (*reallocateFunction)(void*, size_t) = std::realloc,
          void (*freeFunction)(void*) = std::free>
struct SerializedExpression {
  using BOSSArgumentPair =
      std::pair<boss::expressions::ExpressionArguments, boss::expressions::ExpressionSpanArguments>;

  RootExpression* root = nullptr;
  uint64_t argumentCount() const { return root->argumentCount; };
  uint64_t argumentBytesCount() const { return root->argumentBytesCount; };
  uint64_t expressionCount() const { return root->expressionCount; };

  Argument* flattenedArguments() const { return getExpressionArguments(root); }
  ArgumentType* flattenedArgumentTypes() const { return getArgumentTypes(root); }
  Expression* expressionsBuffer() const { return getExpressionSubexpressions(root); }

  //////////////////////////////// Count Argument Bytes ///////////////////////////////

  // Current assumes that only values within spans can be packed into a single 8 byte arg value
  // All else is treated as an 8 bytes arg value
  // Note: To read values at a specific index in a packed span, the span size must be known
  template <typename TupleLike, uint64_t... Is>
  static uint64_t countArgumentBytesInTuple(TupleLike const& tuple,
                                            std::index_sequence<Is...> /*unused*/) {
    return (countArgumentBytes(std::get<Is>(tuple)) + ... + static_cast<uint64_t>(0));
  };

  static uint64_t countArgumentBytes(boss::Expression const& input) {
    return std::visit(
        [](auto& input) -> uint64_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
            return static_cast<uint64_t>(sizeof(Argument)) +
                   countArgumentBytesInTuple(
                       input.getStaticArguments(),
                       std::make_index_sequence<std::tuple_size_v<
                           std::decay_t<decltype(input.getStaticArguments())>>>()) +
                   std::accumulate(input.getDynamicArguments().begin(),
                                   input.getDynamicArguments().end(), uint64_t(0),
                                   [](uint64_t runningSum, auto const& argument) -> uint64_t {
                                     return runningSum + countArgumentBytes(argument);
                                   }) +
                   std::accumulate(
                       input.getSpanArguments().begin(), input.getSpanArguments().end(),
                       uint64_t(0), [](auto runningSum, auto const& argument) -> uint64_t {
                         return runningSum +
                                std::visit(
                                    [&](auto const& spanArgument) -> uint64_t {
                                      uint64_t spanBytes = 0;
                                      uint64_t spanSize = spanArgument.size();
                                      auto const& arg0 = spanArgument[0];
                                      if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
                                                                  bool> ||
                                                   std::is_same_v<std::decay_t<decltype(arg0)>,
                                                                  std::_Bit_reference>) {
                                        spanBytes = spanSize * static_cast<uint64_t>(sizeof(bool));
                                      } else if constexpr(std::is_same_v<
                                                              std::decay_t<decltype(arg0)>,
                                                              int8_t>) {
                                        spanBytes =
                                            spanSize * static_cast<uint64_t>(sizeof(int8_t));
                                      } else if constexpr(std::is_same_v<
                                                              std::decay_t<decltype(arg0)>,
                                                              int16_t>) {
                                        spanBytes =
                                            spanSize * static_cast<uint64_t>(sizeof(int16_t));
                                      } else if constexpr(std::is_same_v<
                                                              std::decay_t<decltype(arg0)>,
                                                              int32_t>) {
                                        spanBytes =
                                            spanSize * static_cast<uint64_t>(sizeof(int32_t));
                                      } else if constexpr(std::is_same_v<
                                                              std::decay_t<decltype(arg0)>,
                                                              int64_t>) {
                                        spanBytes =
                                            spanSize * static_cast<uint64_t>(sizeof(int64_t));
                                      } else if constexpr(std::is_same_v<
                                                              std::decay_t<decltype(arg0)>,
                                                              float_t>) {
                                        spanBytes =
                                            spanSize * static_cast<uint64_t>(sizeof(float_t));
                                      } else if constexpr(std::is_same_v<
                                                              std::decay_t<decltype(arg0)>,
                                                              double_t>) {
                                        spanBytes =
                                            spanSize * static_cast<uint64_t>(sizeof(double_t));
                                      } else if constexpr(std::is_same_v<
                                                              std::decay_t<decltype(arg0)>,
                                                              std::string>) {
                                        spanBytes =
                                            spanSize * static_cast<uint64_t>(sizeof(Argument));
                                      } else if constexpr(std::is_same_v<
                                                              std::decay_t<decltype(arg0)>,
                                                              boss::Symbol>) {
                                        spanBytes =
                                            spanSize * static_cast<uint64_t>(sizeof(Argument));
                                      } else {
                                        print_type_name<std::decay_t<decltype(arg0)>>();
                                        throw std::runtime_error("unknown type in span");
                                      }
                                      return (spanBytes + static_cast<uint64_t>(sizeof(Argument)) -
                                              1) &
                                             -(static_cast<uint64_t>(sizeof(Argument)));
                                    },
                                    std::forward<decltype(argument)>(argument));
                       });
          }
          return static_cast<uint64_t>(sizeof(Argument));
        },
        input);
  }

  //////////////////////////////// Count Arguments ///////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  static uint64_t countArgumentsInTuple(TupleLike const& tuple,
                                        std::index_sequence<Is...> /*unused*/) {
    return (countArguments(std::get<Is>(tuple)) + ... + 0);
  };

  static uint64_t countArguments(boss::Expression const& input) {
    return std::visit(
        [](auto& input) -> uint64_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
            return 1 +
                   countArgumentsInTuple(
                       input.getStaticArguments(),
                       std::make_index_sequence<std::tuple_size_v<
                           std::decay_t<decltype(input.getStaticArguments())>>>()) +
                   std::accumulate(input.getDynamicArguments().begin(),
                                   input.getDynamicArguments().end(), uint64_t(0),
                                   [](uint64_t runningSum, auto const& argument) -> uint64_t {
                                     return runningSum + countArguments(argument);
                                   }) +
                   std::accumulate(
                       input.getSpanArguments().begin(), input.getSpanArguments().end(),
                       uint64_t(0), [](uint64_t runningSum, auto const& argument) -> uint64_t {
                         return runningSum + std::visit(
                                                 [&](auto const& argument) -> uint64_t {
                                                   return argument.size();
                                                 },
                                                 std::forward<decltype(argument)>(argument));
                       });
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
                                   std::accumulate(
                                       input.getDynamicArguments().begin(),
                                       input.getDynamicArguments().end(), uint64_t(0),
                                       [](uint64_t runningSum, auto const& argument) -> uint64_t {
                                         return runningSum + countExpressions(argument);
                                       });
                          },
                          [](auto const&) -> uint64_t { return 0; }),
                      input);
  }

  //////////////////////////////// Count String Bytes ///////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  static uint64_t countStringBytesInTuple(std::unordered_set<std::string>& stringSet,
                                          bool dictEncodeStrings, TupleLike const& tuple,
                                          std::index_sequence<Is...> /*unused*/) {
    return (countStringBytes(std::get<Is>(tuple), stringSet, dictEncodeStrings) + ... + 0);
  };

  static uint64_t countStringBytes(boss::Expression const& input, bool dictEncodeStrings = true) {
    std::unordered_set<std::string> stringSet;
    return 1 + countStringBytes(input, stringSet, dictEncodeStrings);
  }

  static uint64_t countStringBytes(boss::Expression const& input,
                                   std::unordered_set<std::string>& stringSet,
                                   bool dictEncodeStrings) {
    return std::visit(
        [&](auto& input) -> uint64_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
            uint64_t headBytes =
                !dictEncodeStrings * (strlen(input.getHead().getName().c_str()) + 1);
            if(dictEncodeStrings && stringSet.find(input.getHead().getName()) == stringSet.end()) {
              stringSet.insert(input.getHead().getName());
              headBytes = strlen(input.getHead().getName().c_str()) + 1;
            }
            uint64_t staticArgsBytes = countStringBytesInTuple(
                stringSet, dictEncodeStrings, input.getStaticArguments(),
                std::make_index_sequence<
                    std::tuple_size_v<std::decay_t<decltype(input.getStaticArguments())>>>());
            uint64_t dynamicArgsBytes = std::accumulate(
                input.getDynamicArguments().begin(), input.getDynamicArguments().end(), uint64_t(0),
                [&](uint64_t runningSum, auto const& argument) -> uint64_t {
                  return runningSum + countStringBytes(argument, stringSet, dictEncodeStrings);
                });
            uint64_t spanArgsBytes = std::accumulate(
                input.getSpanArguments().begin(), input.getSpanArguments().end(), uint64_t(0),
                [&](size_t runningSum, auto const& argument) -> uint64_t {
                  return runningSum +
                         std::visit(
                             [&](auto const& argument) -> uint64_t {
                               if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                           boss::Span<std::string>>) {
                                 return std::accumulate(
                                     argument.begin(), argument.end(), uint64_t(0),
                                     [&](uint64_t innerRunningSum,
                                         auto const& stringArgument) -> uint64_t {
                                       uint64_t resRunningSum =
                                           innerRunningSum + (!dictEncodeStrings *
                                                              (strlen(stringArgument.c_str()) + 1));
                                       if(dictEncodeStrings &&
                                          stringSet.find(stringArgument) == stringSet.end()) {
                                         stringSet.insert(stringArgument);
                                         resRunningSum += strlen(stringArgument.c_str()) + 1;
                                       }
                                       return resRunningSum;
                                     });
                               } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                                  boss::Span<boss::Symbol>>) {
                                 return std::accumulate(
                                     argument.begin(), argument.end(), uint64_t(0),
                                     [&](uint64_t innerRunningSum,
                                         auto const& stringArgument) -> uint64_t {
                                       uint64_t resRunningSum =
                                           innerRunningSum +
                                           (!dictEncodeStrings *
                                            (strlen(stringArgument.getName().c_str()) + 1));
                                       if(dictEncodeStrings &&
                                          stringSet.find(stringArgument.getName()) ==
                                              stringSet.end()) {
                                         stringSet.insert(stringArgument.getName());
                                         resRunningSum +=
                                             strlen(stringArgument.getName().c_str()) + 1;
                                       }
                                       return resRunningSum;
                                     });
                               }
                               return 0;
                             },
                             std::forward<decltype(argument)>(argument));
                });

            return headBytes + staticArgsBytes + dynamicArgsBytes + spanArgsBytes;
          } else if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::Symbol>) {
            size_t res = !dictEncodeStrings * (strlen(input.getName().c_str()) + 1);
            if(dictEncodeStrings && stringSet.find(input.getName()) == stringSet.end()) {
              stringSet.insert(input.getName());
              res = strlen(input.getName().c_str()) + 1;
            }
            return res;
          } else if constexpr(std::is_same_v<std::decay_t<decltype(input)>, std::string>) {
            size_t res = !dictEncodeStrings * (strlen(input.c_str()) + 1);
            if(dictEncodeStrings && stringSet.find(input) == stringSet.end()) {
              stringSet.insert(input);
              res = strlen(input.c_str()) + 1;
            }
            return res;
          }
          return 0;
        },
        input);
  }

  //////////////////////////////   Flatten Arguments /////////////////////////////

  size_t checkMapAndStoreString(const std::string& key,
                                std::unordered_map<std::string, size_t>& stringMap,
                                bool dictEncodeStrings) {
    size_t storedString = 0;
    if(dictEncodeStrings) {
      auto it = stringMap.find(key);
      if(it == stringMap.end()) {
        storedString = storeString(&root, key.c_str());
        stringMap.emplace(key, storedString);
      } else {
        storedString = it->second;
      }
    } else {
      storedString = storeString(&root, key.c_str());
    }
    return storedString;
  }

  uint64_t countArgumentTypes(boss::ComplexExpression const& expression) {
    return std::tuple_size_v<std::decay_t<decltype(expression.getStaticArguments())>> +
           expression.getDynamicArguments().size() +
           std::accumulate(
               expression.getSpanArguments().begin(), expression.getSpanArguments().end(),
               uint64_t(0), [](uint64_t runningSum, auto const& spanArg) -> uint64_t {
                 return runningSum +
                        std::visit([&](auto const& spanArg) -> uint64_t { return spanArg.size(); },
                                   std::forward<decltype(spanArg)>(spanArg));
               });
  }

  uint64_t countArgumentsPacked(boss::ComplexExpression const& expression) {
    uint64_t staticsCount =
        std::tuple_size_v<std::decay_t<decltype(expression.getStaticArguments())>>;
    uint64_t dynamicsCount = expression.getDynamicArguments().size();
    uint64_t spansCount = std::accumulate(
        expression.getSpanArguments().begin(), expression.getSpanArguments().end(), uint64_t(0),
        [](uint64_t runningSum, auto const& spanArg) -> uint64_t {
          return runningSum +
                 std::visit(
                     [&](auto const& spanArgument) -> uint64_t {
                       uint64_t spanSize = spanArgument.size();
                       auto const& arg0 = spanArgument[0];
                       uint64_t valsPerArg = static_cast<uint64_t>(
                           sizeof(arg0) > sizeof(Argument) ? 1 : sizeof(Argument) / sizeof(arg0));
                       return (spanSize + valsPerArg - 1) / valsPerArg;
                     },
                     std::forward<decltype(spanArg)>(spanArg));
        });
    return staticsCount + dynamicsCount + spansCount;
  }

  template <typename TupleLike, uint64_t... Is>
  void flattenArgumentsInTuple(TupleLike&& tuple, std::index_sequence<Is...> /*unused*/,
                               uint64_t& argumentOutputI, uint64_t& typeOutputI,
                               std::unordered_map<std::string, size_t>& stringMap,
                               bool dictEncodeStrings) {
    (flattenArguments(std::get<Is>(tuple), argumentOutputI, typeOutputI, stringMap,
                      dictEncodeStrings),
     ...);
  };

  // assuming RLE encode for now
  uint64_t flattenArguments(uint64_t argumentOutputI, uint64_t typeOutputI,
                            std::vector<boss::ComplexExpression>&& inputs,
                            uint64_t& expressionOutputI, bool dictEncodeStrings = true) {
    std::unordered_map<std::string, size_t> stringMap;
    return flattenArguments(argumentOutputI, typeOutputI, std::move(inputs), expressionOutputI,
                            stringMap, dictEncodeStrings);
  }

  uint64_t flattenArguments(uint64_t argumentOutputI, uint64_t typeOutputI,
                            std::vector<boss::ComplexExpression>&& inputs,
                            uint64_t& expressionOutputI,
                            std::unordered_map<std::string, size_t>& stringMap,
                            bool dictEncodeStrings) {
    uint64_t const nextLayerTypeOffset =
        typeOutputI + std::accumulate(inputs.begin(), inputs.end(), uint64_t(0),
                                      [this](uint64_t count, auto const& expression) -> uint64_t {
                                        return count + countArgumentTypes(expression);
                                      });
    uint64_t const nextLayerOffset =
        argumentOutputI +
        std::accumulate(inputs.begin(), inputs.end(), uint64_t(0),
                        [this](uint64_t count, auto const& expression) -> uint64_t {
                          return count + countArgumentsPacked(expression);
                        });
    auto children = std::vector<boss::ComplexExpression>();
    uint64_t childrenCountRunningSum = 0UL;
    uint64_t childrenTypeCountRunningSum = 0UL;

    std::for_each(
        std::move_iterator(inputs.begin()), std::move_iterator(inputs.end()),
        [this, &argumentOutputI, &typeOutputI, &children, &expressionOutputI, nextLayerTypeOffset,
         nextLayerOffset, &childrenCountRunningSum, &childrenTypeCountRunningSum, &stringMap,
         &dictEncodeStrings](boss::ComplexExpression&& input) {
          auto [head, statics, dynamics, spans] = std::move(input).decompose();
          flattenArgumentsInTuple(
              statics,
              std::make_index_sequence<std::tuple_size_v<std::decay_t<decltype(statics)>>>(),
              argumentOutputI, typeOutputI, stringMap, dictEncodeStrings);
          std::for_each(
              std::make_move_iterator(dynamics.begin()), std::make_move_iterator(dynamics.end()),
              [this, &argumentOutputI, &typeOutputI, &children, &expressionOutputI,
               nextLayerTypeOffset, nextLayerOffset, &childrenCountRunningSum,
               &childrenTypeCountRunningSum, &stringMap, &dictEncodeStrings](auto&& argument) {
                std::visit(
                    [this, &children, &argumentOutputI, &typeOutputI, &expressionOutputI,
                     nextLayerTypeOffset, nextLayerOffset, &childrenCountRunningSum,
                     &childrenTypeCountRunningSum, &stringMap,
                     &dictEncodeStrings](auto&& argument) {
                      if constexpr(boss::expressions::generic::isComplexExpression<
                                       decltype(argument)>) {
                        uint64_t const childrenCount = countArgumentsPacked(argument);
                        uint64_t const childrenTypeCount = countArgumentTypes(argument);
                        uint64_t const startChildArgOffset =
                            nextLayerOffset + childrenCountRunningSum;
                        uint64_t const endChildArgOffset =
                            nextLayerOffset + childrenCountRunningSum + childrenCount;
                        uint64_t const startChildTypeOffset =
                            nextLayerTypeOffset + childrenTypeCountRunningSum;
                        uint64_t const endChildTypeOffset =
                            nextLayerTypeOffset + childrenTypeCountRunningSum + childrenTypeCount;
#ifdef SERIALIZATION_DEBUG
                        std::cout << "HEAD: " << argument.getHead().getName() << std::endl;
                        std::cout << "  argOutput: " << argumentOutputI << std::endl;
                        std::cout << "  typeOutput: " << typeOutputI << std::endl;
                        std::cout << "  exprOutput: " << expressionOutputI << std::endl;
                        std::cout << "  startChildArgOffset: " << startChildArgOffset << std::endl;
                        std::cout << "  endChildArgOffset: " << endChildArgOffset << std::endl;
                        std::cout << "  startChildArgTypeOffset: " << startChildTypeOffset
                                  << std::endl;
                        std::cout << "  endChildArgTypeOffset:" << endChildTypeOffset << std::endl;
#endif
                        auto storedString = checkMapAndStoreString(argument.getHead().getName(),
                                                                   stringMap, dictEncodeStrings);
                        *makeExpression(root, expressionOutputI) = PortableBOSSExpression{
                            storedString, startChildArgOffset, endChildArgOffset,
                            startChildTypeOffset, endChildTypeOffset};
                        *makeExpressionArgument(root, argumentOutputI++, typeOutputI++) =
                            expressionOutputI++;
                        auto head = viewString(root, storedString);
                        childrenCountRunningSum += childrenCount;
                        childrenTypeCountRunningSum += childrenTypeCount;
                        children.push_back(std::forward<decltype(argument)>(argument));
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>, bool>) {
                        *makeBoolArgument(root, argumentOutputI++, typeOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         int8_t>) {
                        *makeCharArgument(root, argumentOutputI++, typeOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         int16_t>) {
                        *makeShortArgument(root, argumentOutputI++, typeOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         int32_t>) {
                        *makeIntArgument(root, argumentOutputI++, typeOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         int64_t>) {
                        *makeLongArgument(root, argumentOutputI++, typeOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         float_t>) {
                        *makeFloatArgument(root, argumentOutputI++, typeOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         double_t>) {
                        *makeDoubleArgument(root, argumentOutputI++, typeOutputI++) = argument;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         std::string>) {
                        auto storedString =
                            checkMapAndStoreString(argument, stringMap, dictEncodeStrings);
                        *makeStringArgument(root, argumentOutputI++, typeOutputI++) = storedString;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         boss::Symbol>) {
                        auto storedString = checkMapAndStoreString(argument.getName(), stringMap,
                                                                   dictEncodeStrings);
                        *makeSymbolArgument(root, argumentOutputI++, typeOutputI++) = storedString;
                      } else {
                        print_type_name<std::decay_t<decltype(argument)>>();
                        throw std::runtime_error("unknown type");
                      }
                    },
                    std::forward<decltype(argument)>(argument));
              });
          std::for_each(
              std::make_move_iterator(spans.begin()), std::make_move_iterator(spans.end()),
              [this, &argumentOutputI, &typeOutputI, &stringMap,
               &dictEncodeStrings](auto&& argument) {
                std::visit(
                    [&](auto&& spanArgument) {
                      const size_t spanSize = spanArgument.size();
                      auto const& arg0 = spanArgument[0];
                      if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, bool> ||
                                   std::is_same_v<std::decay_t<decltype(arg0)>,
                                                  std::_Bit_reference>) {
                        size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
                        for(size_t i = 0; i < spanSize; i += valsPerArg) {
                          uint64_t tmp = 0;
                          for(size_t j = 0; j < valsPerArg && i + j < spanSize; j++) {
                            makeBoolArgumentType(root, typeOutputI++);
                            tmp |= static_cast<uint64_t>(spanArgument[i + j])
                                   << (Argument_BOOL_SIZE * sizeof(Argument) * j);
                          }
                          *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
                        }
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int8_t>) {
                        size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
                        for(size_t i = 0; i < spanSize; i += valsPerArg) {
                          uint64_t tmp = 0;
                          for(size_t j = 0; j < valsPerArg && i + j < spanSize; j++) {
                            makeCharArgumentType(root, typeOutputI++);
                            tmp |= static_cast<uint64_t>(spanArgument[i + j])
                                   << (Argument_CHAR_SIZE * sizeof(Argument) * j);
                          }
                          *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
                        }
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int16_t>) {
                        size_t valsPerArg = sizeof(Argument) / Argument_SHORT_SIZE;
                        for(size_t i = 0; i < spanSize; i += valsPerArg) {
                          uint64_t tmp = 0;
                          for(size_t j = 0; j < valsPerArg && i + j < spanSize; j++) {
                            makeShortArgumentType(root, typeOutputI++);
                            tmp |= static_cast<uint64_t>(spanArgument[i + j])
                                   << (Argument_SHORT_SIZE * sizeof(Argument) * j);
                          }
                          *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
                        }
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int32_t>) {
                        size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
                        for(size_t i = 0; i < spanSize; i += valsPerArg) {
                          uint64_t tmp = 0;
                          for(size_t j = 0; j < valsPerArg && i + j < spanSize; j++) {
                            makeIntArgumentType(root, typeOutputI++);
                            tmp |= static_cast<uint64_t>(spanArgument[i + j])
                                   << (Argument_INT_SIZE * sizeof(Argument) * j);
                          }
                          *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
                        }
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t>) {
                        std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                          *makeLongArgument(root, argumentOutputI++, typeOutputI++) = arg;
                        });
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, float_t>) {
                        size_t valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE;
                        for(size_t i = 0; i < spanSize; i += valsPerArg) {
                          uint64_t tmp = 0;
                          for(size_t j = 0; j < valsPerArg && i + j < spanSize; j++) {
                            uint32_t rawVal;
                            std::memcpy(&rawVal, &spanArgument[i + j], sizeof(rawVal));
                            makeFloatArgumentType(root, typeOutputI++);
                            tmp |= static_cast<uint64_t>(rawVal)
                                   << (Argument_FLOAT_SIZE * sizeof(Argument) * j);
                          }
                          *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
                        }
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, double_t>) {
                        std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                          *makeDoubleArgument(root, argumentOutputI++, typeOutputI++) = arg;
                        });
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
                                                         std::string>) {
                        std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                          auto storedString =
                              checkMapAndStoreString(arg, stringMap, dictEncodeStrings);
                          *makeStringArgument(root, argumentOutputI++, typeOutputI++) =
                              storedString;
                        });
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
                                                         boss::Symbol>) {
                        std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                          auto storedString =
                              checkMapAndStoreString(arg.getName(), stringMap, dictEncodeStrings);
                          *makeSymbolArgument(root, argumentOutputI++, typeOutputI++) =
                              storedString;
                        });
                      } else {
                        print_type_name<std::decay_t<decltype(arg0)>>();
                        throw std::runtime_error("unknown type");
                      }

                      if(spanSize >= ArgumentType_RLE_MINIMUM_SIZE) {
                        setRLEArgumentFlagOrPropagateTypes(root, typeOutputI - spanSize, spanSize);
                      }
                    },
                    std::forward<decltype(argument)>(argument));
              });
        });
    if(!children.empty()) {
      return flattenArguments(argumentOutputI, typeOutputI, std::move(children), expressionOutputI,
                              stringMap, dictEncodeStrings);
    }
    return argumentOutputI;
  }

  ////////////////////////////////   Surface Area ////////////////////////////////

public:
  explicit SerializedExpression(boss::Expression&& input, bool dictEncodeStrings = true) {
    root = allocateExpressionTree(countArguments(input), countArgumentBytes(input),
                                  countExpressions(input),
                                  countStringBytes(input, dictEncodeStrings), allocateFunction);
    std::visit(utilities::overload(
                   [this, &dictEncodeStrings](boss::ComplexExpression&& input) {
                     uint64_t argumentIterator = 0;
                     uint64_t typeIterator = 0;
                     uint64_t expressionIterator = 0;
                     uint64_t const childrenTypeCount = countArgumentTypes(input);
                     uint64_t const childrenCount = countArgumentsPacked(input);
                     uint64_t const startChildArgOffset = 1;
                     uint64_t const endChildArgOffset = startChildArgOffset + childrenCount;
                     uint64_t const startChildTypeOffset = 1;
                     uint64_t const endChildTypeOffset = startChildArgOffset + childrenTypeCount;
                     auto storedString = storeString(&root, input.getHead().getName().c_str());
                     *makeExpression(root, expressionIterator) = PortableBOSSExpression{
                         storedString, startChildArgOffset, endChildArgOffset, startChildTypeOffset,
                         endChildTypeOffset};
                     *makeExpressionArgument(root, argumentIterator++, typeIterator++) =
                         expressionIterator++;
                     auto inputs = std::vector<boss::ComplexExpression>();
                     inputs.push_back(std::move(input));
                     flattenArguments(argumentIterator, typeIterator, std::move(inputs),
                                      expressionIterator, dictEncodeStrings);
                   },
                   [this](expressions::atoms::Symbol&& input) {
                     auto storedString = storeString(&root, input.getName().c_str());
                     *makeSymbolArgument(root, 0) = storedString;
                   },
                   [this](bool input) { *makeBoolArgument(root, 0) = input; },
                   [this](std::int8_t input) { *makeCharArgument(root, 0) = input; },
                   [this](std::int16_t input) { *makeShortArgument(root, 0) = input; },
                   [this](std::int32_t input) { *makeIntArgument(root, 0) = input; },
                   [this](std::int64_t input) { *makeLongArgument(root, 0) = input; },
                   [this](std::float_t input) { *makeFloatArgument(root, 0) = input; },
                   [this](std::double_t input) { *makeDoubleArgument(root, 0) = input; },
                   [](auto&&) {
                     throw std::logic_error("uncountered unknown type during serialization");
                   }),
               std::move(input));
  }

  explicit SerializedExpression(RootExpression* root) : root(root) {}

  static void addIndexToStream(std::ostream& stream, SerializedExpression const& expr, size_t index,
                               size_t typeIndex, int64_t exprIndex, int64_t exprDepth) {
    for(auto i = 0; i < exprDepth; i++) {
      stream << "  ";
    }
    auto const& arguments = expr.flattenedArguments();
    auto const& types = expr.flattenedArgumentTypes();
    auto const& expressions = expr.expressionsBuffer();
    auto const& root = expr.root;

    auto testIndex = typeIndex;
    bool isRLE = (types[testIndex] & ArgumentType_RLE_BIT) != 0u;
    while(!isRLE && testIndex >= 0 && testIndex > typeIndex - 4) {
      testIndex--;
      isRLE |= (types[testIndex] & ArgumentType_RLE_BIT) != 0u;
    }
    auto validTypeIndex = isRLE ? testIndex : typeIndex;
    auto argumentType = static_cast<ArgumentType>(types[validTypeIndex] & ArgumentType_MASK);

    if(exprIndex < 0) {
      stream << "ARG INDEX: " << index << " TYPE INDEX: " << typeIndex << " VALUE: ";
    } else {
      stream << "ARG INDEX: " << index << " TYPE INDEX: " << typeIndex
             << " SUB-EXPR INDEX: " << exprIndex << " VALUE: ";
    }

    switch(argumentType) {
    case ArgumentType::ARGUMENT_TYPE_BOOL:
      stream << arguments[index].asBool << " TYPE: BOOL";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_CHAR:
      stream << arguments[index].asChar << " TYPE: CHAR";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_SHORT:
      stream << arguments[index].asShort << " TYPE: SHORT";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_INT:
      stream << arguments[index].asInt << " TYPE: INT";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_LONG:
      stream << arguments[index].asLong << " TYPE: LONG";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_FLOAT:
      stream << arguments[index].asFloat << " TYPE: FLOAT";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_DOUBLE:
      stream << arguments[index].asDouble << " TYPE: DOUBLE";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_STRING:
      stream << "( STR_OFFSET[" << arguments[index].asString << "], "
             << viewString(root, arguments[index].asString) << ")" << " TYPE: STRING";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_SYMBOL:
      stream << "( STR_OFFSET[" << arguments[index].asString << "], "
             << boss::Symbol(viewString(root, arguments[index].asString)) << ")" << " TYPE: SYMBOL";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
      auto const& expression = expressions[arguments[index].asExpression];
      auto s = boss::Symbol(viewString(root, expression.symbolNameOffset));
      stream << "( EXPR_OFFSET[" << arguments[index].asExpression << "], \n";
      for(auto i = 0; i < exprDepth + 1; i++) {
        stream << "  ";
      }
      stream << "HEAD: " << s << "\n";
      if(root->expressionCount == 0) {
        for(auto i = 0; i < exprDepth; i++) {
          stream << "  ";
        }
        stream << ")" << " TYPE: EXPRESSION\n";
      }
      for(auto childI = expression.startChildOffset, childTypeI = expression.startChildTypeOffset;
          childI < expression.endChildOffset && childTypeI < expression.endChildTypeOffset;
          childTypeI++) {

        bool isChildRLE = (types[childTypeI] & ArgumentType_RLE_BIT) != 0u;

        if(isChildRLE) {
          auto const argType = static_cast<ArgumentType>(types[childTypeI] & ArgumentType_MASK);
          uint32_t spanSize = (static_cast<uint32_t>(types[childTypeI + 4]) << 24) |
                              (static_cast<uint32_t>(types[childTypeI + 3]) << 16) |
                              (static_cast<uint32_t>(types[childTypeI + 2]) << 8) |
                              (static_cast<uint32_t>(types[childTypeI + 1]));
          auto prevChildTypeI = childTypeI;

          if(argType == ArgumentType::ARGUMENT_TYPE_BOOL) {
            auto valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
            for(; childTypeI < prevChildTypeI + spanSize; childI++) {
              int64_t& arg = arguments[childI].asLong;
              uint64_t tmp = static_cast<uint64_t>(arg);
              for(int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
                  i++, childTypeI++) {
                for(auto j = 0; j < exprDepth + 1; j++) {
                  stream << "  ";
                }
                stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI
                       << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset
                       << " VALUE: ";
                uint8_t val = static_cast<uint8_t>(
                    (tmp >> (Argument_BOOL_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                stream << static_cast<bool>(val) << " TYPE: BOOL";
                stream << "\n";
              }
            }
          } else if(argType == ArgumentType::ARGUMENT_TYPE_CHAR) {
            auto valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
            for(; childTypeI < prevChildTypeI + spanSize; childI++) {
              int64_t& arg = arguments[childI].asLong;
              uint64_t tmp = static_cast<uint64_t>(arg);
              for(int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
                  i++, childTypeI++) {
                for(auto j = 0; j < exprDepth + 1; j++) {
                  stream << "  ";
                }
                stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI
                       << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset
                       << " VALUE: ";
                uint8_t val = static_cast<uint8_t>(
                    (tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                stream << static_cast<int32_t>(val) << " TYPE: CHAR";
                stream << "\n";
              }
            }
          } else if(argType == ArgumentType::ARGUMENT_TYPE_SHORT) {
            auto valsPerArg = sizeof(Argument) / Argument_SHORT_SIZE;
            for(; childTypeI < prevChildTypeI + spanSize; childI++) {
              int64_t& arg = arguments[childI].asLong;
              uint64_t tmp = static_cast<uint64_t>(arg);
              for(int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
                  i++, childTypeI++) {
                for(auto j = 0; j < exprDepth + 1; j++) {
                  stream << "  ";
                }
                stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI
                       << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset
                       << " VALUE: ";
                uint16_t val = static_cast<uint16_t>(
                    (tmp >> (Argument_SHORT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                stream << static_cast<int32_t>(val) << " TYPE: SHORT";
                stream << "\n";
              }
            }
          } else if(argType == ArgumentType::ARGUMENT_TYPE_INT) {
            auto valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
            for(; childTypeI < prevChildTypeI + spanSize; childI++) {
              int64_t& arg = arguments[childI].asLong;
              uint64_t tmp = static_cast<uint64_t>(arg);
              for(int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
                  i++, childTypeI++) {
                for(auto j = 0; j < exprDepth + 1; j++) {
                  stream << "  ";
                }
                stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI
                       << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset
                       << " VALUE: ";
                uint32_t val = static_cast<uint32_t>(
                    (tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                stream << static_cast<int32_t>(val) << " TYPE: INT";
                stream << "\n";
              }
            }
          } else if(argType == ArgumentType::ARGUMENT_TYPE_LONG) {
            for(; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
              addIndexToStream(stream, expr, childI++, childTypeI,
                               childTypeI - expression.startChildTypeOffset, exprDepth + 1);
            }
          } else if(argType == ArgumentType::ARGUMENT_TYPE_FLOAT) {
            auto valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE;
            for(; childTypeI < prevChildTypeI + spanSize; childI++) {
              int64_t& arg = arguments[childI].asLong;
              uint64_t tmp = static_cast<uint64_t>(arg);
              for(int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
                  i++, childTypeI++) {
                for(auto j = 0; j < exprDepth + 1; j++) {
                  stream << "  ";
                }
                stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI
                       << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset
                       << " VALUE: ";
                uint32_t val = static_cast<uint32_t>(
                    (tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                float realVal;
                std::memcpy(&realVal, &val, sizeof(realVal));
                stream << static_cast<float>(val) << " TYPE: FLOAT";
                stream << "\n";
              }
            }
          } else if(argType == ArgumentType::ARGUMENT_TYPE_DOUBLE) {
            for(; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
              addIndexToStream(stream, expr, childI++, childTypeI,
                               childTypeI - expression.startChildTypeOffset, exprDepth + 1);
            }
          } else if(argType == ArgumentType::ARGUMENT_TYPE_STRING) {
            for(; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
              addIndexToStream(stream, expr, childI++, childTypeI,
                               childTypeI - expression.startChildTypeOffset, exprDepth + 1);
            }
          } else if(argType == ArgumentType::ARGUMENT_TYPE_SYMBOL) {
            for(; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
              addIndexToStream(stream, expr, childI++, childTypeI,
                               childTypeI - expression.startChildTypeOffset, exprDepth + 1);
            }
          }
          --childTypeI;
        } else {
          addIndexToStream(stream, expr, childI++, childTypeI,
                           childTypeI - expression.startChildTypeOffset, exprDepth + 1);
        }
      }
      for(auto i = 0; i < exprDepth; i++) {
        stream << "  ";
      }
      stream << ")" << " TYPE: EXPRESSION";
      stream << "\n";
      return;
    }
  }

  friend std::ostream& operator<<(std::ostream& stream, SerializedExpression const& expr) {
    addIndexToStream(stream, expr, 0, 0, -1, 0);
    return stream;
  }

  BOSSArgumentPair deserializeArguments(uint64_t startChildOffset, uint64_t endChildOffset,
                                        uint64_t startChildTypeOffset,
                                        uint64_t endChildTypeOffset) const {
    boss::expressions::ExpressionArguments arguments;
    boss::expressions::ExpressionSpanArguments spanArguments;
    for(auto childTypeIndex = startChildTypeOffset, childArgIndex = startChildOffset;
        childTypeIndex < endChildTypeOffset && childArgIndex < endChildOffset;
        childTypeIndex++, childArgIndex++) {
      auto const& type = flattenedArgumentTypes()[childTypeIndex];
      auto const& isRLE = (type & ArgumentType_RLE_BIT) != 0U;

      if(isRLE) {

        auto const argType = static_cast<ArgumentType>(type & ArgumentType_MASK);
        uint32_t size =
            (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 4]) << 24) |
            (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 3]) << 16) |
            (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 2]) << 8) |
            (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 1]));
        auto prevChildTypeIndex = childTypeIndex;

        auto const spanFunctors = std::unordered_map<
            ArgumentType, std::function<boss::expressions::ExpressionSpanArgument()>>{
            {ArgumentType::ARGUMENT_TYPE_BOOL,
             [&] {
               std::vector<bool> data;
               data.reserve(size);
               size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
               for(; childTypeIndex < prevChildTypeIndex + size;) {
                 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
                 uint64_t tmp = static_cast<uint64_t>(arg);
                 for(int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
                     i++, childTypeIndex++) {
                   uint8_t val = static_cast<uint8_t>(
                       (tmp >> (Argument_BOOL_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                   data.push_back(static_cast<bool>(val));
                 }
               }
               return boss::expressions::Span<bool>(std::move(data));
             }},
            {ArgumentType::ARGUMENT_TYPE_CHAR,
             [&] {
               std::vector<int8_t> data;
               data.reserve(size);
               size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
               for(; childTypeIndex < prevChildTypeIndex + size;) {
                 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
                 uint64_t tmp = static_cast<uint64_t>(arg);
                 for(int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
                     i++, childTypeIndex++) {
                   uint8_t val = static_cast<uint8_t>(
                       (tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                   data.push_back(static_cast<int8_t>(val));
                 }
               }
               return boss::expressions::Span<int8_t>(std::move(data));
             }},
            {ArgumentType::ARGUMENT_TYPE_SHORT,
             [&] {
               std::vector<int16_t> data;
               data.reserve(size);
               size_t valsPerArg = sizeof(Argument) / Argument_SHORT_SIZE;
               for(; childTypeIndex < prevChildTypeIndex + size;) {
                 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
                 uint64_t tmp = static_cast<uint64_t>(arg);
                 for(int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
                     i++, childTypeIndex++) {
                   uint16_t val = static_cast<uint16_t>(
                       (tmp >> (Argument_SHORT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                   data.push_back(static_cast<int16_t>(val));
                 }
               }
               return boss::expressions::Span<int16_t>(std::move(data));
             }},
            {ArgumentType::ARGUMENT_TYPE_INT,
             [&] {
               std::vector<int32_t> data;
               data.reserve(size);
               size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
               for(; childTypeIndex < prevChildTypeIndex + size;) {
                 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
                 uint64_t tmp = static_cast<uint64_t>(arg);
                 for(int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
                     i++, childTypeIndex++) {
                   uint32_t val = static_cast<uint32_t>(
                       (tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                   data.push_back(static_cast<int32_t>(val));
                 }
               }
               return boss::expressions::Span<int32_t>(std::move(data));
             }},
            {ArgumentType::ARGUMENT_TYPE_LONG,
             [&] {
               std::vector<int64_t> data;
               data.reserve(size);
               for(; childTypeIndex < prevChildTypeIndex + size;
                   childTypeIndex++, childArgIndex++) {
                 auto const& arg = flattenedArguments()[childArgIndex];
                 data.push_back(arg.asLong);
               }
               return boss::expressions::Span<int64_t>(std::move(data));
             }},
            {ArgumentType::ARGUMENT_TYPE_FLOAT,
             [&] {
               std::vector<float> data;
               data.reserve(size);
               size_t valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE;
               for(; childTypeIndex < prevChildTypeIndex + size;) {
                 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
                 uint64_t tmp = static_cast<uint64_t>(arg);
                 for(int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
                     i++, childTypeIndex++) {
                   uint32_t val = static_cast<uint32_t>(
                       (tmp >> (Argument_FLOAT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
                   float realVal;
                   std::memcpy(&realVal, &val, sizeof(realVal));
                   data.push_back(realVal);
                 }
               }
               return boss::expressions::Span<float>(std::move(data));
             }},
            {ArgumentType::ARGUMENT_TYPE_DOUBLE,
             [&] {
               std::vector<double_t> data;
               data.reserve(size);
               for(; childTypeIndex < prevChildTypeIndex + size;
                   childTypeIndex++, childArgIndex++) {
                 auto const& arg = flattenedArguments()[childArgIndex];
                 data.push_back(arg.asDouble);
               }
               return boss::expressions::Span<double_t>(std::move(data));
             }},
            {ArgumentType::ARGUMENT_TYPE_SYMBOL,
             [&childArgIndex, &childTypeIndex, &prevChildTypeIndex, &size, this] {
               std::vector<boss::Symbol> data;
               data.reserve(size);
               auto spanArgument = boss::expressions::Span<boss::Symbol>();
               for(; childTypeIndex < prevChildTypeIndex + size;
                   childTypeIndex++, childArgIndex++) {
                 auto const& arg = flattenedArguments()[childArgIndex];
                 data.push_back(boss::Symbol(viewString(root, arg.asString)));
               }
               return boss::expressions::Span<boss::Symbol>(std::move(data));
             }},
            {ArgumentType::ARGUMENT_TYPE_STRING,
             [&childArgIndex, &childTypeIndex, &prevChildTypeIndex, &size, this] {
               std::vector<std::string> data;
               data.reserve(size);
               for(; childTypeIndex < prevChildTypeIndex + size;
                   childTypeIndex++, childArgIndex++) {
                 auto const& arg = flattenedArguments()[childArgIndex];
                 data.push_back(std::string(viewString(root, arg.asString)));
               }
               return boss::expressions::Span<std::string>(std::move(data));
             }}};

        spanArguments.push_back(spanFunctors.at(argType)());
        childTypeIndex--;
        childArgIndex--;

      } else {
        auto const& arg = flattenedArguments()[childArgIndex];
        auto const functors = std::unordered_map<ArgumentType, std::function<boss::Expression()>>{
            {ArgumentType::ARGUMENT_TYPE_BOOL, [&] { return (arg.asBool); }},
            {ArgumentType::ARGUMENT_TYPE_CHAR, [&] { return (arg.asChar); }},
            {ArgumentType::ARGUMENT_TYPE_SHORT, [&] { return (arg.asShort); }},
            {ArgumentType::ARGUMENT_TYPE_INT, [&] { return (arg.asInt); }},
            {ArgumentType::ARGUMENT_TYPE_LONG, [&] { return (arg.asLong); }},
            {ArgumentType::ARGUMENT_TYPE_FLOAT, [&] { return (arg.asFloat); }},
            {ArgumentType::ARGUMENT_TYPE_DOUBLE, [&] { return (arg.asDouble); }},
            {ArgumentType::ARGUMENT_TYPE_SYMBOL,
             [&arg, this] { return boss::Symbol(viewString(root, arg.asString)); }},
            {ArgumentType::ARGUMENT_TYPE_EXPRESSION,
             [&arg, this]() -> boss::Expression {
               auto const& expr = expressionsBuffer()[arg.asExpression];
               auto [args, spanArgs] =
                   deserializeArguments(expr.startChildOffset, expr.endChildOffset,
                                        expr.startChildTypeOffset, expr.endChildTypeOffset);
               auto result = boss::expressions::ComplexExpression(
                   boss::Symbol(viewString(root, expr.symbolNameOffset)), {}, std::move(args),
                   std::move(spanArgs));
               return result;
             }},
            {ArgumentType::ARGUMENT_TYPE_STRING,
             [&arg, this] { return std::string(viewString(root, arg.asString)); }}};
        arguments.push_back(functors.at(type)());
      }
    }
    return std::make_pair(std::move(arguments), std::move(spanArguments));
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
    size_t typeIndex;

    template <typename T> T as(Argument const& arg) const;
    template <> bool as<bool>(Argument const& arg) const { return arg.asBool; };
    template <> std::int8_t as<std::int8_t>(Argument const& arg) const { return arg.asChar; };
    template <> std::int16_t as<std::int16_t>(Argument const& arg) const { return arg.asShort; };
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
    LazilyDeserializedExpression(SerializedExpression const& buffer, size_t argumentIndex,
                                 size_t typeIndex = 0)
        : buffer(buffer), argumentIndex(argumentIndex),
          typeIndex(typeIndex == 0 ? argumentIndex : typeIndex) {}

    size_t getArgumentIndex() const { return argumentIndex; }
    size_t getTypeIndex() const { return typeIndex; }

    bool operator==(boss::Expression const& other) const {
      if(other.index() != buffer.flattenedArgumentTypes()[typeIndex]) {
        return false;
      }
      auto const& argument = buffer.flattenedArguments()[argumentIndex];
      return std::visit(
          utilities::overload(
              [&argument, this](boss::ComplexExpression const& e) {
                auto expressionPosition = argument.asExpression;
                assert(expressionPosition < buffer.expressionCount());
                auto& startChildOffset =
                    buffer.expressionsBuffer()[expressionPosition].startChildOffset;
                auto& endChildOffset =
                    buffer.expressionsBuffer()[expressionPosition].endChildOffset;
                auto& startChildTypeOffset =
                    buffer.expressionsBuffer()[expressionPosition].startChildTypeOffset;
                auto& endChildTypeOffset =
                    buffer.expressionsBuffer()[expressionPosition].endChildTypeOffset;
                auto numberOfChildrenTypes = endChildTypeOffset - startChildTypeOffset;
                if(numberOfChildrenTypes != e.getArguments().size()) {
                  return false;
                }
                auto result = true;
                auto argI = 0U;
                auto typeI = 0U;
                for(; typeI < e.getDynamicArguments().size(); typeI++, argI++) {
                  auto subExpressionPosition = startChildOffset + argI;
                  auto subExpressionTypePosition = startChildTypeOffset + typeI;
                  result &= (LazilyDeserializedExpression(buffer, subExpressionPosition,
                                                          subExpressionTypePosition) ==
                             e.getDynamicArguments().at(typeI));
                }
                for(auto j = 0; j < e.getSpanArguments().size(); j++) {
                  std::visit(
                      [&](auto&& typedSpanArg) {
                        auto subSpanPosition = startChildOffset + argI;
                        auto subSpanTypePosition = startChildTypeOffset + typeI;
                        auto currSpan = (LazilyDeserializedExpression(buffer, subSpanPosition,
                                                                      subSpanTypePosition))
                                            .getCurrentExpressionAsSpan(false);
                        result &= std::visit(
                            [&](auto&& typedCurrSpan) {
                              if(typedCurrSpan.size() != typedSpanArg.size()) {
                                return false;
                              }
                              using Curr = std::decay_t<decltype(typedCurrSpan)>;
                              using Other = std::decay_t<decltype(typedSpanArg)>;
                              if constexpr(!is_same_v<Curr, Other>) {
                                return false;
                              } else {
                                auto res = true;
                                for(auto k = 0; k < typedCurrSpan.size(); k++) {
                                  auto first = typedCurrSpan.at(k);
                                  auto second = typedSpanArg.at(k);
                                  res &= first == second;
                                }
                                return res;
                              }
                            },
                            currSpan);
                        typeI += typedSpanArg.size();
                        const auto& arg0 = typedSpanArg[0];
                        auto valsPerArg =
                            sizeof(arg0) > sizeof(Argument) ? 1 : sizeof(Argument) / sizeof(arg0);
                        argI += (typedSpanArg.size() + valsPerArg - 1) / valsPerArg;
                      },
                      e.getSpanArguments().at(j));
                }
                return result;
              },
              [&argument, this](auto v) { return as<decltype(v)>(argument) == v; }),
          other);
      ;
    }

    friend std::ostream& operator<<(std::ostream& stream, LazilyDeserializedExpression lazyExpr) {
      lazyExpr.buffer.addIndexToStream(stream, lazyExpr.buffer, lazyExpr.argumentIndex,
                                       lazyExpr.typeIndex, -1, 0);
      return stream;
    }

    ArgumentType getCurrentExpressionType() const {
      auto stopTypeIndex = typeIndex < (ArgumentType_RLE_MINIMUM_SIZE - 1)
                               ? 0
                               : typeIndex - (ArgumentType_RLE_MINIMUM_SIZE - 1);
      auto testIndex = typeIndex;
      bool isRLE = (buffer.flattenedArgumentTypes()[testIndex] & ArgumentType_RLE_BIT) != 0u;
      while(!isRLE && testIndex >= 0 && testIndex > stopTypeIndex) {
        testIndex--;
        isRLE |= (buffer.flattenedArgumentTypes()[testIndex] & ArgumentType_RLE_BIT) != 0u;
      }
      auto validTypeIndex = isRLE ? testIndex : typeIndex;
      auto const& type = buffer.flattenedArgumentTypes()[validTypeIndex];
      return static_cast<ArgumentType>(type & ArgumentType_MASK);
    }

    ArgumentType getCurrentExpressionTypeExact() const {
      auto const& type = buffer.flattenedArgumentTypes()[typeIndex];
      return static_cast<ArgumentType>(type & ArgumentType_MASK);
    }

    LazilyDeserializedExpression operator()(size_t childOffset, size_t childTypeOffset) const {
      auto const& expr = expression();
#ifdef SERIALIZATION_DEBUG
      std::cout << "START CHILD OFFSET: " << expr.startChildOffset << std::endl;
      std::cout << "END CHILD OFFSET: " << expr.endChildOffset << std::endl;
      std::cout << "START CHILD TYPE OFFSET: " << expr.startChildTypeOffset << std::endl;
      std::cout << "END CHILD TYPE OFFSET: " << expr.endChildTypeOffset << std::endl;
#endif
      assert(childOffset < expr.endChildOffset - expr.startChildOffset);
      assert(childTypeOffset < expr.endChildTypeOffset - expr.startChildTypeOffset);
      return {buffer, expr.startChildOffset + childOffset,
              expr.startChildTypeOffset + childTypeOffset};
    }

    LazilyDeserializedExpression operator[](size_t childOffset) const {
      auto const& expr = expression();
      assert(childOffset < expr.endChildOffset - expr.startChildOffset);
      assert(childOffset < expr.endChildTypeOffset - expr.startChildTypeOffset);
      return {buffer, expr.startChildOffset + childOffset, expr.startChildTypeOffset + childOffset};
    }

    LazilyDeserializedExpression operator[](std::string const& keyName) const {
      auto const& expr = expression();
      auto const& arguments = buffer.flattenedArguments();
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& expressions = buffer.expressionsBuffer();
      for(auto i = expr.startChildOffset, typeI = expr.startChildTypeOffset;
          i < expr.endChildOffset && typeI < expr.endChildTypeOffset; ++i, ++typeI) {
        if(argumentTypes[typeI] != ArgumentType::ARGUMENT_TYPE_EXPRESSION) {
          continue;
        }
        auto const& child = expressions[arguments[i].asExpression];
        auto const& key = viewString(buffer.root, child.symbolNameOffset);
        if(std::string_view{key} == keyName) {
          return {buffer, i};
        }
      }
      throw std::runtime_error(keyName + " not found.");
    }

    // Assumes expressions do not run
    Expression const& expression() const {
      auto const& arguments = buffer.flattenedArguments();
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& expressions = buffer.expressionsBuffer();
      assert(argumentTypes[typeIndex] == ArgumentType::ARGUMENT_TYPE_EXPRESSION);
      return expressions[arguments[argumentIndex].asExpression];
    }

    size_t getCurrentExpressionAsExpressionOffset() const {
      auto const& arguments = buffer.flattenedArguments();
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      assert(argumentTypes[typeIndex] == ArgumentType::ARGUMENT_TYPE_EXPRESSION);
      return arguments[argumentIndex].asExpression;
    }

    size_t getCurrentExpressionAsString(bool partOfRLE) const {
      auto const& type = getCurrentExpressionType();
      if(!partOfRLE) {
        assert(type == ArgumentType::ARGUMENT_TYPE_STRING ||
               type == ArgumentType::ARGUMENT_TYPE_SYMBOL);
      }
      return buffer.flattenedArguments()[argumentIndex].asString;
    }

    size_t getCurrentExpressionAsStringUnconditional() const {
      return buffer.flattenedArguments()[argumentIndex].asString;
    }

    std::vector<size_t>
    getCurrentExpressionAsStringOffsetsVector(size_t size, bool startFromExpression = true) const {
      std::vector<size_t> res(size);
      const auto& arguments = buffer.flattenedArguments();
      size_t startOffset = argumentIndex;
      if(startFromExpression) {
        auto const& expr = expression();
        startOffset = expr.startChildOffset;
      }

      for(size_t i = 0; i < size; i++) {
        res[i] = arguments[startOffset + i].asString;
      }
      return res;
    }

    template <typename T>
    std::vector<size_t>
    getCurrentExpressionAsStringOffsetsVectorWithIndices(const std::vector<T>& indices,
                                                         bool startFromExpression = true) const {
      const size_t n = indices.size();
      std::vector<size_t> res(n);
      const auto& arguments = buffer.flattenedArguments();
      size_t startOffset = argumentIndex;
      if(startFromExpression) {
        auto const& expr = expression();
        startOffset = expr.startChildOffset;
      }

      for(size_t i = 0; i < n; i++) {
        res[i] = arguments[startOffset + indices[i]].asString;
      }
      return res;
    }

    bool currentIsExpression() const {
      auto const& argumentType = (buffer.flattenedArgumentTypes()[typeIndex] & ArgumentType_MASK);
      return argumentType == ArgumentType::ARGUMENT_TYPE_EXPRESSION;
    }

    size_t currentIsRLE() const {
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& type = argumentTypes[typeIndex];
      auto const& isRLE = (type & ArgumentType_RLE_BIT) != 0u;
      if(isRLE) {
        uint32_t size = (static_cast<uint32_t>(argumentTypes[typeIndex + 4]) << 24) |
                        (static_cast<uint32_t>(argumentTypes[typeIndex + 3]) << 16) |
                        (static_cast<uint32_t>(argumentTypes[typeIndex + 2]) << 8) |
                        (static_cast<uint32_t>(argumentTypes[typeIndex + 1]));
        return size;
      }
      return 0;
    }

    boss::Symbol getCurrentExpressionHead() const {
      auto const& expr = expression();
      return boss::Symbol(viewString(buffer.root, expr.symbolNameOffset));
    }

    template <typename IntT> static inline IntT extractField(uint64_t tmp, size_t shiftAmt) {
      return static_cast<IntT>((tmp >> shiftAmt) & 0xFFFFFFFFUL);
    }

    template <typename T, typename S, typename U>
    boss::expressions::ExpressionSpanArguments
    getCurrentExpressionAsSpanAsTypeWithIndices(const std::vector<U>& indices, int64_t spanSize,
                                                int64_t numSpansOut, int64_t numThreads = 1,
                                                bool startFromExpression = true) const {
      static_assert(std::is_convertible<T, S>::value,
                    "Cannot convert stored type to requested span type in "
                    "getCurrentExpressionAsSpanAsTypeWithIndices. Change from <T, S, U> as T "
                    "cannot be converted to S.");
      if(spanSize > 0 && numSpansOut > 0) {
        throw std::runtime_error("Cannot call getCurrentExpressionAsSpanAsTypeWithIndices with "
                                 "non-zero spanSize and numSpansOut");
      }
      constexpr size_t THREADING_THRESHOLD = 256000;
      auto const& arguments = buffer.flattenedArguments();
      size_t startOffset = argumentIndex;
      if(startFromExpression) {
        auto const& expr = expression();
        startOffset = expr.startChildOffset;
      }

      const size_t n = indices.size();
      constexpr size_t valsPerArg = sizeof(T) > sizeof(Argument) ? 1 : sizeof(Argument) / sizeof(T);
      constexpr size_t shiftAmt = sizeof(T) > sizeof(Argument) ? sizeof(Argument) * sizeof(Argument)
                                                               : sizeof(Argument) * sizeof(T);

      constexpr size_t valsPerArgMask = valsPerArg - 1;
      constexpr size_t valsPerArgShift = [] {
        size_t s = 0;
        size_t v = valsPerArg;
        while((v >>= 1) > 0)
          ++s;
        return s;
      }();

      if(spanSize <= 0 && numSpansOut <= 0) {
        spanSize = n;
        numSpansOut = 1;
      } else if(spanSize <= 0 && numSpansOut > 0) {
        size_t nPerSpan = (n + numSpansOut - 1) / numSpansOut;
        spanSize = nPerSpan;
      } else if(numSpansOut <= 0 && spanSize > 0) {
        size_t numSpansUnderEst = n / spanSize;
        size_t remainder = n % spanSize;
        numSpansOut = numSpansUnderEst + (remainder == 0 ? 0 : 1);
      }

      boss::expressions::ExpressionSpanArguments res;
      res.reserve(numSpansOut);

      for(size_t spanI = 0; spanI < n; spanI += spanSize) {
        size_t currSize = spanSize < (n - spanI) ? spanSize : (n - spanI);
        if constexpr(std::is_same_v<T, boss::Symbol>) {
          std::vector<S> data;
          data.reserve(currSize);
          for(size_t i = 0; i < currSize; i++) {
            const auto& index = indices[i + spanI];
            size_t childOffset = index >> valsPerArgShift;
            int64_t inArgI = (index & valsPerArgMask);

            auto& arg = arguments[startOffset + childOffset];
            uint64_t tmp = static_cast<uint64_t>(arg.asLong);
            data.emplace_back(static_cast<S>(viewString(buffer.root, arg.asString)));
          }
          res.push_back(std::move(boss::expressions::Span<S>(std::move(data))));
        } else {
          std::vector<S> data(currSize);

          if(n > THREADING_THRESHOLD && currSize > THREADING_THRESHOLD &&
             (numSpansOut != 1 || numThreads != 1)) {
            int64_t numThreadsToUse = numSpansOut != 1 ? 20 : numThreads;
#pragma omp parallel for schedule(static) num_threads(numThreadsToUse)
            for(size_t i = 0; i < currSize; i++) {
              const auto& index = indices[i + spanI];
              size_t childOffset = index >> valsPerArgShift;
              int64_t inArgI = (index & valsPerArgMask);

              auto& arg = arguments[startOffset + childOffset];
              uint64_t tmp = static_cast<uint64_t>(arg.asLong);

              if constexpr(std::is_same_v<T, bool>) {
                data[i] = static_cast<S>(
                    static_cast<bool>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
              } else if constexpr(std::is_same_v<T, int8_t>) {
                data[i] = static_cast<S>(
                    static_cast<int8_t>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
              } else if constexpr(std::is_same_v<T, int16_t>) {
                data[i] = static_cast<S>(
                    static_cast<int16_t>(extractField<uint16_t>(tmp, shiftAmt * inArgI)));
              } else if constexpr(std::is_same_v<T, int32_t>) {
                data[i] = static_cast<S>(
                    static_cast<int32_t>(extractField<uint32_t>(tmp, shiftAmt * inArgI)));
              } else if constexpr(std::is_same_v<T, float_t>) {
                uint32_t val = extractField<uint32_t>(tmp, shiftAmt * inArgI);
                union {
                  uint32_t iVal;
                  float f;
                } u;
                u.iVal = val;
                data[i] = static_cast<S>(u.f);
              } else if constexpr(std::is_same_v<T, int64_t>) {
                data[i] = static_cast<S>(arg.asLong);
              } else if constexpr(std::is_same_v<T, double_t>) {
                data[i] = static_cast<S>(arg.asDouble);
              } else if constexpr(std::is_same_v<T, std::string>) {
                data[i] = static_cast<S>(std::string(viewString(buffer.root, arg.asString)));
              } else {
                static_assert(
                    sizeof(T) == 0,
                    "Unsupported type in getCurrentExpressionAsSpanAsTypeWithIndices<T, S, U>()");
              }
            }
          } else {
            for(size_t i = 0; i < currSize; i++) {
              const auto& index = indices[i + spanI];
              size_t childOffset = index >> valsPerArgShift;
              int64_t inArgI = (index & valsPerArgMask);

              auto& arg = arguments[startOffset + childOffset];
              uint64_t tmp = static_cast<uint64_t>(arg.asLong);

              if constexpr(std::is_same_v<T, bool>) {
                data[i] = static_cast<S>(
                    static_cast<bool>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
              } else if constexpr(std::is_same_v<T, int8_t>) {
                data[i] = static_cast<S>(
                    static_cast<int8_t>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
              } else if constexpr(std::is_same_v<T, int16_t>) {
                data[i] = static_cast<S>(
                    static_cast<int16_t>(extractField<uint16_t>(tmp, shiftAmt * inArgI)));
              } else if constexpr(std::is_same_v<T, int32_t>) {
                data[i] = static_cast<S>(
                    static_cast<int32_t>(extractField<uint32_t>(tmp, shiftAmt * inArgI)));
              } else if constexpr(std::is_same_v<T, float_t>) {
                uint32_t val = extractField<uint32_t>(tmp, shiftAmt * inArgI);
                union {
                  uint32_t iVal;
                  float f;
                } u;
                u.iVal = val;
                data[i] = static_cast<S>(u.f);
              } else if constexpr(std::is_same_v<T, int64_t>) {
                data[i] = static_cast<S>(arg.asLong);
              } else if constexpr(std::is_same_v<T, double_t>) {
                data[i] = static_cast<S>(arg.asDouble);
              } else if constexpr(std::is_same_v<T, std::string>) {
                data[i] = static_cast<S>(std::string(viewString(buffer.root, arg.asString)));
              } else {
                static_assert(
                    sizeof(T) == 0,
                    "Unsupported type in getCurrentExpressionAsSpanAsTypeWithIndices<T, S, U>()");
              }
            }
          }
          res.push_back(std::move(boss::expressions::Span<S>(std::move(data))));
        }
      }
      return res;
    }

    template <typename T, typename U>
    boss::expressions::ExpressionSpanArguments
    getCurrentExpressionAsSpanWithIndices(const std::vector<U>& indices, int64_t spanSize,
                                          int64_t numSpansOut, int64_t numThreads = 1,
                                          bool startFromExpression = true) const {
      return getCurrentExpressionAsSpanAsTypeWithIndices<T, T, U>(indices, spanSize, numSpansOut,
                                                                  numThreads, startFromExpression);
    }

    template <typename T>
    boss::expressions::ExpressionSpanArguments getCurrentExpressionAsSpanWithIndices(
        ArgumentType type, const std::vector<T>& indices, int64_t spanSize, int64_t numSpansOut,
        int64_t numThreads = 1, bool startFromExpression = true) const {
      switch(type) {
      case ArgumentType::ARGUMENT_TYPE_BOOL:
        return getCurrentExpressionAsSpanWithIndices<bool, T>(indices, spanSize, numSpansOut,
                                                              numThreads, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_CHAR:
        return getCurrentExpressionAsSpanWithIndices<int8_t, T>(indices, spanSize, numSpansOut,
                                                                numThreads, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_SHORT:
        return getCurrentExpressionAsSpanWithIndices<int16_t, T>(indices, spanSize, numSpansOut,
                                                                 numThreads, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_INT:
        return getCurrentExpressionAsSpanWithIndices<int32_t, T>(indices, spanSize, numSpansOut,
                                                                 numThreads, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_LONG:
        return getCurrentExpressionAsSpanWithIndices<int64_t, T>(indices, spanSize, numSpansOut,
                                                                 numThreads, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_FLOAT:
        return getCurrentExpressionAsSpanWithIndices<float_t, T>(indices, spanSize, numSpansOut,
                                                                 numThreads, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_DOUBLE:
        return getCurrentExpressionAsSpanWithIndices<double_t, T>(indices, spanSize, numSpansOut,
                                                                  numThreads, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_STRING:
        return getCurrentExpressionAsSpanWithIndices<std::string, T>(
            indices, spanSize, numSpansOut, numThreads, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_SYMBOL:
        return getCurrentExpressionAsSpanWithIndices<boss::Symbol, T>(
            indices, spanSize, numSpansOut, numThreads, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
        break;
      }
      throw std::runtime_error("Invalid type in getCurrentExpressionAsSpanWithIndices");
    }

    template <typename T, typename S, typename U>
    boss::Span<S>
    getCurrentExpressionAsSpanAsTypeWithIndices(const std::vector<U>& indices,
                                                bool startFromExpression = true) const {
      static_assert(std::is_convertible<T, S>::value,
                    "Cannot convert stored type to requested span type in "
                    "getCurrentExpressionAsSpanAsTypeWithIndices. Change from <T, S, U> as T "
                    "cannot be converted to S.");
      auto const& arguments = buffer.flattenedArguments();
      size_t startOffset = argumentIndex;
      if(startFromExpression) {
        auto const& expr = expression();
        startOffset = expr.startChildOffset;
      }

      const size_t n = indices.size();
      constexpr size_t valsPerArg = sizeof(T) > sizeof(Argument) ? 1 : sizeof(Argument) / sizeof(T);
      constexpr size_t shiftAmt = sizeof(T) > sizeof(Argument) ? sizeof(Argument) * sizeof(Argument)
                                                               : sizeof(Argument) * sizeof(T);

      constexpr size_t valsPerArgMask = valsPerArg - 1;
      constexpr size_t valsPerArgShift = [] {
        size_t s = 0;
        size_t v = valsPerArg;
        while((v >>= 1) > 0)
          ++s;
        return s;
      }();

      if constexpr(std::is_same_v<T, boss::Symbol>) {
        std::vector<S> data;
        data.reserve(n);
        for(size_t i = 0; i < n; i++) {
          const auto& index = indices[i];
          size_t childOffset = index >> valsPerArgShift;
          int64_t inArgI = (index & valsPerArgMask);

          auto& arg = arguments[startOffset + childOffset];
          uint64_t tmp = static_cast<uint64_t>(arg.asLong);
          data.emplace_back(static_cast<S>(viewString(buffer.root, arg.asString)));
        }
        return boss::expressions::Span<S>(std::move(data));
      } else {
        std::vector<S> data(n);

        for(size_t i = 0; i < n; i++) {
          const auto& index = indices[i];
          size_t childOffset = index >> valsPerArgShift;
          int64_t inArgI = (index & valsPerArgMask);

          auto& arg = arguments[startOffset + childOffset];
          uint64_t tmp = static_cast<uint64_t>(arg.asLong);

          if constexpr(std::is_same_v<T, bool>) {
            data[i] =
                static_cast<S>(static_cast<bool>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
          } else if constexpr(std::is_same_v<T, int8_t>) {
            data[i] =
                static_cast<S>(static_cast<int8_t>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
          } else if constexpr(std::is_same_v<T, int16_t>) {
            data[i] = static_cast<S>(
                static_cast<int16_t>(extractField<uint16_t>(tmp, shiftAmt * inArgI)));
          } else if constexpr(std::is_same_v<T, int32_t>) {
            data[i] = static_cast<S>(
                static_cast<int32_t>(extractField<uint32_t>(tmp, shiftAmt * inArgI)));
          } else if constexpr(std::is_same_v<T, float_t>) {
            uint32_t val = extractField<uint32_t>(tmp, shiftAmt * inArgI);
            union {
              uint32_t i;
              float f;
            } u;
            u.i = val;
            data[i] = static_cast<S>(u.f);
          } else if constexpr(std::is_same_v<T, int64_t>) {
            data[i] = static_cast<S>(arg.asLong);
          } else if constexpr(std::is_same_v<T, double_t>) {
            data[i] = static_cast<S>(arg.asDouble);
          } else if constexpr(std::is_same_v<T, std::string>) {
            data[i] = static_cast<S>(std::string(viewString(buffer.root, arg.asString)));
          } else {
            static_assert(
                sizeof(T) == 0,
                "Unsupported type in getCurrentExpressionAsSpanAsTypeWithIndices<T, S, U>()");
          }
        }
        return boss::expressions::Span<S>(std::move(data));
      }
    }

    template <typename T, typename U>
    boss::Span<T> getCurrentExpressionAsSpanWithIndices(const std::vector<U>& indices,
                                                        bool startFromExpression = true) const {
      return getCurrentExpressionAsSpanAsTypeWithIndices<T, T, U>(indices, startFromExpression);
    }

    template <typename T>
    boss::expressions::ExpressionSpanArgument
    getCurrentExpressionAsSpanWithIndices(ArgumentType type, const std::vector<T>& indices,
                                          bool startFromExpression = true) const {
      switch(type) {
      case ArgumentType::ARGUMENT_TYPE_BOOL:
        return getCurrentExpressionAsSpanWithIndices<bool, T>(indices, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_CHAR:
        return getCurrentExpressionAsSpanWithIndices<int8_t, T>(indices, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_SHORT:
        return getCurrentExpressionAsSpanWithIndices<int16_t, T>(indices, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_INT:
        return getCurrentExpressionAsSpanWithIndices<int32_t, T>(indices, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_LONG:
        return getCurrentExpressionAsSpanWithIndices<int64_t, T>(indices, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_FLOAT:
        return getCurrentExpressionAsSpanWithIndices<float_t, T>(indices, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_DOUBLE:
        return getCurrentExpressionAsSpanWithIndices<double_t, T>(indices, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_STRING:
        return getCurrentExpressionAsSpanWithIndices<std::string, T>(indices, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_SYMBOL:
        return getCurrentExpressionAsSpanWithIndices<boss::Symbol, T>(indices, startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
        break;
      }
      throw std::runtime_error("Invalid type in getCurrentExpressionAsSpanWithIndices");
    }

    template <typename T>
    boss::expressions::ExpressionSpanArguments
    getCurrentExpressionAsSpanWithTypeAndSize(size_t size, int64_t spanSize, int64_t numSpansOut,
                                              bool startFromExpression = true) const {
      auto const& arguments = buffer.flattenedArguments();
      size_t startOffset = argumentIndex;
      if(startFromExpression) {
        auto const& expr = expression();
        startOffset = expr.startChildOffset;
      }
      if(spanSize > 0 && numSpansOut > 0) {
        throw std::runtime_error("Cannot call getCurrentExpressionAsSpanAsTypeWithTypeAndSize<T> "
                                 "with non-zero spanSize and numSpansOut");
      }

      if(spanSize <= 0 && numSpansOut <= 0) {
        spanSize = size;
        numSpansOut = 1;
      } else if(spanSize <= 0 && numSpansOut > 0) {
        size_t nPerSpan = (size + numSpansOut - 1) / numSpansOut;
        spanSize = nPerSpan;
      } else if(numSpansOut <= 0 && spanSize > 0) {
        size_t numSpansUnderEst = size / spanSize;
        size_t remainder = size % spanSize;
        numSpansOut = numSpansUnderEst + (remainder == 0 ? 0 : 1);
      }

      boss::expressions::ExpressionSpanArguments res;
      res.reserve(numSpansOut);
      auto base64 = &arguments[startOffset];

      if constexpr(std::is_same_v<T, bool>) {
        constexpr size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
        constexpr size_t shiftAmt = sizeof(Argument) * Argument_BOOL_SIZE;

        size_t tempI = 0;
        for(size_t spanI = 0; spanI < size; spanI += spanSize) {
          size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
          std::vector<bool> data(currSize);
          for(size_t i = 0; i < currSize && tempI < size; tempI++) {
            int64_t& arg = arguments[startOffset + tempI].asLong;
            uint64_t tmp = static_cast<uint64_t>(arg);
            for(int64_t j = 0; j < valsPerArg && i < currSize; j++, i++) {
              uint8_t val = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
              data[i] = static_cast<bool>(val);
            }
          }
          res.push_back(std::move(boss::expressions::Span<bool>(std::move(data))));
        }
      } else if constexpr(std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> ||
                          std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> ||
                          std::is_same_v<T, float_t> || std::is_same_v<T, double_t>) {
        auto base = reinterpret_cast<T*>(base64);
        for(size_t spanI = 0; spanI < size; spanI += spanSize) {
          size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
          auto shiftedBase = base + spanI;
          res.push_back(std::move(boss::expressions::Span<T>(shiftedBase, currSize, nullptr)));
        }
      } else if constexpr(std::is_same_v<T, std::string> || std::is_same_v<T, boss::Symbol>) {
        size_t tempI = 0;
        for(size_t spanI = 0; spanI < size; spanI += spanSize) {
          size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
          std::vector<T> data;
          data.reserve(currSize);
          for(size_t i = 0; i < currSize && tempI < size; i++, tempI++) {
            auto const& arg = arguments[startOffset + tempI];
            data.emplace_back(viewString(buffer.root, arg.asString));
          }
          res.push_back(std::move(boss::expressions::Span<T>(std::move(data))));
        }
      } else {
        throw std::runtime_error("Unsupported template type requested for "
                                 "getCurrentExpressionAsSpanWithTypeAndSize<T>.");
      }

      return res;
    }

    boss::expressions::ExpressionSpanArguments
    getCurrentExpressionAsSpanWithTypeAndSize(ArgumentType type, size_t size, int64_t spanSize,
                                              int64_t numSpansOut,
                                              bool startFromExpression = true) const {
      switch(type) {
      case ArgumentType::ARGUMENT_TYPE_BOOL:
        return getCurrentExpressionAsSpanWithTypeAndSize<bool>(size, spanSize, numSpansOut,
                                                               startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_CHAR:
        return getCurrentExpressionAsSpanWithTypeAndSize<int8_t>(size, spanSize, numSpansOut,
                                                                 startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_SHORT:
        return getCurrentExpressionAsSpanWithTypeAndSize<int16_t>(size, spanSize, numSpansOut,
                                                                  startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_INT:
        return getCurrentExpressionAsSpanWithTypeAndSize<int32_t>(size, spanSize, numSpansOut,
                                                                  startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_LONG:
        return getCurrentExpressionAsSpanWithTypeAndSize<int64_t>(size, spanSize, numSpansOut,
                                                                  startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_FLOAT:
        return getCurrentExpressionAsSpanWithTypeAndSize<float_t>(size, spanSize, numSpansOut,
                                                                  startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_DOUBLE:
        return getCurrentExpressionAsSpanWithTypeAndSize<double_t>(size, spanSize, numSpansOut,
                                                                   startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_STRING:
        return getCurrentExpressionAsSpanWithTypeAndSize<std::string>(size, spanSize, numSpansOut,
                                                                      startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_SYMBOL:
        return getCurrentExpressionAsSpanWithTypeAndSize<boss::Symbol>(size, spanSize, numSpansOut,
                                                                       startFromExpression);
      case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
        break;
      }
      throw std::runtime_error("Invalid type in getCurrentExpressionAsSpanWithTypeAndSize.");
    }

    boss::expressions::ExpressionSpanArgument
    getCurrentExpressionAsSpanWithTypeAndSize(ArgumentType type, size_t size,
                                              bool startFromExpression = true) const {
      auto const& arguments = buffer.flattenedArguments();
      size_t startOffset = argumentIndex;
      if(startFromExpression) {
        auto const& expr = expression();
        startOffset = expr.startChildOffset;
      }

      auto const spanFunctors =
          std::unordered_map<ArgumentType,
                             std::function<boss::expressions::ExpressionSpanArgument()>>{
              {ArgumentType::ARGUMENT_TYPE_BOOL,
               [&] {
                 std::vector<bool> data(size);
                 constexpr size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
                 constexpr size_t shiftAmt = sizeof(Argument) * Argument_BOOL_SIZE;
                 auto tempI = 0;
                 for(size_t i = 0; i < size; tempI++) {
                   int64_t& arg = arguments[startOffset + tempI].asLong;
                   uint64_t tmp = static_cast<uint64_t>(arg);
                   for(int64_t j = 0; j < valsPerArg && i < size; j++, i++) {
                     uint8_t val = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
                     data[i] = static_cast<bool>(val);
                   }
                 }
                 return boss::expressions::Span<bool>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_CHAR,
               [&] {
                 auto base64 = &arguments[startOffset];
                 auto base = reinterpret_cast<int8_t*>(base64);
                 return boss::expressions::Span<int8_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_SHORT,
               [&] {
                 auto base64 = &arguments[startOffset];
                 auto base = reinterpret_cast<int16_t*>(base64);
                 return boss::expressions::Span<int16_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_INT,
               [&] {
                 auto base64 = &arguments[startOffset];
                 auto base = reinterpret_cast<int32_t*>(base64);
                 return boss::expressions::Span<int32_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_LONG,
               [&] {
                 auto base64 = &arguments[startOffset];
                 auto base = reinterpret_cast<int64_t*>(base64);
                 return boss::expressions::Span<int64_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_FLOAT,
               [&] {
                 auto base64 = &arguments[startOffset];
                 auto base = reinterpret_cast<float_t*>(base64);
                 return boss::expressions::Span<float_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_DOUBLE,
               [&] {
                 auto base64 = &arguments[startOffset];
                 auto base = reinterpret_cast<double_t*>(base64);
                 return boss::expressions::Span<double_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_STRING,
               [&] {
                 std::vector<std::string> data(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[startOffset + i];
                   data[i] = std::string(viewString(buffer.root, arg.asString));
                 }
                 return boss::expressions::Span<std::string>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_SYMBOL, [&] {
                 std::vector<boss::Symbol> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[startOffset + i];
                   data.emplace_back(viewString(buffer.root, arg.asString));
                 }
                 return boss::expressions::Span<boss::Symbol>(std::move(data));
               }}};
      return spanFunctors.at(type)();
    }

    boss::expressions::ExpressionSpanArgument
    getCurrentExpressionAsSpanWithTypeAndSizeWithCopy(ArgumentType type, size_t size,
                                                      bool startFromExpression = true) const {
      auto const& arguments = buffer.flattenedArguments();
      size_t startOffset = argumentIndex;
      if(startFromExpression) {
        auto const& expr = expression();
        startOffset = expr.startChildOffset;
      }

      auto const spanFunctors =
          std::unordered_map<ArgumentType,
                             std::function<boss::expressions::ExpressionSpanArgument()>>{
              {ArgumentType::ARGUMENT_TYPE_BOOL,
               [&] {
                 std::vector<bool> data(size);
                 constexpr size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
                 constexpr size_t shiftAmt = sizeof(Argument) * Argument_BOOL_SIZE;
                 auto tempI = 0;
                 for(size_t i = 0; i < size; tempI++) {
                   int64_t& arg = arguments[startOffset + tempI].asLong;
                   uint64_t tmp = static_cast<uint64_t>(arg);
                   for(int64_t j = 0; j < valsPerArg && i < size; j++, i++) {
                     uint8_t val = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
                     data[i] = static_cast<bool>(val);
                   }
                 }
                 return boss::expressions::Span<bool>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_CHAR,
               [&] {
                 std::vector<int8_t> data(size);
                 constexpr size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
                 constexpr size_t shiftAmt = sizeof(Argument) * Argument_CHAR_SIZE;
                 auto tempI = 0;
                 for(size_t i = 0; i < size; tempI++) {
                   int64_t& arg = arguments[startOffset + tempI].asLong;
                   uint64_t tmp = static_cast<uint64_t>(arg);
                   for(int64_t j = 0; j < valsPerArg && i < size; j++, i++) {
                     uint8_t val = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
                     data[i] = static_cast<int8_t>(val);
                   }
                 }
                 return boss::expressions::Span<int8_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_SHORT,
               [&] {
                 std::vector<int16_t> data(size);
                 constexpr size_t valsPerArg = sizeof(Argument) / Argument_SHORT_SIZE;
                 constexpr size_t shiftAmt = sizeof(Argument) * Argument_SHORT_SIZE;
                 auto tempI = 0;
                 for(size_t i = 0; i < size; tempI++) {
                   int64_t& arg = arguments[startOffset + tempI].asLong;
                   uint64_t tmp = static_cast<uint64_t>(arg);
                   for(int64_t j = 0; j < valsPerArg && i < size; j++, i++) {
                     uint16_t val = static_cast<uint16_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
                     data[i] = static_cast<int16_t>(val);
                   }
                 }
                 return boss::expressions::Span<int16_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_INT,
               [&] {
                 std::vector<int32_t> data(size);
                 constexpr size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
                 constexpr size_t shiftAmt = sizeof(Argument) * Argument_INT_SIZE;
                 auto tempI = 0;
                 for(size_t i = 0; i < size; tempI++) {
                   int64_t& arg = arguments[startOffset + tempI].asLong;
                   uint64_t tmp = static_cast<uint64_t>(arg);
                   for(int64_t j = 0; j < valsPerArg && i < size; j++, i++) {
                     uint32_t val = static_cast<uint32_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
                     data[i] = static_cast<int32_t>(val);
                   }
                 }
                 return boss::expressions::Span<int32_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_LONG,
               [&] {
                 std::vector<int64_t> data(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[startOffset + i];
                   data[i] = arg.asLong;
                 }
                 return boss::expressions::Span<int64_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_FLOAT,
               [&] {
                 std::vector<float_t> data(size);
                 constexpr size_t valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE;
                 constexpr size_t shiftAmt = sizeof(Argument) * Argument_FLOAT_SIZE;
                 auto tempI = 0;
                 for(size_t i = 0; i < size; tempI++) {
                   int64_t& arg = arguments[startOffset + tempI].asLong;
                   uint64_t tmp = static_cast<uint64_t>(arg);
                   for(int64_t j = 0; j < valsPerArg && i < size; j++, i++) {
                     uint32_t val = static_cast<uint32_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
                     union {
                       uint32_t i;
                       float f;
                     } u;
                     u.i = val;
                     data[i] = u.f;
                   }
                 }
                 return boss::expressions::Span<float_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_DOUBLE,
               [&] {
                 std::vector<double_t> data(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[startOffset + i];
                   data[i] = arg.asDouble;
                 }
                 return boss::expressions::Span<double_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_STRING,
               [&] {
                 std::vector<std::string> data(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[startOffset + i];
                   data[i] = std::string(viewString(buffer.root, arg.asString));
                 }
                 return boss::expressions::Span<std::string>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_SYMBOL, [&] {
                 std::vector<boss::Symbol> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[startOffset + i];
                   data.emplace_back(viewString(buffer.root, arg.asString));
                 }
                 return boss::expressions::Span<boss::Symbol>(std::move(data));
               }}};
      return spanFunctors.at(type)();
    }

    boss::expressions::ExpressionSpanArgument
    getCurrentExpressionAsSpan(bool startFromExpression = true) const {
      size_t size = currentIsRLE();
      assert(size != 0);
      auto const& type = getCurrentExpressionType();
      return std::move(getCurrentExpressionAsSpanWithTypeAndSize(type, size, startFromExpression));
    }

    boss::expressions::ExpressionSpanArgument
    getCurrentExpressionAsSpanWithCopy(bool startFromExpression = true) const {
      size_t size = currentIsRLE();
      assert(size != 0);
      auto const& type = getCurrentExpressionType();
      return std::move(
          getCurrentExpressionAsSpanWithTypeAndSizeWithCopy(type, size, startFromExpression));
    }

    template <typename T> inline T getCurrentExpressionInSpanAtAs(size_t spanArgI) const {
      auto& argument = buffer.flattenedArguments()[argumentIndex];
      uint64_t tmp = static_cast<uint64_t>(argument.asLong);

      if constexpr(std::is_same_v<T, bool>) {
        constexpr size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
        constexpr size_t shiftAmt = sizeof(Argument) * Argument_BOOL_SIZE;
        int64_t inArgI = (spanArgI % valsPerArg);
        return static_cast<bool>(extractField<uint8_t>(tmp, shiftAmt * inArgI));
      } else if constexpr(std::is_same_v<T, int8_t>) {
        constexpr size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
        constexpr size_t shiftAmt = sizeof(Argument) * Argument_CHAR_SIZE;
        int64_t inArgI = (spanArgI % valsPerArg);
        return static_cast<int8_t>(extractField<uint8_t>(tmp, shiftAmt * inArgI));
      } else if constexpr(std::is_same_v<T, int16_t>) {
        constexpr size_t valsPerArg = sizeof(Argument) / Argument_SHORT_SIZE;
        constexpr size_t shiftAmt = sizeof(Argument) * Argument_SHORT_SIZE;
        int64_t inArgI = (spanArgI % valsPerArg);
        return static_cast<int16_t>(extractField<uint16_t>(tmp, shiftAmt * inArgI));
      } else if constexpr(std::is_same_v<T, int32_t>) {
        constexpr size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
        constexpr size_t shiftAmt = sizeof(Argument) * Argument_INT_SIZE;
        int64_t inArgI = (spanArgI % valsPerArg);
        return static_cast<int32_t>(extractField<uint32_t>(tmp, shiftAmt * inArgI));
      } else if constexpr(std::is_same_v<T, int64_t>) {
        return argument.asLong;
      } else if constexpr(std::is_same_v<T, float_t>) {
        constexpr size_t valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE;
        constexpr size_t shiftAmt = sizeof(Argument) * Argument_FLOAT_SIZE;
        int64_t inArgI = (spanArgI % valsPerArg);
        uint32_t val = extractField<uint32_t>(tmp, shiftAmt * inArgI);
        union {
          int32_t i;
          float f;
        } u;
        u.i = val;
        return u.f;
      } else if constexpr(std::is_same_v<T, double_t>) {
        return argument.asDouble;
      } else if constexpr(std::is_same_v<T, std::string>) {
        return viewString(buffer.root, argument.asString);
      } else if constexpr(std::is_same_v<T, boss::Symbol>) {
        return boss::Symbol(viewString(buffer.root, argument.asString));
      } else {
        static_assert(sizeof(T) == 0,
                      "Unsupported type passes to getCurrentExpressionInSpanAtAs<T>()");
      }
    }

    inline boss::Expression getCurrentExpressionInSpanAtAs(size_t spanArgI,
                                                           ArgumentType argumentType) const {
      switch(argumentType) {
      case ArgumentType::ARGUMENT_TYPE_BOOL:
        return getCurrentExpressionInSpanAtAs<bool>(spanArgI);
      case ArgumentType::ARGUMENT_TYPE_CHAR:
        return getCurrentExpressionInSpanAtAs<int8_t>(spanArgI);
      case ArgumentType::ARGUMENT_TYPE_SHORT:
        return getCurrentExpressionInSpanAtAs<int16_t>(spanArgI);
      case ArgumentType::ARGUMENT_TYPE_INT:
        return getCurrentExpressionInSpanAtAs<int32_t>(spanArgI);
      case ArgumentType::ARGUMENT_TYPE_LONG:
        return getCurrentExpressionInSpanAtAs<int64_t>(spanArgI);
      case ArgumentType::ARGUMENT_TYPE_FLOAT:
        return getCurrentExpressionInSpanAtAs<float_t>(spanArgI);
      case ArgumentType::ARGUMENT_TYPE_DOUBLE:
        return getCurrentExpressionInSpanAtAs<double_t>(spanArgI);
      case ArgumentType::ARGUMENT_TYPE_STRING:
        return getCurrentExpressionInSpanAtAs<std::string>(spanArgI);
      case ArgumentType::ARGUMENT_TYPE_SYMBOL:
        return getCurrentExpressionInSpanAtAs<boss::Symbol>(spanArgI);
      case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
        break;
      }
      return "ErrorDeserialisingExpressionInSpan"_(
          "ArgumentIndex"_(static_cast<int64_t>(argumentIndex)),
          "TypeIndex"_(static_cast<int64_t>(typeIndex)),
          "InSpanIndex"_(static_cast<int64_t>(spanArgI)),
          "ArgumentType"_(static_cast<int64_t>(argumentType)));
    }

    boss::Expression getCurrentExpressionInSpanAt(size_t spanArgI) const {
      auto argumentType = getCurrentExpressionType();
      return getCurrentExpressionInSpanAtAs(spanArgI, argumentType);
    }

    template <typename T> T getCurrentExpressionAs() const {
      auto& argument = buffer.flattenedArguments()[argumentIndex];
      if constexpr(std::is_same_v<T, bool>) {
        return argument.asBool;
      } else if constexpr(std::is_same_v<T, int8_t>) {
        return argument.asChar;
      } else if constexpr(std::is_same_v<T, int16_t>) {
        return argument.asShort;
      } else if constexpr(std::is_same_v<T, int32_t>) {
        return argument.asInt;
      } else if constexpr(std::is_same_v<T, int64_t>) {
        return argument.asLong;
      } else if constexpr(std::is_same_v<T, float_t>) {
        return argument.asFloat;
      } else if constexpr(std::is_same_v<T, double_t>) {
        return argument.asDouble;
      } else if constexpr(std::is_same_v<T, std::string>) {
        return viewString(buffer.root, argument.asString);
      } else if constexpr(std::is_same_v<T, boss::Symbol>) {
        return boss::Symbol(viewString(buffer.root, argument.asString));
      } else if constexpr(std::is_same_v<T, boss::Expression>) {
        auto const& expr = expression();
        auto s = boss::Symbol(viewString(buffer.root, expr.symbolNameOffset));
        if(buffer.expressionCount() == 0) {
          return s;
        }
        auto [args, spanArgs] =
            buffer.deserializeArguments(expr.startChildOffset, expr.endChildOffset,
                                        expr.startChildTypeOffset, expr.endChildTypeOffset);
        auto result = boss::ComplexExpression{s, {}, std::move(args), std::move(spanArgs)};
        return result;
      } else {
        static_assert(sizeof(T) == 0, "Unsupported type passes to getCurrentExpressionAs<T>()");
      }
    }

    boss::Expression getCurrentExpressionAs(ArgumentType argumentType) const {
      switch(argumentType) {
      case ArgumentType::ARGUMENT_TYPE_BOOL:
        return getCurrentExpressionAs<bool>();
      case ArgumentType::ARGUMENT_TYPE_CHAR:
        return getCurrentExpressionAs<int8_t>();
      case ArgumentType::ARGUMENT_TYPE_SHORT:
        return getCurrentExpressionAs<int16_t>();
      case ArgumentType::ARGUMENT_TYPE_INT:
        return getCurrentExpressionAs<int32_t>();
      case ArgumentType::ARGUMENT_TYPE_LONG:
        return getCurrentExpressionAs<int64_t>();
      case ArgumentType::ARGUMENT_TYPE_FLOAT:
        return getCurrentExpressionAs<float_t>();
      case ArgumentType::ARGUMENT_TYPE_DOUBLE:
        return getCurrentExpressionAs<double_t>();
      case ArgumentType::ARGUMENT_TYPE_STRING:
        return getCurrentExpressionAs<std::string>();
      case ArgumentType::ARGUMENT_TYPE_SYMBOL:
        return getCurrentExpressionAs<boss::Symbol>();
      case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
        return getCurrentExpressionAs<boss::Expression>();
      }
      return "ErrorDeserialisingExpression"_("ArgumentIndex"_(static_cast<int64_t>(argumentIndex)),
                                             "TypeIndex"_(static_cast<int64_t>(typeIndex)),
                                             "ArgumentType"_(static_cast<int64_t>(argumentType)));
    }

    // could use * operator for this
    boss::Expression getCurrentExpression() const {
      auto argumentType = getCurrentExpressionType();
      return getCurrentExpressionAs(argumentType);
    }

    template <typename T> class Iterator {
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = T;
      using difference_type = std::ptrdiff_t;
      using pointer = T*;
      using reference = T&;

      Iterator(SerializedExpression const& buffer, size_t argumentIndex)
          : buffer(buffer), arguments(buffer.flattenedArguments()),
            argumentTypes(buffer.flattenedArgumentTypes()), argumentIndex(argumentIndex),
            validIndexEnd(argumentIndex) {
        updateValidIndexEnd();
      }
      virtual ~Iterator() = default;

      Iterator(Iterator&&) noexcept = default;
      Iterator(Iterator const&) = delete;
      Iterator& operator=(Iterator&&) noexcept = default;
      Iterator& operator=(Iterator const&) = delete;

      Iterator operator++(int) { return Iterator(buffer.root, incrementIndex(1)); }
      Iterator& operator++() {
        incrementIndex(1);
        return *this;
      }

      bool isValid() { return argumentIndex < validIndexEnd; }

      T& operator*() const {
        if constexpr(std::is_same_v<T, int32_t>) {
          return arguments[argumentIndex].asInt;
        } else if constexpr(std::is_same_v<T, int64_t>) {
          return arguments[argumentIndex].asLong;
        } else if constexpr(std::is_same_v<T, float_t>) {
          return arguments[argumentIndex].asFloat;
        } else if constexpr(std::is_same_v<T, double_t>) {
          return arguments[argumentIndex].asDouble;
        } else {
          throw std::runtime_error("non-numerical types not yet implemented");
        }
      }

      T* operator->() const { return &operator*(); }

      Iterator operator+(std::ptrdiff_t v) const { return incrementIndex(v); }
      bool operator==(const Iterator& rhs) const { return argumentIndex == rhs.argumentIndex; }
      bool operator!=(const Iterator& rhs) const { return argumentIndex != rhs.argumentIndex; }

    private:
      SerializedExpression const& buffer;
      Argument* arguments;
      ArgumentType* argumentTypes;
      size_t argumentIndex;
      size_t validIndexEnd;

      size_t incrementIndex(std::ptrdiff_t increment) {
        argumentIndex += increment;
        updateValidIndexEnd();
        return argumentIndex;
      }

      void updateValidIndexEnd() {
        if(argumentIndex >= validIndexEnd) {
          if((argumentTypes[argumentIndex] & ArgumentType_RLE_BIT) != 0U) {
            if((argumentTypes[argumentIndex] & ~ArgumentType_RLE_BIT) == expectedArgumentType()) {
              uint32_t size = (static_cast<uint32_t>(argumentTypes[argumentIndex + 4]) << 24) |
                              (static_cast<uint32_t>(argumentTypes[argumentIndex + 3]) << 16) |
                              (static_cast<uint32_t>(argumentTypes[argumentIndex + 2]) << 8) |
                              (static_cast<uint32_t>(argumentTypes[argumentIndex + 1]));
              validIndexEnd = argumentIndex + size;
            }
          } else {
            if(argumentTypes[argumentIndex] == expectedArgumentType()) {
              validIndexEnd = argumentIndex + 1;
            }
          }
        }
      }

      constexpr ArgumentType expectedArgumentType() {
        if constexpr(std::is_same_v<T, int32_t>) {
          return ArgumentType::ARGUMENT_TYPE_INT;
        } else if constexpr(std::is_same_v<T, int64_t>) {
          return ArgumentType::ARGUMENT_TYPE_LONG;
        } else if constexpr(std::is_same_v<T, float_t>) {
          return ArgumentType::ARGUMENT_TYPE_FLOAT;
        } else if constexpr(std::is_same_v<T, double_t>) {
          return ArgumentType::ARGUMENT_TYPE_DOUBLE;
        } else if constexpr(std::is_same_v<T, std::string>) {
          return ArgumentType::ARGUMENT_TYPE_STRING;
        }
      }
    };

    template <typename T> Iterator<T> begin() {
      return Iterator<T>(buffer, expression().startChildOffset);
    }
    template <typename T> Iterator<T> end() {
      return Iterator<T>(buffer, expression().endChildOffset);
    }

  private:
  };

  LazilyDeserializedExpression lazilyDeserialize() & { return {*this, 0, 0}; };

  boss::Expression deserialize() && {
    switch(flattenedArgumentTypes()[0]) {
    case ArgumentType::ARGUMENT_TYPE_BOOL:
      return flattenedArguments()[0].asBool;
    case ArgumentType::ARGUMENT_TYPE_CHAR:
      return flattenedArguments()[0].asChar;
    case ArgumentType::ARGUMENT_TYPE_SHORT:
      return flattenedArguments()[0].asShort;
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
#ifdef SERIALIZATION_DEBUG
      std::cout << "ROOT METADATA: " << std::endl;
      std::cout << "  argumentCount: " << root->argumentCount << std::endl;
      std::cout << "  argumentBytesCount: " << root->argumentBytesCount << std::endl;
      std::cout << "  expressionCount: " << root->expressionCount << std::endl;
      std::cout << "  argumentDictionaryBytesCount: " << root->argumentDictionaryBytesCount
                << std::endl;
      std::cout << "  stringArgumentsFillIndex: " << root->stringArgumentsFillIndex << std::endl;
      std::cout << "  originalAddress: " << root->originalAddress << std::endl;
      std::cout << "ROOT: " << root << std::endl;
      std::cout << "ARGS: " << flattenedArguments() << std::endl;
      std::cout << "TYPES: " << flattenedArgumentTypes() << std::endl;
      std::cout << "EXPRS: " << expressionsBuffer() << std::endl;
#endif
      auto const& expr = expressionsBuffer()[0];
      auto s = boss::Symbol(viewString(root, expr.symbolNameOffset));
      if(root->expressionCount == 0) {
        return s;
      }
      auto [args, spanArgs] =
          deserializeArguments(expr.startChildOffset, expr.endChildOffset,
                               expr.startChildTypeOffset, expr.endChildTypeOffset);
      auto result = boss::ComplexExpression{s, {}, std::move(args), std::move(spanArgs)};
      return result;
    }
  };

  RootExpression* extractRoot() && {
    auto* root = this->root;
    this->root = nullptr;
    return root;
  };

  SerializedExpression(SerializedExpression&&) noexcept = default;
  SerializedExpression(SerializedExpression const&) = delete;
  SerializedExpression& operator=(SerializedExpression&&) noexcept = default;
  SerializedExpression& operator=(SerializedExpression const&) = delete;
  ~SerializedExpression() {
    if(freeFunction)
      freeExpressionTree(root, freeFunction);
  }
};

// NOLINTEND(cppcoreguidelines-pro-type-union-access)
} // namespace boss::serialization
