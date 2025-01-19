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
#include <unordered_set>

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

static const uint8_t& ArgumentType_RLE_MINIMUM_SIZE = PortableBOSSArgumentType_RLE_MINIMUM_SIZE;
static const uint8_t& ArgumentType_RLE_BIT = PortableBOSSArgumentType_RLE_BIT;

static const uint64_t& Argument_BOOL_SIZE = PortableBOSSArgument_BOOL_SIZE;
static const uint64_t& Argument_CHAR_SIZE = PortableBOSSArgument_CHAR_SIZE;
static const uint64_t& Argument_INT_SIZE = PortableBOSSArgument_INT_SIZE;
static const uint64_t& Argument_LONG_SIZE = PortableBOSSArgument_LONG_SIZE;
static const uint64_t& Argument_FLOAT_SIZE = PortableBOSSArgument_FLOAT_SIZE;
static const uint64_t& Argument_DOUBLE_SIZE = PortableBOSSArgument_DOUBLE_SIZE;
static const uint64_t& Argument_STRING_SIZE = PortableBOSSArgument_STRING_SIZE;
static const uint64_t& Argument_EXPRESSION_SIZE = PortableBOSSArgument_EXPRESSION_SIZE;  

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

  // using DictKey = std::variant<bool, int8_t, int32_t, int64_t, float_t, double_t>;
  using DictKey = std::variant<int64_t, double_t>;
  struct VariantHash {
    template <typename T>
    std::size_t operator()(const T& value) const {
      if constexpr (std::is_same_v<T, bool>) {
	return std::hash<int>()(value);
      } else {
	return std::hash<T>()(value);
      }
    }

    std::size_t operator()(const DictKey& v) const {
      return std::visit([](auto&& arg) { return VariantHash{}(arg); }, v);
    }
  };
  struct VariantEqual {
    bool operator()(const DictKey& a, const DictKey& b) const {
      return a == b;
    }
  };

  
  using ExpressionDictionary = std::unordered_map<DictKey, int32_t, VariantHash, VariantEqual>;
  using SpanDictionary = std::unordered_map<size_t, ExpressionDictionary>;
  
  RootExpression* root = nullptr;
  uint64_t argumentCount() const { return root->argumentCount; };
  uint64_t argumentBytesCount() const { return root->argumentBytesCount; };
  uint64_t expressionCount() const { return root->expressionCount; };

  Argument* flattenedArguments() const { return getExpressionArguments(root); }
  ArgumentType* flattenedArgumentTypes() const { return getArgumentTypes(root); }
  Expression* expressionsBuffer() const { return getExpressionSubexpressions(root); }

  //////////////////////////////// Count Unique Arguments ///////////////////////////////

  static uint64_t calculateDictionaryBytes(SpanDictionary& spanDict) {
    uint64_t sum = 0;
    for (const auto& entry : spanDict) {
      const auto& innerDict = entry.second;
      sum += innerDict.size() * sizeof(Argument);
    }
    return sum;
  }
  
  static void checkMapAndIncrement(DictKey&& input, ExpressionDictionary& dict) {
    auto it = dict.find(input);
    if (it == dict.end()) {
      dict.emplace(std::move(input), -1);
    }
  }
  
  template <typename TupleLike, uint64_t... Is>
  static void countUniqueArgumentsInTuple(SpanDictionary& dict, size_t& spanI, TupleLike const& tuple,
                                        std::index_sequence<Is...> /*unused*/) {
    (countUniqueArguments(std::get<Is>(tuple), dict, spanI), ...);
  };

  static SpanDictionary countUniqueArguments(boss::Expression const& input) {
    SpanDictionary res;
    size_t spanI = 0;
    countUniqueArguments(input, res, spanI);
    return std::move(res);
  }

  static void countUniqueArguments(boss::Expression const& input, SpanDictionary& dict, size_t& spanI) {
    return std::visit(
		      [&dict, &spanI](auto& input) {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
	    countUniqueArgumentsInTuple(dict, spanI, input.getStaticArguments(),
				  std::make_index_sequence<std::tuple_size_v<
				  std::decay_t<decltype(input.getStaticArguments())>>>());
	    std::for_each(input.getDynamicArguments().begin(),
			  input.getDynamicArguments().end(),
			  [&dict, &spanI](auto const& argument) {
			    countUniqueArguments(argument, dict, spanI);
			  });
	    std::for_each(input.getSpanArguments().begin(), input.getSpanArguments().end(),
			  [&dict, &spanI](auto const& argument) {
			    std::visit([&](auto const& spanArgument) {
			      ExpressionDictionary spanDict;
			      auto spanSize = spanArgument.size();
			      auto const& arg0 = spanArgument[0];
			      if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t> ||
					   std::is_same_v<std::decay_t<decltype(arg0)>, double_t>) {
				std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto arg) {
				  checkMapAndIncrement(DictKey(arg), spanDict);
				});
			      }
			      if (spanDict.size() < (spanSize / 2)) {
				dict[spanI] = std::move(spanDict);
			      }
			      spanI++;
			    },
			      std::forward<decltype(argument)>(argument));
			  });
          }
        },
        input);
  }

  //////////////////////////////// Count Argument Bytes ///////////////////////////////

  // Current assumes that only values within spans can be packed into a single 8 byte arg value
  // All else is treated as an 8 bytes arg value
  // Note: To read values at a specific index in a packed span, the span size must be known
  template <typename TupleLike, uint64_t... Is>
  static uint64_t countArgumentBytesInTuple(TupleLike const& tuple,
                                        std::index_sequence<Is...> /*unused*/) {
    return (countArgumentBytes(std::get<Is>(tuple)) + ... + 0);
  };

  static uint64_t countArgumentBytes(boss::Expression const& input) {
    return std::visit(
        [](auto& input) -> size_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
            return Argument_EXPRESSION_SIZE +
                   countArgumentBytesInTuple(
                       input.getStaticArguments(),
                       std::make_index_sequence<std::tuple_size_v<
                           std::decay_t<decltype(input.getStaticArguments())>>>()) +
                   std::accumulate(input.getDynamicArguments().begin(),
                                   input.getDynamicArguments().end(), 0,
                                   [](auto runningSum, auto const& argument) {
                                     return runningSum + countArgumentBytes(argument);
                                   }) +
                   std::accumulate(
                       input.getSpanArguments().begin(), input.getSpanArguments().end(), 0,
                       [](auto runningSum, auto const& argument) {
                         return runningSum +
                                std::visit([&](auto const& spanArgument) {
				  auto spanBytes = 0;
				  auto spanSize = spanArgument.size();
				  auto const& arg0 = spanArgument[0];
				  if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, bool> ||
					       std::is_same_v<std::decay_t<decltype(arg0)>, std::_Bit_reference>) {
				    spanBytes = spanSize * Argument_BOOL_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int8_t>) {
				    spanBytes = spanSize * Argument_CHAR_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int32_t>) {
				    spanBytes = spanSize * Argument_INT_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t>) {
				    spanBytes = spanSize * Argument_LONG_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, float_t>) {
				    spanBytes = spanSize * Argument_FLOAT_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, double_t>) {
				    spanBytes = spanSize * Argument_DOUBLE_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, std::string>) {
				    spanBytes = spanSize * Argument_STRING_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, boss::Symbol>) {
				    spanBytes = spanSize * Argument_STRING_SIZE;
				  } else {
				    print_type_name<std::decay_t<decltype(arg0)>>();
				    throw std::runtime_error("unknown type in span");
				  }
				  // std::cout << "SPAN BYTES: " << spanBytes << std::endl;
				  // std::cout << "ROUNDED SPAN BYTES: " << ((spanBytes + sizeof(Argument) - 1) & -sizeof(Argument)) << std::endl;
				  return (spanBytes + sizeof(Argument) - 1) & -sizeof(Argument);
				},
				  std::forward<decltype(argument)>(argument));
                       });
          }
          return sizeof(Argument);
        },
        input);
  }


  //////////////////////////////// Count Argument Bytes with Dictionary ///////////////////////////////

  // Current assumes that only values within spans can be packed into a single 8 byte arg value
  // All else is treated as an 8 bytes arg value
  // Note: To read values at a specific index in a packed span, the span size must be known
  template <typename TupleLike, uint64_t... Is>
  static uint64_t countArgumentBytesInTupleDict(SpanDictionary& dict, size_t& spanI, TupleLike const& tuple,
                                        std::index_sequence<Is...> /*unused*/) {
    return (countArgumentBytesDict(std::get<Is>(tuple), dict, spanI) + ... + 0);
  };

  static uint64_t countArgumentBytesDict(boss::Expression const& input, SpanDictionary& dict) {
    size_t spanI = 0;
    return countArgumentBytesDict(input, dict, spanI);
  };

  static uint64_t countArgumentBytesDict(boss::Expression const& input, SpanDictionary& dict, size_t& spanI) {
    return std::visit(
	[&dict, &spanI](auto& input) -> size_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
            return Argument_EXPRESSION_SIZE +
	      countArgumentBytesInTupleDict(dict, spanI,
                       input.getStaticArguments(),
                       std::make_index_sequence<std::tuple_size_v<
                           std::decay_t<decltype(input.getStaticArguments())>>>()) +
                   std::accumulate(input.getDynamicArguments().begin(),
                                   input.getDynamicArguments().end(), 0,
                                   [&dict, &spanI](auto runningSum, auto const& argument) {
                                     return runningSum + countArgumentBytesDict(argument, dict, spanI);
                                   }) +
                   std::accumulate(
                       input.getSpanArguments().begin(), input.getSpanArguments().end(), 0,
                       [&dict, &spanI](auto runningSum, auto const& argument) {
                         return runningSum +
                                std::visit([&](auto const& spanArgument) {
				  auto spanBytes = 0;
				  auto spanSize = spanArgument.size();
				  auto const& arg0 = spanArgument[0];
				  if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, bool> ||
					       std::is_same_v<std::decay_t<decltype(arg0)>, std::_Bit_reference>) {
				    spanBytes = spanSize * Argument_BOOL_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int8_t>) {
				    spanBytes = spanSize * Argument_CHAR_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int32_t>) {
				    spanBytes = spanSize * Argument_INT_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t>) {
				    if (dict.find(spanI) == dict.end()) {
				      spanBytes = spanSize * Argument_LONG_SIZE;
				    }
				    spanBytes = spanSize * Argument_INT_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, float_t>) {
				    spanBytes = spanSize * Argument_FLOAT_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, double_t>) {
				    if (dict.find(spanI) == dict.end()) {
				      spanBytes = spanSize * Argument_DOUBLE_SIZE;
				    }
				    spanBytes = spanSize * Argument_INT_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, std::string>) {
				    spanBytes = spanSize * Argument_STRING_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, boss::Symbol>) {
				    spanBytes = spanSize * Argument_STRING_SIZE;
				  } else {
				    print_type_name<std::decay_t<decltype(arg0)>>();
				    throw std::runtime_error("unknown type in span");
				  }
				  // std::cout << "SPAN BYTES: " << spanBytes << std::endl;
				  // std::cout << "ROUNDED SPAN BYTES: " << ((spanBytes + sizeof(Argument) - 1) & -sizeof(Argument)) << std::endl;
				  spanI++;
				  return (spanBytes + sizeof(Argument) - 1) & -sizeof(Argument);
				},
				  std::forward<decltype(argument)>(argument));
                       });
          }
          return sizeof(Argument);
        },
        input);
  }

  //////////////////////////////// Count RLE Arguments ///////////////////////////////

  // Current assumes that only values within spans can be RLEd
  // Note: To read values at a specific index in RLEd span, the span size must be known (logical & physical)
  template <typename TupleLike, uint64_t... Is>
  static uint64_t countRLEArgumentsInTuple(TupleLike const& tuple,
                                        std::index_sequence<Is...> /*unused*/) {
    return (countRLEArgument(std::get<Is>(tuple)) + ... + 0);
  };

  static uint64_t countRLEArguments(boss::Expression const& input) {
    return std::visit(
        [](auto& input) -> size_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
            return 1 +
                   countRLEArgumentsInTuple(
                       input.getStaticArguments(),
                       std::make_index_sequence<std::tuple_size_v<
                           std::decay_t<decltype(input.getStaticArguments())>>>()) +
                   std::accumulate(input.getDynamicArguments().begin(),
                                   input.getDynamicArguments().end(), 0,
                                   [](auto runningSum, auto const& argument) {
                                     return runningSum + countRLEArguments(argument);
                                   }) +
                   std::accumulate(
                       input.getSpanArguments().begin(), input.getSpanArguments().end(), 0,
                       [](auto runningSum, auto const& argument) {
                         return runningSum +
                                std::visit([&](auto const& spanArgument) {
				  auto spanSize = spanArgument.size();
				  auto const& arg0 = spanArgument[0];
				  auto spanSum = 0;
				  auto runCount = 1;
				  for(size_t i = 1; i < spanSize; i++) {
				    if (spanArgument[i] == spanArgument[i-1]) {
				      runCount++;
				    } else {
				      if (runCount > 2) {
					spanSum += 2; // 1 for value, 1 for startIdx of run
				      } else {
					spanSum += runCount; // do not RLE runs lteq 2
				      }
				      runCount = 1;
				    }
				  }
				  return spanSum;
				},
				  std::forward<decltype(argument)>(argument));
                       });
          }
          return 1;
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
  static uint64_t countStringBytesInTuple(std::unordered_set<std::string>& stringSet, bool dictEncodeStrings,
					  TupleLike const& tuple,
                                          std::index_sequence<Is...> /*unused*/) {
    return (countStringBytes(std::get<Is>(tuple), stringSet, dictEncodeStrings) + ... + 0);
  };

  static uint64_t countStringBytes(boss::Expression const& input, bool dictEncodeStrings = true) {
    std::unordered_set<std::string> stringSet;
    return countStringBytes(input, stringSet, dictEncodeStrings);
  }
  
  static uint64_t countStringBytes(boss::Expression const& input, std::unordered_set<std::string>& stringSet, bool dictEncodeStrings) {
    return std::visit(
        [&](auto& input) -> size_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
	    size_t headBytes = !dictEncodeStrings * (strlen(input.getHead().getName().c_str()) + 1);
	    if (dictEncodeStrings && stringSet.find(input.getHead().getName()) == stringSet.end()) {
	      stringSet.insert(input.getHead().getName());
	      headBytes = strlen(input.getHead().getName().c_str()) + 1;
	    }
	    size_t staticArgsBytes =
	      countStringBytesInTuple(stringSet, dictEncodeStrings,
				      input.getStaticArguments(),
				      std::make_index_sequence<std::tuple_size_v<
				      std::decay_t<decltype(input.getStaticArguments())>>>());
	    size_t dynamicArgsBytes =
	      std::accumulate(input.getDynamicArguments().begin(),
			      input.getDynamicArguments().end(), 0,
			      [&](size_t runningSum, auto const& argument) {
				return runningSum + countStringBytes(argument, stringSet, dictEncodeStrings);
			      });
	    size_t spanArgsBytes =
	      std::accumulate(
                       input.getSpanArguments().begin(), input.getSpanArguments().end(), 0,
                       [&](size_t runningSum, auto const& argument) {
                         return runningSum +
                                std::visit(
                                    [&](auto const& argument) {
                                      if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                                  boss::Span<std::string>>) {
                                        return std::accumulate(
                                            argument.begin(), argument.end(), 0,
                                            [&](size_t innerRunningSum, auto const& stringArgument) {
					      size_t resRunningSum = innerRunningSum +
						(!dictEncodeStrings * (strlen(stringArgument.c_str()) + 1));
					      if (dictEncodeStrings && stringSet.find(stringArgument) == stringSet.end()) {
						stringSet.insert(stringArgument);	
						resRunningSum += strlen(stringArgument.c_str()) + 1; 
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
	    if (dictEncodeStrings && stringSet.find(input.getName()) == stringSet.end()) {
	      stringSet.insert(input.getName());
	      res = strlen(input.getName().c_str()) + 1;
	    }
            return res;
          } else if constexpr(std::is_same_v<std::decay_t<decltype(input)>, std::string>) {
	    size_t res = !dictEncodeStrings * (strlen(input.c_str()) + 1);
	    if (dictEncodeStrings && stringSet.find(input) == stringSet.end()) {
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

  size_t checkMapAndStoreString(const std::string& key, std::unordered_map<std::string, size_t>& stringMap, bool dictEncodeStrings) {
    size_t storedString = 0;
    if (dictEncodeStrings) {
      auto it = stringMap.find(key);
	if (it == stringMap.end()) {
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
		      expression.getSpanArguments().begin(), expression.getSpanArguments().end(), 0,
		      [](auto runningSum, auto const& spanArg) {
			return runningSum +
			  std::visit([&](auto const& spanArg) { return spanArg.size(); },
				     std::forward<decltype(spanArg)>(spanArg));
		      });
  }

  uint64_t countArgumentsPacked(boss::ComplexExpression const& expression, SpanDictionary& spanDict) {
    size_t spanI = 0;
    return countArgumentsPacked(expression, spanDict, spanI);
  }

  uint64_t countArgumentsPacked(boss::ComplexExpression const& expression, SpanDictionary& spanDict, size_t spanIInput) {
    size_t spanI = spanIInput;
    return std::tuple_size_v<std::decay_t<decltype(expression.getStaticArguments())>> +
      expression.getDynamicArguments().size() +
      std::accumulate(
		      expression.getSpanArguments().begin(), expression.getSpanArguments().end(), 0,
		      [&spanDict, &spanI](auto runningSum, auto const& spanArg) {
			return runningSum +
			  std::visit([&](auto const& spanArgument) {
			    auto spanSize = spanArgument.size();
			    auto const& arg0 = spanArgument[0];
			    auto valsPerArg = sizeof(Argument) / sizeof(arg0);
			    if (spanDict.find(spanI++) != spanDict.end()) {
			      valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
			    }
			    return (spanSize + valsPerArg - 1) / valsPerArg;
			  },
			    std::forward<decltype(spanArg)>(spanArg));
		      });
  }
  
  template <typename TupleLike, uint64_t... Is>
  void flattenArgumentsInTuple(TupleLike&& tuple, std::index_sequence<Is...> /*unused*/,
                               uint64_t& argumentOutputI, uint64_t& typeOutputI, uint64_t& dictOutputI, SpanDictionary& spanDict,
			       size_t& spanI, std::unordered_map<std::string, size_t>& stringMap,
			       bool dictEncodeStrings, bool rleSpans) {
    (flattenArguments(std::get<Is>(tuple), argumentOutputI, typeOutputI, dictOutputI, spanDict, spanI, stringMap, dictEncodeStrings, rleSpans), ...);
  };

  // assuming RLE encode for now
  uint64_t flattenArguments(uint64_t argumentOutputI, uint64_t typeOutputI,
			    std::vector<boss::ComplexExpression>&& inputs,
			    uint64_t& expressionOutputI, uint64_t dictOutputI, SpanDictionary& spanDict,
			    bool dictEncodeStrings = true, bool rleSpans = true) {
    std::unordered_map<std::string, size_t> stringMap;
    size_t spanI = 0;
    return flattenArguments(argumentOutputI, typeOutputI, std::move(inputs), expressionOutputI, dictOutputI, spanDict, spanI, stringMap, dictEncodeStrings, rleSpans);
  }

  uint64_t flattenArguments(uint64_t argumentOutputI, uint64_t typeOutputI,
			    std::vector<boss::ComplexExpression>&& inputs,
                            uint64_t& expressionOutputI, uint64_t dictOutputI, SpanDictionary& spanDict, size_t& spanI,
			    std::unordered_map<std::string, size_t>& stringMap,
			    bool dictEncodeStrings, bool rleSpans) {
    auto const nextLayerTypeOffset =
      typeOutputI +
      std::accumulate(inputs.begin(), inputs.end(), 0, [this](auto count, auto const& expression) {
	return count + countArgumentTypes(expression);
      });
    auto const nextLayerOffset =
        argumentOutputI +
      std::accumulate(inputs.begin(), inputs.end(), 0, [this, &spanDict, spanI](auto count, auto const& expression) {
	return count + countArgumentsPacked(expression, spanDict, spanI);
        });
    auto children = std::vector<boss::ComplexExpression>();
    auto childrenCountRunningSum = 0UL;
    auto childrenTypeCountRunningSum = 0UL;

    std::for_each(
        std::move_iterator(inputs.begin()), std::move_iterator(inputs.end()),
        [this, &argumentOutputI, &typeOutputI, &children, &expressionOutputI, nextLayerTypeOffset, nextLayerOffset,
         &childrenCountRunningSum, &childrenTypeCountRunningSum, &dictOutputI, &spanDict, &spanI, &stringMap,
	 &dictEncodeStrings, &rleSpans](boss::ComplexExpression&& input) {
          auto [head, statics, dynamics, spans] = std::move(input).decompose();
          flattenArgumentsInTuple(
              statics,
              std::make_index_sequence<std::tuple_size_v<std::decay_t<decltype(statics)>>>(),
              argumentOutputI, typeOutputI, dictOutputI, spanDict, spanI, stringMap, dictEncodeStrings, rleSpans);
          std::for_each(
              std::make_move_iterator(dynamics.begin()), std::make_move_iterator(dynamics.end()),
              [this, &argumentOutputI, &typeOutputI, &children, &expressionOutputI, nextLayerTypeOffset, nextLayerOffset,
               &childrenCountRunningSum, &childrenTypeCountRunningSum, &stringMap, &dictEncodeStrings, &rleSpans, &spanDict, &spanI](auto&& argument) {
                std::visit(
			   [this, &children, &argumentOutputI, &typeOutputI, &expressionOutputI, nextLayerTypeOffset,
			    nextLayerOffset, &childrenCountRunningSum, &childrenTypeCountRunningSum,
			    &stringMap, &dictEncodeStrings, &spanDict, spanI](auto&& argument) {
                      if constexpr(boss::expressions::generic::isComplexExpression<
                                       decltype(argument)>) {
			auto const childrenCount = countArgumentsPacked(argument, spanDict, spanI);
                        auto const childrenTypeCount = countArgumentTypes(argument);
                        auto const startChildArgOffset = nextLayerOffset + childrenCountRunningSum;
                        auto const endChildArgOffset =
                            nextLayerOffset + childrenCountRunningSum + childrenCount;
                        auto const startChildTypeOffset = nextLayerTypeOffset + childrenTypeCountRunningSum;
                        auto const endChildTypeOffset =
                            nextLayerTypeOffset + childrenTypeCountRunningSum + childrenTypeCount;
		        auto storedString =
			  checkMapAndStoreString(argument.getHead().getName(), stringMap, dictEncodeStrings);
                        *makeExpression(root, expressionOutputI) =
			  PortableBOSSExpression{storedString,
						 startChildArgOffset, endChildArgOffset,
						 startChildTypeOffset, endChildTypeOffset};
                        *makeExpressionArgument(root, argumentOutputI++, typeOutputI++) = expressionOutputI++;
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
                        auto storedString =
			  checkMapAndStoreString(argument.getName(), stringMap, dictEncodeStrings);
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
              [this, &argumentOutputI, &typeOutputI, &dictOutputI, &spanDict, &spanI, &stringMap, &dictEncodeStrings, &rleSpans](auto&& argument) {
                std::visit(
                    [&](auto&& spanArgument) {
                      auto spanSize = spanArgument.size();
                      if(spanSize >= ArgumentType_RLE_MINIMUM_SIZE) {
                        auto const& arg0 = spanArgument[0];
			size_t argumentStartIndex = argumentOutputI;
			size_t runStartIndex = 0;
			size_t runCount = 1;
			// RLE NEEDS UPDATE WITH PACKED VALUES
                        if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, bool> ||
                                     std::is_same_v<std::decay_t<decltype(arg0)>,
                                                    std::_Bit_reference>) {
			  if (rleSpans) {
			    for(size_t i = 1; i < spanSize; i++) {
			      if (spanArgument[i] == spanArgument[i-1]) {
				makeBoolArgumentType(root, argumentOutputI++);
				runCount++;
			      } else {
				if (runCount > 2) {
				  *makeBoolArgumentRLE(root, argumentStartIndex, argumentStartIndex) =
				    spanArgument[runStartIndex];
				} else {
				  if (runCount > 1) {
				    *makeBoolArgument(root, argumentStartIndex+1) = spanArgument[runStartIndex+1];
				  }
				  *makeBoolArgument(root, argumentStartIndex) = spanArgument[runStartIndex];
				}
				argumentStartIndex = argumentOutputI++;
				runStartIndex = i;
			      }
			    }
			  } else {
			    size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
			    for (size_t i = 0; i < spanSize; i += valsPerArg) {
			      uint64_t tmp = 0;
			      for (size_t j = 0; j < valsPerArg; j++) {
				makeBoolArgumentType(root, typeOutputI++);
				tmp |= static_cast<uint64_t>(spanArgument[i+j]) << (Argument_BOOL_SIZE * sizeof(Argument) * (valsPerArg - 1 - j));
			      }
			      *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			    }
			    // std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto arg) {
			    //   *makeBoolArgument(root, argumentOutputI++) = arg;
			    // });
			  }
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int8_t>) {
			  if (rleSpans) {
			    for(size_t i = 1; i < spanSize; i++) {
			      if (spanArgument[i] == spanArgument[i-1]) {
				makeCharArgumentType(root, argumentOutputI++);
				runCount++;
			      } else {
				if (runCount > 2) {
				  *makeCharArgumentRLE(root, argumentStartIndex, argumentStartIndex) =
				    spanArgument[runStartIndex];
				} else {
				  if (runCount > 1) {
				    *makeCharArgument(root, argumentStartIndex+1) = spanArgument[runStartIndex+1];
				  }
				  *makeCharArgument(root, argumentStartIndex) = spanArgument[runStartIndex];
				}
				argumentStartIndex = argumentOutputI++;
				runStartIndex = i;
			      }
			    }
			  } else {
			    size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
			    for (size_t i = 0; i < spanSize; i += valsPerArg) {
			      uint64_t tmp = 0;
			      for (size_t j = 0; j < valsPerArg; j++) {
				makeCharArgumentType(root, typeOutputI++);
				tmp |= static_cast<uint64_t>(spanArgument[i+j]) << (Argument_CHAR_SIZE * sizeof(Argument) * (valsPerArg - 1 - j));
			      }
			      *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			    }
			  }
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int32_t>) {
			  if (rleSpans) {
			    for(size_t i = 1; i < spanSize; i++) {
			      if (spanArgument[i] == spanArgument[i-1]) {
				makeIntArgumentType(root, argumentOutputI++);
				runCount++;
			      } else {
				if (runCount > 2) {
				  *makeIntArgumentRLE(root, argumentStartIndex, argumentStartIndex) =
				    spanArgument[runStartIndex];
				} else {
				  if (runCount > 1) {
				    *makeIntArgument(root, argumentStartIndex+1) = spanArgument[runStartIndex+1];
				  }
				  *makeIntArgument(root, argumentStartIndex) = spanArgument[runStartIndex];
				}
				argumentStartIndex = argumentOutputI++;
				runStartIndex = i;
			      }
			    }
			  } else {
			    size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
			    for (size_t i = 0; i < spanSize; i += valsPerArg) {
			      uint64_t tmp = 0;
			      for (size_t j = 0; j < valsPerArg; j++) {
				makeIntArgumentType(root, typeOutputI++);
				tmp |= static_cast<uint64_t>(spanArgument[i+j]) << (Argument_INT_SIZE * sizeof(Argument) * (valsPerArg - 1 - j));
			      }
			      *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			    }
			  }
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t>) {
			  if (rleSpans) {
			    for(size_t i = 1; i < spanSize; i++) {
			      if (spanArgument[i] == spanArgument[i-1]) {
				makeLongArgumentType(root, argumentOutputI++);
				runCount++;
			      } else {
				if (runCount > 2) {
				  *makeLongArgumentRLE(root, argumentStartIndex, argumentStartIndex) =
				    spanArgument[runStartIndex];
				} else {
				  if (runCount > 1) {
				    *makeLongArgument(root, argumentStartIndex+1) = spanArgument[runStartIndex+1];
				  }
				  *makeLongArgument(root, argumentStartIndex) = spanArgument[runStartIndex];
				}
				argumentStartIndex = argumentOutputI++;
				runStartIndex = i;
			      }
			    }
			  } else if (spanDict.find(spanI) != spanDict.end()) {
			    auto& dict = spanDict[spanI];
			    int64_t dictStartI = dictOutputI;
			    for (auto& entry : dict) {
			      int64_t value = std::get<int64_t>(entry.first);
			      int32_t& offset = entry.second;
			      offset = dictOutputI;
			      *makeLongDictionaryEntry(root, dictOutputI++) = value;
			    }
			    size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
			    for (size_t i = 0; i < spanSize; i += valsPerArg) {
			      uint64_t tmp = 0;
			      for (size_t j = 0; j < valsPerArg; j++) {
				// NEED DICT ENC LONG TYPE OR BIT ON LONG TYPE
				makeLongArgumentType(root, typeOutputI++);
				int32_t val = dict[DictKey(spanArgument[i+j])];
				tmp |= static_cast<uint64_t>(val) << (Argument_INT_SIZE * sizeof(Argument) * (valsPerArg - 1 - j));
			      }
			      *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			    }
			    setDictStartAndFlag(root, typeOutputI - spanSize, dictOutputI);
			  } else {
			    std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
			      *makeLongArgument(root, argumentOutputI++, typeOutputI++) = arg;
			    });
			  }
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, float_t>) {
			  if (rleSpans) {
			    for(size_t i = 1; i < spanSize; i++) {
			      if (spanArgument[i] == spanArgument[i-1]) {
				makeFloatArgumentType(root, argumentOutputI++);
				runCount++;
			      } else {
				if (runCount > 2) {
				  *makeFloatArgumentRLE(root, argumentStartIndex, argumentStartIndex) =
				    spanArgument[runStartIndex];
				} else {
				  if (runCount > 1) {
				    *makeFloatArgument(root, argumentStartIndex+1) = spanArgument[runStartIndex+1];
				  }
				  *makeFloatArgument(root, argumentStartIndex) = spanArgument[runStartIndex];
				}
				argumentStartIndex = argumentOutputI++;
				runStartIndex = i;
			      }
			    }
			  } else {
			    size_t valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE;
			    for (size_t i = 0; i < spanSize; i += valsPerArg) {
			      uint64_t tmp = 0;
			      for (size_t j = 0; j < valsPerArg; j++) {
				uint32_t rawVal;
				std::memcpy(&rawVal, &spanArgument[i+j], sizeof(rawVal));
				makeFloatArgumentType(root, typeOutputI++);
				tmp |= static_cast<uint64_t>(rawVal) << (Argument_FLOAT_SIZE * sizeof(Argument) * (valsPerArg - 1 - j));
			      }
			      *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			    }
			    // std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
			    //   *makeFloatArgument(root, argumentOutputI++) = arg;
			    // });
			  }
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
                                                           double_t>) {
			  if (rleSpans) {
			    for(size_t i = 1; i < spanSize; i++) {
			      if (spanArgument[i] == spanArgument[i-1]) {
				makeDoubleArgumentType(root, argumentOutputI++);
				runCount++;
			      } else {
				if (runCount > 2) {
				  *makeDoubleArgumentRLE(root, argumentStartIndex, argumentStartIndex) =
				    spanArgument[runStartIndex];
				} else {
				  if (runCount > 1) {
				    *makeDoubleArgument(root, argumentStartIndex+1) = spanArgument[runStartIndex+1];
				  }
				  *makeDoubleArgument(root, argumentStartIndex) = spanArgument[runStartIndex];
				}
				argumentStartIndex = argumentOutputI++;
				runStartIndex = i;
			      }
			    }
			  } else if (spanDict.find(spanI) != spanDict.end()) {
			    auto& dict = spanDict[spanI];
			    int64_t dictStartI = dictOutputI;
			    for (auto& entry : dict) {
			      double value = std::get<double>(entry.first);
			      int32_t& offset = entry.second;
			      offset = dictOutputI;
			      *makeDoubleDictionaryEntry(root, dictOutputI++) = value;
			    }
			    size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
			    for (size_t i = 0; i < spanSize; i += valsPerArg) {
			      uint64_t tmp = 0;
			      for (size_t j = 0; j < valsPerArg; j++) {
				// NEED DICT ENC LONG TYPE OR BIT ON LONG TYPE
				makeDoubleArgumentType(root, typeOutputI++);
				int32_t val = dict[DictKey(spanArgument[i+j])];
				tmp |= static_cast<uint64_t>(val) << (Argument_INT_SIZE * sizeof(Argument) * (valsPerArg - 1 - j));
			      }
			      *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			    }
			    setDictStartAndFlag(root, typeOutputI - spanSize, dictOutputI);
			  } else {
			    std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
			      *makeDoubleArgument(root, argumentOutputI++, typeOutputI++) = arg;
			    });
			  }
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
                                                           std::string>) {
			  if (rleSpans) {
			    auto prevStoredString = checkMapAndStoreString(spanArgument[0], stringMap, dictEncodeStrings);
			    for(size_t i = 1; i < spanSize; i++) {
			      auto storedString =
				checkMapAndStoreString(spanArgument[i], stringMap, dictEncodeStrings);
			      if (storedString == prevStoredString) {
				makeStringArgumentType(root, argumentOutputI++);
				runCount++;
			      } else {
				if (runCount > 2) {
				  *makeStringArgumentRLE(root, argumentStartIndex, argumentStartIndex) =
				    prevStoredString;
				} else {
				  if (runCount > 1) {
				    *makeStringArgument(root, argumentStartIndex+1) = prevStoredString;
				  }
				  *makeStringArgument(root, argumentStartIndex) = prevStoredString;
				}
				argumentStartIndex = argumentOutputI++;
				runStartIndex = i;
			      }
			    }
			  } else {
			    std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
			      auto storedString =
				checkMapAndStoreString(arg, stringMap, dictEncodeStrings);
			      *makeStringArgument(root, argumentOutputI++, typeOutputI++) = storedString;
			    });
			  }
                        } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
                                                           boss::Symbol>) {
			  if (rleSpans) {
			    auto prevStoredString = checkMapAndStoreString(spanArgument[0].getName(), stringMap, dictEncodeStrings);
			    for(size_t i = 1; i < spanSize; i++) {
			      auto storedString =
				checkMapAndStoreString(spanArgument[i].getName(), stringMap, dictEncodeStrings);
			      if (storedString == prevStoredString) {
				makeSymbolArgumentType(root, argumentOutputI++);
				runCount++;
			      } else {
				if (runCount > 2) {
				  *makeSymbolArgumentRLE(root, argumentStartIndex, argumentStartIndex) =
				    prevStoredString;
				} else {
				  if (runCount > 1) {
				    *makeSymbolArgument(root, argumentStartIndex+1) = prevStoredString;
				  }
				  *makeSymbolArgument(root, argumentStartIndex) = prevStoredString;
				}
				argumentStartIndex = argumentOutputI++;
				runStartIndex = i;
			      }
			    }
			  } else {
			    std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
			      auto storedString =
				checkMapAndStoreString(arg.getName(), stringMap, dictEncodeStrings);
			      *makeSymbolArgument(root, argumentOutputI++, typeOutputI++) = storedString;
			    });
			  }
                        } else {
                          print_type_name<std::decay_t<decltype(arg0)>>();
                          throw std::runtime_error("unknown type");
                        }
			spanI++;
                        setRLEArgumentFlagOrPropagateTypes(root, typeOutputI - spanSize,
                                                           spanSize);
                        //  CHECK HERE NEXT
                      }
                    },
                    std::forward<decltype(argument)>(argument));
              });
        });
    if(!children.empty()) {
      return flattenArguments(argumentOutputI, typeOutputI, std::move(children), expressionOutputI, dictOutputI, spanDict, spanI, stringMap, dictEncodeStrings, rleSpans);
    }
    return argumentOutputI;
  }

  ////////////////////////////////   Surface Area ////////////////////////////////

public:
  explicit SerializedExpression(boss::Expression&& input, bool dictEncodeStrings = true, bool rleSpans = false, bool dictEncodeDoublesAndLongs = true) {
    SpanDictionary spanDict;
    if (dictEncodeDoublesAndLongs) {
      spanDict = countUniqueArguments(input);
      root = allocateExpressionTree(countArguments(input),
				    countArgumentBytesDict(input, spanDict),
				    countExpressions(input),
				    calculateDictionaryBytes(spanDict),
				    countStringBytes(input, dictEncodeStrings),
				    allocateFunction);
    } else {
      root = allocateExpressionTree(countArguments(input),
				    rleSpans ? countRLEArguments(input) * sizeof(Argument) : countArgumentBytes(input),
				    countExpressions(input),
				    countStringBytes(input, dictEncodeStrings),
				    allocateFunction);
    }
    std::visit(utilities::overload(
				   [this, &spanDict, &dictEncodeStrings, &rleSpans](boss::ComplexExpression&& input) {
                     uint64_t argumentIterator = 0;
		     uint64_t typeIterator = 0;
                     uint64_t expressionIterator = 0;
		     uint64_t dictIterator = 0;
		     auto const childrenTypeCount = countArgumentTypes(input);
		     auto const childrenCount = countArgumentsPacked(input, spanDict);
                     auto const startChildArgOffset = 1;
                     auto const endChildArgOffset =
                         startChildArgOffset + childrenCount;
                     auto const startChildTypeOffset = 1;
                     auto const endChildTypeOffset =
                         startChildArgOffset + childrenTypeCount;
                     auto storedString = storeString(&root, input.getHead().getName().c_str());
                     *makeExpression(root, expressionIterator) =
                         PortableBOSSExpression{storedString,
						startChildArgOffset, endChildArgOffset,
						startChildTypeOffset, endChildTypeOffset};
                     *makeExpressionArgument(root, argumentIterator++, typeIterator++) = expressionIterator++;
                     auto inputs = std::vector<boss::ComplexExpression>();
                     inputs.push_back(std::move(input));
                     flattenArguments(argumentIterator, typeIterator, std::move(inputs), expressionIterator, dictIterator, spanDict, dictEncodeStrings, rleSpans);
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

  static void addIndexToStream(std::ostream& stream, SerializedExpression const& expr, size_t index, size_t typeIndex,
			       int64_t exprIndex, int64_t exprDepth) {
    for(auto i = 0; i < exprDepth; i++) {
      stream << "  ";
    }
    auto const& arguments = expr.flattenedArguments();
    auto const& types = expr.flattenedArgumentTypes();
    auto const& expressions = expr.expressionsBuffer();
    auto const& root = expr.root;
    
    auto testIndex = typeIndex;
    bool isRLE = (types[testIndex] & ArgumentType_RLE_BIT) != 0u;
    while (!isRLE && testIndex >= 0 && testIndex > typeIndex - 4) {
      testIndex--;
      isRLE |= (types[testIndex] & ArgumentType_RLE_BIT) != 0u;
    }
    auto validTypeIndex = isRLE ? testIndex : typeIndex;
    auto argumentType = static_cast<ArgumentType>((types[validTypeIndex] & (~ArgumentType_RLE_BIT)));

    if(exprIndex < 0) {
      stream << "ARG INDEX: " << index << " TYPE INDEX: " << typeIndex << " VALUE: ";
    } else {
      stream << "ARG INDEX: " << index << " TYPE INDEX: " << typeIndex << " SUB-EXPR INDEX: " << exprIndex << " VALUE: ";
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
      for (auto childI = expression.startChildOffset, childTypeI = expression.startChildTypeOffset;
	   childI < expression.endChildOffset && childTypeI < expression.endChildTypeOffset;
	   childTypeI++) {
        
	bool isChildRLE = (types[childTypeI] & ArgumentType_RLE_BIT) != 0u;
        if (isChildRLE) {
	  auto const argType = (ArgumentType)(types[childTypeI] & (~ArgumentType_RLE_BIT));
	  uint32_t spanSize =
	    (static_cast<uint32_t>(types[childTypeI + 4]) << 24) |
	    (static_cast<uint32_t>(types[childTypeI + 3]) << 16) |
	    (static_cast<uint32_t>(types[childTypeI + 2]) << 8)  |
	    (static_cast<uint32_t>(types[childTypeI + 1]));
	  auto prevChildTypeI = childTypeI;

	  if (argType == ArgumentType::ARGUMENT_TYPE_BOOL) {
            auto valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE; 
	    for (; childTypeI < prevChildTypeI + spanSize; childI++) {
	      int64_t& arg = arguments[childI].asLong;
	      uint64_t tmp = static_cast<uint64_t>(arg);
	      for (int64_t i = valsPerArg - 1; i >= 0 && childTypeI < prevChildTypeI + spanSize; i--, childTypeI++) {
		for(auto j = 0; j < exprDepth + 1; j++) {
		  stream << "  ";
		}
		stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " VALUE: ";
		uint8_t val = static_cast<uint8_t>((tmp >> (Argument_BOOL_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		stream << static_cast<bool>(val) << " TYPE: BOOL";
		stream << "\n";
	      }
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_CHAR) {
            auto valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE; 
	    for (; childTypeI < prevChildTypeI + spanSize; childI++) {
	      int64_t& arg = arguments[childI].asLong;
	      uint64_t tmp = static_cast<uint64_t>(arg);
	      for (int64_t i = valsPerArg - 1; i >= 0 && childTypeI < prevChildTypeI + spanSize; i--, childTypeI++) {
		for(auto j = 0; j < exprDepth + 1; j++) {
		  stream << "  ";
		}
		stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " VALUE: ";
		uint8_t val = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		stream << static_cast<int8_t>(val) << " TYPE: CHAR";
		stream << "\n";
	      }
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_INT) {
	    auto valsPerArg = sizeof(Argument) / Argument_INT_SIZE; 
	    for (; childTypeI < prevChildTypeI + spanSize; childI++) {
	      int64_t& arg = arguments[childI].asLong;
	      uint64_t tmp = static_cast<uint64_t>(arg);
	      for (int64_t i = valsPerArg - 1; i >= 0 && childTypeI < prevChildTypeI + spanSize; i--, childTypeI++) {
		for(auto j = 0; j < exprDepth + 1; j++) {
		  stream << "  ";
		}
		stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " VALUE: ";
		uint32_t val = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		stream << static_cast<int32_t>(val) << " TYPE: INT";
		stream << "\n";
	      }
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_LONG) {
	    for (; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
	      addIndexToStream(stream, expr, childI++, childTypeI, childTypeI - expression.startChildTypeOffset, exprDepth + 1);
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_FLOAT) {
            auto valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE; 
	    for (; childTypeI < prevChildTypeI + spanSize; childI++) {
	      int64_t& arg = arguments[childI].asLong;
	      uint64_t tmp = static_cast<uint64_t>(arg);
	      for (int64_t i = valsPerArg - 1; i >= 0 && childTypeI < prevChildTypeI + spanSize; i--, childTypeI++) {
		for(auto j = 0; j < exprDepth + 1; j++) {
		  stream << "  ";
		}
		stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " VALUE: ";
		uint32_t val = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		float realVal;
		std::memcpy(&realVal, &val, sizeof(realVal));
		stream << static_cast<float>(val) << " TYPE: FLOAT";
		stream << "\n";
	      }
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_STRING) {
	    for (; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
	      addIndexToStream(stream, expr, childI++, childTypeI, childTypeI - expression.startChildTypeOffset, exprDepth + 1);
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_STRING) {
	    for (; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
	      addIndexToStream(stream, expr, childI++, childTypeI, childTypeI - expression.startChildTypeOffset, exprDepth + 1);
	    }
	  }
	  --childTypeI;
	  // maybe need to --childI or --childTypeI
	} else {
	  addIndexToStream(stream, expr, childI++, childTypeI, childTypeI - expression.startChildTypeOffset, exprDepth + 1);
	}
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
    addIndexToStream(stream, expr, 0, 0, -1, 0);
    return stream;
  }

  BOSSArgumentPair deserializeArguments(uint64_t startChildOffset, uint64_t endChildOffset, uint64_t startChildTypeOffset, uint64_t endChildTypeOffset) const {
    boss::expressions::ExpressionArguments arguments;
    boss::expressions::ExpressionSpanArguments spanArguments;
    for(auto childTypeIndex = startChildTypeOffset, childArgIndex = startChildOffset;
	childTypeIndex < endChildTypeOffset && childArgIndex < endChildOffset;
	childTypeIndex++, childArgIndex++) {
      auto const& type = flattenedArgumentTypes()[childTypeIndex];
      auto const& isRLE = (type & ArgumentType_RLE_BIT) != 0U;

      // std::cout << "TYPE: " << (int64_t)(type & (~ArgumentType_RLE_BIT)) << " isRLE: " << (int64_t)isRLE << std::endl;

      if(isRLE) {

        auto const argType = (ArgumentType)(type & (~ArgumentType_RLE_BIT));
        uint32_t size =
	  (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 4]) << 24) |
	  (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 3]) << 16) |
	  (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 2]) << 8)  |
	  (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 1]));
        auto prevChildTypeIndex = childTypeIndex;

        auto const spanFunctors =
            std::unordered_map<ArgumentType,
                               std::function<boss::expressions::ExpressionSpanArgument()>>{
                {ArgumentType::ARGUMENT_TYPE_BOOL,
                 [&] {
		   std::vector<bool> data;
		   data.reserve(size);
		   size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
		   for(; childTypeIndex < prevChildTypeIndex + size;) {
                     int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t i = valsPerArg - 1;
			  i >= 0 && childTypeIndex < prevChildTypeIndex + size;
			  i--, childTypeIndex++) {
		       uint8_t val = static_cast<uint8_t>((tmp >> (Argument_BOOL_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		       data.push_back(static_cast<bool>(val));
		     }
                   }
                   // for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++) {
                   //   auto const& arg = flattenedArguments()[childTypeIndex];
                   //   data.push_back(arg.asBool);
                   // }
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
		     for (int64_t i = valsPerArg - 1;
			  i >= 0 && childTypeIndex < prevChildTypeIndex + size;
			  i--, childTypeIndex++) {
		       uint8_t val = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		       data.push_back(static_cast<int8_t>(val));
		     }
                   }
		   // for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++) {
                   //   auto const& arg = flattenedArguments()[childTypeIndex];
                   //   data.push_back(arg.asChar);
                   // }
                   return boss::expressions::Span<int8_t>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_INT,
                 [&] {
                   std::vector<int32_t> data;
                   data.reserve(size);
		   size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		   for(; childTypeIndex < prevChildTypeIndex + size;) {
                     int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t i = valsPerArg - 1;
			  i >= 0 && childTypeIndex < prevChildTypeIndex + size;
			  i--, childTypeIndex++) {
		       uint32_t val = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		       data.push_back(static_cast<int32_t>(val));
		     }
                   }
                   // for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++) {
                   //   auto const& arg = flattenedArguments()[childTypeIndex];
                   //   data.push_back(arg.asInt);
                   // }
                   return boss::expressions::Span<int32_t>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_LONG,
                 [&] {
                   std::vector<int64_t> data;
                   data.reserve(size);
                   for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++, childArgIndex++) {
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
		     for (int64_t i = valsPerArg - 1;
			  i >= 0 && childTypeIndex < prevChildTypeIndex + size;
			  i--, childTypeIndex++) {
		       uint32_t val = static_cast<uint32_t>((tmp >> (Argument_FLOAT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		       float realVal;
		       std::memcpy(&realVal, &val, sizeof(realVal));
		       data.push_back(realVal);
		     }
                   }
                   // for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++) {
                   //   auto const& arg = flattenedArguments()[childTypeIndex];
                   //   data.push_back(arg.asFloat);
                   // }
                   return boss::expressions::Span<float>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_DOUBLE,
                 [&] {
                   std::vector<double_t> data;
                   data.reserve(size);
                   for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++, childArgIndex++) {
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
                   for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++, childArgIndex++) {
                     auto const& arg = flattenedArguments()[childArgIndex];
                     data.push_back(boss::Symbol(viewString(root, arg.asString)));
                   }
                   return boss::expressions::Span<boss::Symbol>(std::move(data));
                 }},
                {ArgumentType::ARGUMENT_TYPE_STRING, [&childArgIndex, &childTypeIndex, &prevChildTypeIndex, &size, this] {
                   std::vector<std::string> data;
                   data.reserve(size);
                   for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++, childArgIndex++) {
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
    LazilyDeserializedExpression(SerializedExpression const& buffer, size_t argumentIndex, size_t typeIndex = 0)
      : buffer(buffer), argumentIndex(argumentIndex), typeIndex(typeIndex == 0 ? argumentIndex : typeIndex) {}

    size_t getArgumentIndex() const { return argumentIndex; }
    size_t getTypeIndex() const { return typeIndex; }

    bool operator==(boss::Expression const& other) const {
      if(other.index() != buffer.flattenedArgumentTypes()[typeIndex]) {
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
                                result &=
				  (LazilyDeserializedExpression(buffer, subExpressionPosition, subExpressionTypePosition) ==
                                     e.getDynamicArguments().at(typeI));
                              }
                              for(auto j = 0; j < e.getSpanArguments().size(); j++) {
                                std::visit(
                                    [&](auto&& typedSpanArg) {
                                      auto subSpanPosition = startChildOffset + argI;
                                      auto subSpanTypePosition = startChildTypeOffset + typeI;
                                      auto currSpan =
					(LazilyDeserializedExpression(buffer, subSpanPosition, subSpanTypePosition))
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
                                      typeI += typedSpanArg.size();
				      const auto& arg0 = typedSpanArg[0];
				      auto valsPerArg = sizeof(Argument) / sizeof(arg0);
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
      lazyExpr.buffer.addIndexToStream(stream, lazyExpr.buffer, lazyExpr.argumentIndex, lazyExpr.typeIndex, -1, 0);
      return stream;
    }

    ArgumentType getCurrentExpressionType() const {
      auto testIndex = typeIndex;
      bool isRLE = (buffer.flattenedArgumentTypes()[testIndex] & ArgumentType_RLE_BIT) != 0u;
      while (!isRLE && testIndex >= 0 && testIndex > typeIndex - 4) {
	testIndex--;
	isRLE |= (buffer.flattenedArgumentTypes()[testIndex] & ArgumentType_RLE_BIT) != 0u;
      }
      auto validTypeIndex = isRLE ? testIndex : typeIndex;
      auto const& type = buffer.flattenedArgumentTypes()[validTypeIndex];
      return static_cast<ArgumentType>((type & (~ArgumentType_RLE_BIT)));
    }
    
    ArgumentType getCurrentExpressionTypeExact() const {
      auto const& type = buffer.flattenedArgumentTypes()[typeIndex];
      return static_cast<ArgumentType>((type & (~ArgumentType_RLE_BIT)));
    }

    // ALTER TO CHANGE TYPE OFFSET TOO
    LazilyDeserializedExpression operator()(size_t childOffset, size_t childTypeOffset) const {
      auto const& expr = expression();
      assert(childOffset < expr.endChildOffset - expr.startChildOffset);
      assert(childTypeOffset < expr.endChildTypeOffset - expr.startChildTypeOffset);
      return {buffer, expr.startChildOffset + childOffset, expr.startChildTypeOffset + childTypeOffset};
    }

    LazilyDeserializedExpression operator[](size_t childOffset) const {
      auto const& expr = expression();
      assert(childOffset < expr.endChildOffset - expr.startChildOffset);
      assert(childOffset < expr.endChildTypeOffset - expr.startChildTypeOffset);
      return {buffer, expr.startChildOffset + childOffset, expr.startChildTypeOffset + childOffset};
    }

    // MAYBE SHOULD USE getCurrentExpressionType()
    LazilyDeserializedExpression operator[](std::string const& keyName) const {
      auto const& expr = expression();
      auto const& arguments = buffer.flattenedArguments();
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& expressions = buffer.expressionsBuffer();
      for(auto i = expr.startChildOffset, typeI = expr.startChildTypeOffset; i < expr.endChildOffset && typeI < expr.endChildTypeOffset; ++i, ++typeI) {
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

    // MAYBE SHOULD USE getCurrentExpressionType() as expressions can run
    Expression const& expression() const {
      auto const& arguments = buffer.flattenedArguments();
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& expressions = buffer.expressionsBuffer();
      assert(argumentTypes[typeIndex] == ArgumentType::ARGUMENT_TYPE_EXPRESSION);
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
      auto const& argumentType = buffer.flattenedArgumentTypes()[typeIndex];
      return argumentType == ArgumentType::ARGUMENT_TYPE_EXPRESSION;
    }

    size_t currentIsRLE() const {
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& type = argumentTypes[typeIndex];
      auto const& isRLE = (type & ArgumentType_RLE_BIT) != 0u;
      if (isRLE) {
	uint32_t size =
	  (static_cast<uint32_t>(argumentTypes[typeIndex + 4]) << 24) |
	  (static_cast<uint32_t>(argumentTypes[typeIndex + 3]) << 16) |
	  (static_cast<uint32_t>(argumentTypes[typeIndex + 2]) << 8)  |
	  (static_cast<uint32_t>(argumentTypes[typeIndex + 1]));
	std::cout << "Size: " << size << std::endl;
	return size;
      }
      return 0;
    }

    boss::Symbol getCurrentExpressionHead() const {
      auto const& expr = expression();
      return boss::Symbol(viewString(buffer.root, expr.symbolNameOffset));
    }

    boss::expressions::ExpressionSpanArgument getCurrentExpressionAsSpanWithTypeAndSize(ArgumentType type, size_t size) const {
      auto const& arguments = buffer.flattenedArguments();
      auto const spanFunctors =
          std::unordered_map<ArgumentType,
                             std::function<boss::expressions::ExpressionSpanArgument()>>{
              {ArgumentType::ARGUMENT_TYPE_BOOL,
               [&] {
                 std::vector<bool> data;
                 data.reserve(size);
		 size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
		 auto tempI = 0;
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI];
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = valsPerArg - 1;
			j >= 0 && i < size;
			j--, i++) {
		     uint8_t val = static_cast<uint8_t>((tmp >> (Argument_BOOL_SIZE * sizeof(Argument) * j)) & 0xFFFFFFFFUL);
		     data.push_back(static_cast<bool>(val));
		   }
		 }
                 // for(size_t i = 0; i < size; i++) {
                 //   auto const& arg = arguments[argumentIndex + i];
                 //   data.push_back(arg.asBool);
                 // }
                 return boss::expressions::Span<bool>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_CHAR,
               [&] {
                 std::vector<int8_t> data;
                 data.reserve(size);
                 size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		 auto tempI = 0;
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI];
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = valsPerArg - 1;
			j >= 0 && i < size;
			j--, i++) {
		     uint8_t val = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * j)) & 0xFFFFFFFFUL);
		     data.push_back(static_cast<int8_t>(val));
		   }
		 }
                 return boss::expressions::Span<int8_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_INT,
               [&] {
                 std::vector<int32_t> data;
                 data.reserve(size);
		 size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		 auto tempI = 0;
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI];
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = valsPerArg - 1;
			j >= 0 && i < size;
			j--, i++) {
		     uint32_t val = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * j)) & 0xFFFFFFFFUL);
		     data.push_back(static_cast<int32_t>(val));
		   }
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
		 size_t valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE;
		 auto tempI = 0;
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI];
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = valsPerArg - 1;
			j >= 0 && i < size;
			j--, i++) {
		     uint32_t val = static_cast<uint32_t>((tmp >> (Argument_FLOAT_SIZE * sizeof(Argument) * j)) & 0xFFFFFFFFUL);
		     float realVal;
		     std::memcpy(&realVal, &val, sizeof(realVal));
		     data.push_back(realVal);
		   }
		 }
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
    
    boss::expressions::ExpressionSpanArgument getCurrentExpressionAsSpan() const {
      size_t size = currentIsRLE();
      assert(size != 0);
      auto const& type = getCurrentExpressionType();
      return std::move(getCurrentExpressionAsSpanWithTypeAndSize(type, size));
    }

    boss::Expression getCurrentExpressionInSpanAtAs(size_t spanArgI, ArgumentType argumentType) const {
      // std::cout << "ARGI: " << argumentIndex << " TYPEI: " << typeIndex << std::endl;
      auto& argument = buffer.flattenedArguments()[argumentIndex];
      uint64_t tmp = static_cast<uint64_t>(argument.asLong);
      size_t valsPerArg;
      int64_t inArgI;
      uint32_t val;
      switch(argumentType) {
      case ArgumentType::ARGUMENT_TYPE_BOOL:
        return argument.asBool;
      case ArgumentType::ARGUMENT_TYPE_CHAR:
        return argument.asChar;
      case ArgumentType::ARGUMENT_TYPE_INT:
	valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
	inArgI = valsPerArg - 1 - (spanArgI % valsPerArg);
	val = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * inArgI)) & 0xFFFFFFFFUL);
	return static_cast<int32_t>(val);
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
	  buffer.deserializeArguments(expr.startChildOffset, expr.endChildOffset,
				      expr.startChildTypeOffset, expr.endChildTypeOffset);
        auto result = boss::ComplexExpression{s, {}, std::move(args), std::move(spanArgs)};
        return result;
      }
    }

    boss::Expression getCurrentExpressionInSpanAt(size_t spanArgI) const {
      auto argumentType = getCurrentExpressionType();
      return getCurrentExpressionInSpanAtAs(spanArgI, argumentType);
    }
    
    boss::Expression getCurrentExpressionAs(ArgumentType argumentType) const {
      auto& argument = buffer.flattenedArguments()[argumentIndex];
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
	  buffer.deserializeArguments(expr.startChildOffset, expr.endChildOffset,
				      expr.startChildTypeOffset, expr.endChildTypeOffset);
        auto result = boss::ComplexExpression{s, {}, std::move(args), std::move(spanArgs)};
        return result;
      }
    }

    // could use * operator for this
    // should this be && qualified?
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
	      uint32_t size =
		(static_cast<uint32_t>(argumentTypes[argumentIndex + 4]) << 24) |
		(static_cast<uint32_t>(argumentTypes[argumentIndex + 3]) << 16) |
		(static_cast<uint32_t>(argumentTypes[argumentIndex + 2]) << 8)  |
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
      auto [args, spanArgs] = deserializeArguments(expr.startChildOffset, expr.endChildOffset,
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
