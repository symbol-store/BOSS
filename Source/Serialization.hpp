#include "BOSS.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"
#include <cassert>
#include <cstdlib>
#include <inttypes.h>
#include <iostream>
#include <iterator>
#include <optional>
#include <string.h>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>

#ifndef _MSC_VER
#include <cxxabi.h>
#include <memory>
#endif

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

static const size_t& ArgumentType_RLE_MINIMUM_SIZE = PortableBOSSArgumentType_RLE_MINIMUM_SIZE;
static const size_t& ArgumentType_RLE_BIT = PortableBOSSArgumentType_RLE_BIT;
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
  uint64_t expressionCount() const { return root->expressionCount; };

  Argument* flattenedArguments() const { return getExpressionArguments(root); }
  ArgumentType* flattenedArgumentTypes() const { return getArgumentTypes(root); }
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
                   std::accumulate(
                       input.getSpanArguments().begin(), input.getSpanArguments().end(), 0,
                       [](auto runningSum, auto const& argument) {
                         return runningSum +
                                std::visit([&](auto const& argument) { return argument.size(); },
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
                                   std::accumulate(input.getDynamicArguments().begin(),
                                                   input.getDynamicArguments().end(), 0,
                                                   [](auto runningSum, auto const& argument) {
                                                     return runningSum + countExpressions(argument);
                                                   });
                          },
                          [](auto const&) -> uint64_t { return 0; }),
                      input);
  }

  //////////////////////////////// Count String Bytes ///////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  static uint64_t countStringBytesInTuple(TupleLike const& tuple,
                                          std::index_sequence<Is...> /*unused*/) {
    return (countStringBytes(std::get<Is>(tuple)) + ... + 0);
  };

  static uint64_t countStringBytes(boss::Expression const& input) {
    return std::visit(
        [](auto& input) -> size_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
            return strlen(input.getHead().getName().c_str()) + 1 +
                   countStringBytesInTuple(
                       input.getStaticArguments(),
                       std::make_index_sequence<std::tuple_size_v<
                           std::decay_t<decltype(input.getStaticArguments())>>>()) +
                   std::accumulate(input.getDynamicArguments().begin(),
                                   input.getDynamicArguments().end(), 0,
                                   [](auto runningSum, auto const& argument) {
                                     return runningSum + countStringBytes(argument);
                                   }) +
                   std::accumulate(
                       input.getSpanArguments().begin(), input.getSpanArguments().end(), 0,
                       [](auto runningSum, auto const& argument) {
                         return runningSum +
                                std::visit(
                                    [&](auto const& argument) {
                                      if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                                  boss::Span<std::string>>) {
                                        return std::accumulate(
                                            argument.begin(), argument.end(), 0,
                                            [](auto innerRunningSum, auto const& stringArgument) {
                                              return innerRunningSum +
                                                     strlen(stringArgument.c_str()) + 1;
                                            });
                                      }
                                      return 0;
                                    },
                                    std::forward<decltype(argument)>(argument));
                       });
          } else if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::Symbol>) {
            return strlen(input.getName().c_str()) + 1;
          } else if constexpr(std::is_same_v<std::decay_t<decltype(input)>, std::string>) {
            return strlen(input.c_str()) + 1;
          }
          return 0;
        },
        input);
  }

  //////////////////////////////   Flatten Arguments /////////////////////////////

  template <typename TupleLike, uint64_t... Is>
  void flattenArgumentsInTuple(TupleLike&& tuple, std::index_sequence<Is...> /*unused*/,
                               uint64_t& argumentOutputI) {
    (flattenArguments(std::get<Is>(tuple), argumentOutputI), ...);
  };

  uint64_t flattenArguments(uint64_t argumentOutputI, std::vector<boss::ComplexExpression>&& inputs,
                            uint64_t& expressionOutputI) {
    auto const nextLayerOffset =
        argumentOutputI +
        std::accumulate(inputs.begin(), inputs.end(), 0, [](auto count, auto const& expression) {
          return count +
                 std::tuple_size_v<std::decay_t<decltype(expression.getStaticArguments())>> +
                 expression.getDynamicArguments().size() +
                 std::accumulate(
                     expression.getSpanArguments().begin(), expression.getSpanArguments().end(), 0,
                     [](auto runningSum, auto const& spanArg) {
                       return runningSum +
                              std::visit([&](auto const& spanArg) { return spanArg.size(); },
                                         std::forward<decltype(spanArg)>(spanArg));
                     });
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
                            std::accumulate(
                                argument.getSpanArguments().begin(),
                                argument.getSpanArguments().end(), 0,
                                [](auto runningSum, auto const& spanArg) {
                                  return runningSum +
                                         std::visit(
                                             [&](auto const& spanArg) { return spanArg.size(); },
                                             std::forward<decltype(spanArg)>(spanArg));
                                });
                        auto const startChildOffset = nextLayerOffset + childrenCountRunningSum;
                        auto const endChildOffset =
                            nextLayerOffset + childrenCountRunningSum + childrenCount;
                        auto storedString =
                            storeString(&root, argument.getHead().getName().c_str());
                        *makeExpression(root, expressionOutputI) =
                            PortableBOSSExpression{storedString, startChildOffset, endChildOffset};
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
                        auto storedString = storeString(&root, argument.c_str());
                        *makeStringArgument(root, argumentOutputI++) = storedString;
                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                         boss::Symbol>) {
                        auto storedString = storeString(&root, argument.getName().c_str());
                        *makeSymbolArgument(root, argumentOutputI++) = storedString;
                      } else {
                        print_type_name<std::decay_t<decltype(argument)>>();
                        throw std::runtime_error("unknown type");
                      }
                    },
                    std::forward<decltype(argument)>(argument));
              });
          std::for_each(
              std::make_move_iterator(spans.begin()), std::make_move_iterator(spans.end()),
              [this, &argumentOutputI](auto&& argument) {
                std::visit(
                    [&](auto&& spanArgument) {
                      auto spanSize = spanArgument.size();
                      if(spanSize >= ArgumentType_RLE_MINIMUM_SIZE) {
                        auto const& arg0 = spanArgument[0];
                        if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, bool> ||
                                     std::is_same_v<std::decay_t<decltype(arg0)>,
                                                    std::_Bit_reference>) {
                          std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto arg) {
                            *makeBoolArgument(root, argumentOutputI++) = arg;
                          });
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int8_t>) {
                          std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                            *makeCharArgument(root, argumentOutputI++) = arg;
                          });
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int32_t>) {
                          std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                            *makeIntArgument(root, argumentOutputI++) = arg;
                          });
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t>) {
                          std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                            *makeLongArgument(root, argumentOutputI++) = arg;
                          });
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, float_t>) {
                          std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                            *makeFloatArgument(root, argumentOutputI++) = arg;
                          });
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
                                                           double_t>) {
                          std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                            *makeDoubleArgument(root, argumentOutputI++) = arg;
                          });
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
                                                           std::string>) {
                          std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                            auto storedString = storeString(&root, arg.c_str());
                            *makeStringArgument(root, argumentOutputI++) = storedString;
                          });
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
                                                           boss::Symbol>) {
                          std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
                            auto storedString = storeString(&root, arg.getName().c_str());
                            *makeSymbolArgument(root, argumentOutputI++) = storedString;
                          });
                        } else {
                          print_type_name<std::decay_t<decltype(arg0)>>();
                          throw std::runtime_error("unknown type");
                        }
                        setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI - spanSize,
                                                           spanSize);
                        //  CHECK HERE NEXT
                      }
                    },
                    std::forward<decltype(argument)>(argument));
              });
        });
    if(!children.empty()) {
      return flattenArguments(argumentOutputI, std::move(children), expressionOutputI);
    }
    return argumentOutputI;
  }

  ////////////////////////////////   Surface Area ////////////////////////////////

public:
  explicit SerializedExpression(boss::Expression&& input)
      : SerializedExpression(allocateExpressionTree(countArguments(input), countExpressions(input),
                                                    countStringBytes(input), allocateFunction)) {
    std::visit(utilities::overload(
                   [this](boss::ComplexExpression&& input) {
                     uint64_t argumentIterator = 0;
                     uint64_t expressionIterator = 0;
		     auto const childrenCount =
                            std::tuple_size_v<
                                std::decay_t<decltype(input.getStaticArguments())>> +
                            input.getDynamicArguments().size() +
                            std::accumulate(
                                input.getSpanArguments().begin(),
                                input.getSpanArguments().end(), 0,
                                [](auto runningSum, auto const& spanArg) {
                                  return runningSum +
                                         std::visit(
                                             [&](auto const& spanArg) { return spanArg.size(); },
                                             std::forward<decltype(spanArg)>(spanArg));
                                });
                     auto const startChildOffset = 1;
                     auto const endChildOffset =
                         startChildOffset + childrenCount;
                     auto storedString = storeString(&root, input.getHead().getName().c_str());
                     *makeExpression(root, expressionIterator) =
                         PortableBOSSExpression{storedString, startChildOffset, endChildOffset};
                     *makeExpressionArgument(root, argumentIterator++) = expressionIterator++;
                     auto inputs = std::vector<boss::ComplexExpression>();
                     inputs.push_back(std::move(input));
                     flattenArguments(argumentIterator, std::move(inputs), expressionIterator);
                   },
                   [this](expressions::atoms::Symbol&& input) {
                     auto storedString = storeString(&root, input.getName().c_str());
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

  explicit SerializedExpression(RootExpression* root) : root(root) {}

  static void addIndexToStream(std::ostream& stream, SerializedExpression const& expr, size_t index,
			      int64_t exprIndex, int64_t exprDepth) {
    for(auto i = 0; i < exprDepth; i++) {
      stream << "  ";
    }
    auto const& arguments = expr.flattenedArguments();
    auto const& types = expr.flattenedArgumentTypes();
    auto const& expressions = expr.expressionsBuffer();
    auto const& root = expr.root;

    auto argumentType = static_cast<ArgumentType>((types[index] & (~ArgumentType_RLE_BIT)));
    auto const& isRLE = (types[index] & ArgumentType_RLE_BIT) != 0U;
    bool outOfBounds = argumentType > ArgumentType::ARGUMENT_TYPE_EXPRESSION ||
      argumentType < ArgumentType::ARGUMENT_TYPE_BOOL;

    if(outOfBounds && index > 0) {
      auto const& prevType = types[index - 1];
      bool prevIsRLE = (prevType & ArgumentType_RLE_BIT) != 0;
      if(prevIsRLE) {
	argumentType = static_cast<ArgumentType>((prevType & (~ArgumentType_RLE_BIT)));
      }
    }

    if(exprIndex < 0) {
      stream << "ARG INDEX: " << index << " VALUE: ";
    } else {
      stream << "ARG INDEX: " << index << " SUB-EXPR INDEX: " << exprIndex << " VALUE: ";
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
	     << viewString(root, arguments[index].asString) << ")"
	     << " TYPE: STRING";
      stream << "\n";
      return;
    case ArgumentType::ARGUMENT_TYPE_SYMBOL:
      stream << "( STR_OFFSET[" << arguments[index].asString << "], "
	     << boss::Symbol(viewString(root, arguments[index].asString)) << ")"
	     << " TYPE: SYMBOL";
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
	stream << ")"
	       << " TYPE: EXPRESSION\n";
      }
      for(auto i = expression.startChildOffset; i < expression.endChildOffset; i++) {
	addIndexToStream(stream, expr, i, i - expression.startChildOffset, exprDepth + 1);
      }
      for(auto i = 0; i < exprDepth; i++) {
	stream << "  ";
      }
      stream << ")"
	     << " TYPE: EXPRESSION";
      stream << "\n";
      return;
    }
    // if (isRLE) {
    // 	stream << " SPAN";
    // }
    // stream << "\n";
  }

  friend std::ostream& operator<<(std::ostream& stream, SerializedExpression const& expr) {
    addIndexToStream(stream, expr, 0, -1, 0);
    return stream;
  }

  BOSSArgumentPair deserializeArguments(uint64_t startChildOffset, uint64_t endChildOffset) const {
    boss::expressions::ExpressionArguments arguments;
    boss::expressions::ExpressionSpanArguments spanArguments;
    for(auto childIndex = startChildOffset; childIndex < endChildOffset; childIndex++) {
      auto const& type = flattenedArgumentTypes()[childIndex];
      auto const& isRLE = (type & ArgumentType_RLE_BIT) != 0U;

      if(isRLE) {

        auto const argType = (ArgumentType)(type & (~ArgumentType_RLE_BIT));

        size_t size = flattenedArgumentTypes()[childIndex + 1];
        auto prevChildIndex = childIndex;

        auto const spanFunctors =
            std::unordered_map<ArgumentType,
                               std::function<boss::expressions::ExpressionSpanArgument()>>{
                {ArgumentType::ARGUMENT_TYPE_BOOL,
                 [&] {
                   std::vector<bool> data;
                   data.reserve(size);
                   for(; childIndex < prevChildIndex + size; childIndex++) {
                     auto const& arg = flattenedArguments()[childIndex];
                     data.push_back(arg.asBool);
                   }
                   return boss::expressions::Span<bool>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_CHAR,
                 [&] {
                   std::vector<int8_t> data;
                   data.reserve(size);
                   for(; childIndex < prevChildIndex + size; childIndex++) {
                     auto const& arg = flattenedArguments()[childIndex];
                     data.push_back(arg.asChar);
                   }
                   return boss::expressions::Span<int8_t>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_INT,
                 [&] {
                   std::vector<int32_t> data;
                   data.reserve(size);
                   for(; childIndex < prevChildIndex + size; childIndex++) {
                     auto const& arg = flattenedArguments()[childIndex];
                     data.push_back(arg.asInt);
                   }
                   return boss::expressions::Span<int32_t>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_LONG,
                 [&] {
                   std::vector<int64_t> data;
                   data.reserve(size);
                   for(; childIndex < prevChildIndex + size; childIndex++) {
                     auto const& arg = flattenedArguments()[childIndex];
                     data.push_back(arg.asLong);
                   }
                   return boss::expressions::Span<int64_t>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_FLOAT,
                 [&] {
                   std::vector<float> data;
                   data.reserve(size);
                   for(; childIndex < prevChildIndex + size; childIndex++) {
                     auto const& arg = flattenedArguments()[childIndex];
                     data.push_back(arg.asFloat);
                   }
                   return boss::expressions::Span<float>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_DOUBLE,
                 [&] {
                   std::vector<double_t> data;
                   data.reserve(size);
                   for(; childIndex < prevChildIndex + size; childIndex++) {
                     auto const& arg = flattenedArguments()[childIndex];
                     data.push_back(arg.asDouble);
                   }
                   return boss::expressions::Span<double_t>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_SYMBOL,
                 [&childIndex, &prevChildIndex, &size, this] {
                   std::vector<boss::Symbol> data;
                   data.reserve(size);
                   auto spanArgument = boss::expressions::Span<boss::Symbol>();
                   for(; childIndex < prevChildIndex + size; childIndex++) {
                     auto const& arg = flattenedArguments()[childIndex];
                     data.push_back(boss::Symbol(viewString(root, arg.asString)));
                   }
                   return boss::expressions::Span<boss::Symbol>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_STRING, [&childIndex, &prevChildIndex, &size, this] {
                   std::vector<std::string> data;
                   data.reserve(size);
                   for(; childIndex < prevChildIndex + size; childIndex++) {
                     auto const& arg = flattenedArguments()[childIndex];
                     data.push_back(std::string(viewString(root, arg.asString)));
                   }
                   return boss::expressions::Span<std::string>(std::move(data));
                 }}};

        spanArguments.push_back(spanFunctors.at(argType)());
        childIndex--;

      } else {
        auto const& arg = flattenedArguments()[childIndex];
        auto const functors = std::unordered_map<ArgumentType, std::function<boss::Expression()>>{
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
               auto const& expr = expressionsBuffer()[arg.asExpression];
               auto [args, spanArgs] =
                   deserializeArguments(expr.startChildOffset, expr.endChildOffset);
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

    size_t getArgumentIndex() const { return argumentIndex; }

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
                              auto i = 0U;
                              for(; i < e.getDynamicArguments().size(); i++) {
                                auto subExpressionPosition = startChildOffset + i;
                                result &=
                                    (LazilyDeserializedExpression(buffer, subExpressionPosition) ==
                                     e.getDynamicArguments().at(i));
                              }
                              for(auto j = 0; j < e.getSpanArguments().size(); j++) {
                                std::visit(
                                    [&](auto&& typedSpanArg) {
                                      auto subSpanPosition = startChildOffset + i;
                                      auto currSpan =
                                          (LazilyDeserializedExpression(buffer, subSpanPosition))
                                              .getCurrentExpressionAsSpan();
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
                                      i += typedSpanArg.size();
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
      lazyExpr.buffer.addIndexToStream(stream, lazyExpr.buffer, lazyExpr.argumentIndex, -1, 0);
      return stream;
    }

    LazilyDeserializedExpression operator[](size_t childOffset) const {
      auto const& expr = expression();
      assert(childOffset < expr.endChildOffset - expr.startChildOffset);
      return {buffer, expr.startChildOffset + childOffset};
    }

    LazilyDeserializedExpression operator[](std::string const& keyName) const {
      auto const& expr = expression();
      auto const& arguments = buffer.flattenedArguments();
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& expressions = buffer.expressionsBuffer();
      for(auto i = expr.startChildOffset; i < expr.endChildOffset; ++i) {
        if(argumentTypes[i] != ArgumentType::ARGUMENT_TYPE_EXPRESSION) {
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

    Expression const& expression() const {
      auto const& arguments = buffer.flattenedArguments();
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& expressions = buffer.expressionsBuffer();
      assert(argumentTypes[argumentIndex] == ArgumentType::ARGUMENT_TYPE_EXPRESSION);
      return expressions[arguments[argumentIndex].asExpression];
    }

    size_t getCurrentExpressionAsString(bool partOfRLE) const {
      auto const& type = getCurrentExpressionType();
      if(!partOfRLE) {
        assert(type == ArgumentType::ARGUMENT_TYPE_STRING ||
               type == ArgumentType::ARGUMENT_TYPE_SYMBOL);
      }
      return buffer.flattenedArguments()[argumentIndex].asString;
    }

    bool currentIsExpression() const {
      auto const& argumentType = buffer.flattenedArgumentTypes()[argumentIndex];
      return argumentType == ArgumentType::ARGUMENT_TYPE_EXPRESSION;
    }

    size_t currentIsRLE() const {
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& type = argumentTypes[argumentIndex];
      auto const& isRLE = (type & ArgumentType_RLE_BIT) != 0u;
      return isRLE ? argumentTypes[argumentIndex + 1] : 0;
    }

    boss::Symbol getCurrentExpressionHead() const {
      auto const& expr = expression();
      return boss::Symbol(viewString(buffer.root, expr.symbolNameOffset));
    }

    ArgumentType getCurrentExpressionType() const {
      auto const& type = buffer.flattenedArgumentTypes()[argumentIndex];
      return static_cast<ArgumentType>((type & (~ArgumentType_RLE_BIT)));
    }

    boss::expressions::ExpressionSpanArgument getCurrentExpressionAsSpan() const {
      size_t size = currentIsRLE();
      assert(size != 0);
      auto const& type = getCurrentExpressionType();
      auto const& arguments = buffer.flattenedArguments();
      auto const spanFunctors =
          std::unordered_map<ArgumentType,
                             std::function<boss::expressions::ExpressionSpanArgument()>>{
              {ArgumentType::ARGUMENT_TYPE_BOOL,
               [&] {
                 std::vector<bool> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data.push_back(arg.asBool);
                 }
                 return boss::expressions::Span<bool>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_CHAR,
               [&] {
                 std::vector<int8_t> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data.push_back(arg.asChar);
                 }
                 return boss::expressions::Span<int8_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_INT,
               [&] {
                 std::vector<int32_t> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data.push_back(arg.asInt);
                 }
                 return boss::expressions::Span<int32_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_LONG,
               [&] {
                 std::vector<int64_t> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data.push_back(arg.asLong);
                 }
                 return boss::expressions::Span<int64_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_FLOAT,
               [&] {
                 std::vector<float_t> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data.push_back(arg.asFloat);
                 }
                 return boss::expressions::Span<float_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_DOUBLE,
               [&] {
                 std::vector<double_t> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data.push_back(arg.asDouble);
                 }
                 return boss::expressions::Span<double_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_STRING,
               [&] {
                 std::vector<std::string> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data.push_back(std::string(viewString(buffer.root, arg.asString)));
                 }
                 return boss::expressions::Span<std::string>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_SYMBOL, [&] {
                 std::vector<boss::Symbol> data;
                 data.reserve(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data.push_back(boss::Symbol(viewString(buffer.root, arg.asString)));
                 }
                 return boss::expressions::Span<boss::Symbol>(std::move(data));
               }}};
      return spanFunctors.at(type)();
    }
    
    boss::Expression getCurrentExpressionAs(ArgumentType argumentType) const {
      auto const& argument = buffer.flattenedArguments()[argumentIndex];
      switch(argumentType) {
      case ArgumentType::ARGUMENT_TYPE_BOOL:
        return argument.asBool;
      case ArgumentType::ARGUMENT_TYPE_CHAR:
        return argument.asChar;
      case ArgumentType::ARGUMENT_TYPE_INT:
        return argument.asInt;
      case ArgumentType::ARGUMENT_TYPE_LONG:
        return argument.asLong;
      case ArgumentType::ARGUMENT_TYPE_FLOAT:
        return argument.asFloat;
      case ArgumentType::ARGUMENT_TYPE_DOUBLE:
        return argument.asDouble;
      case ArgumentType::ARGUMENT_TYPE_STRING:
        return viewString(buffer.root, argument.asString);
      case ArgumentType::ARGUMENT_TYPE_SYMBOL:
        return boss::Symbol(viewString(buffer.root, argument.asString));
      case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
        auto const& expr = expression();
        auto s = boss::Symbol(viewString(buffer.root, expr.symbolNameOffset));
        if(buffer.expressionCount() == 0) {
          return s;
        }
        auto [args, spanArgs] =
            buffer.deserializeArguments(expr.startChildOffset, expr.endChildOffset);
        auto result = boss::ComplexExpression{s, {}, std::move(args), std::move(spanArgs)};
        return result;
      }
    }

    // could use * operator for this
    // should this be && qualified?
    boss::Expression getCurrentExpression() const {
      auto const& types = buffer.flattenedArgumentTypes();
      auto argumentType =
          static_cast<ArgumentType>((types[argumentIndex] & (~ArgumentType_RLE_BIT)));
      bool outOfBounds = argumentType > ArgumentType::ARGUMENT_TYPE_EXPRESSION ||
                         argumentType < ArgumentType::ARGUMENT_TYPE_BOOL;

      if(outOfBounds && argumentIndex > 0) {
        auto const& prevType = types[argumentIndex - 1];
        bool prevIsRLE = (prevType & ArgumentType_RLE_BIT) != 0;
        if(prevIsRLE) {
          argumentType = static_cast<ArgumentType>((prevType & (~ArgumentType_RLE_BIT)));
        }
      }
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
              validIndexEnd =
                  argumentIndex + static_cast<uint32_t>(argumentTypes[argumentIndex + 1]);
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
      auto const& expr = expressionsBuffer()[0];
      auto s = boss::Symbol(viewString(root, expr.symbolNameOffset));
      if(root->expressionCount == 0) {
        return s;
      }
      auto [args, spanArgs] = deserializeArguments(1, expr.endChildOffset);
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
