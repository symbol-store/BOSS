#include "BOSS.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"
#include "ExpressionUtilities.hpp"
#include <cassert>
#include <cstdlib>
#include <cmath>
#include <inttypes.h>
#include <omp.h>
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
static const uint8_t& ArgumentType_DICT_ENC_BIT = PortableBOSSArgumentType_DICT_ENC_BIT;
static const uint8_t& ArgumentType_DICT_ENC_SIZE_BIT = PortableBOSSArgumentType_DICT_ENC_SIZE_BIT;
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

  // using DictKey = std::variant<bool, int8_t, int32_t, int64_t, float_t, double_t>;
  using DictKey = std::variant<int64_t, double_t, std::string>;
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
  Argument* spanDictionariesBuffer() const { return getSpanDictionaries(root); }

  //////////////////////////////// Count Unique Arguments ///////////////////////////////

  static size_t getArgumentSizeFromDictSize(ExpressionDictionary& dict) {
    if (dict.size() < ((2 << (Argument_CHAR_SIZE - 1)) - 1)) {
      return Argument_CHAR_SIZE;
    } else if (dict.size() < ((2 << (Argument_INT_SIZE - 1)) - 1)) {
      return Argument_INT_SIZE;
    } else {
      return Argument_LONG_SIZE;
    }
  }
  
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
    (countUniqueArgumentsStaticsAndSpans(std::get<Is>(tuple), dict, spanI), ...);
  };

  static SpanDictionary countUniqueArguments(boss::Expression const& input) {
    SpanDictionary res;
    size_t spanI = 0;
    int64_t level = 1;
    while (countUniqueArgumentsAtLevel(input, res, spanI, level)) {
      level++;
    }
    return std::move(res);
  };

  static bool countUniqueArgumentsAtLevel(boss::Expression const& input, SpanDictionary& dict, size_t& spanI, int64_t level) {
    if (level == 1) {
      countUniqueArgumentsStaticsAndSpans(input, dict, spanI);
      return true;
    }
    bool recurse = false;
    std::visit(
	       [&dict, &spanI, &level, &recurse](auto& input) {
		 if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
	     	   std::for_each(input.getDynamicArguments().begin(),
				 input.getDynamicArguments().end(),
				 [&dict, &spanI, &level, &recurse](auto const& argument) {
				   recurse |= countUniqueArgumentsAtLevel(argument, dict, spanI, level - 1);
				 });
		 }
	       },
	       input);
    return recurse;
  };

  static void countUniqueArgumentsStaticsAndSpans(boss::Expression const& input, SpanDictionary& dict, size_t& spanI) {
    std::visit(
	       [&dict, &spanI](auto& input) {
		 if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
		   countUniqueArgumentsInTuple(dict, spanI, input.getStaticArguments(),
					       std::make_index_sequence<std::tuple_size_v<
					       std::decay_t<decltype(input.getStaticArguments())>>>());
		   std::for_each(input.getSpanArguments().begin(), input.getSpanArguments().end(),
				 [&dict, &spanI](auto const& argument) {
				   std::visit([&](auto const& spanArgument) {
				     ExpressionDictionary spanDict;
				     auto spanSize = spanArgument.size();
				     auto const& arg0 = spanArgument[0];
				     if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t> ||
						  std::is_same_v<std::decay_t<decltype(arg0)>, double_t> ||
						  std::is_same_v<std::decay_t<decltype(arg0)>, std::string>) {
				       std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto arg) {
					 checkMapAndIncrement(DictKey(arg), spanDict);
				       });
				       if (spanDict.size() < (spanSize / 2) &&
					   spanDict.size() < ((2 << (Argument_LONG_SIZE - 1)) - 1)) {
					 dict[spanI] = std::move(spanDict);
				       }
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
				   input.getSpanArguments().begin(), input.getSpanArguments().end(), uint64_t(0),
                       [](auto runningSum, auto const& argument) -> uint64_t {
                         return runningSum +
                                std::visit([&](auto const& spanArgument) -> uint64_t {
				  uint64_t spanBytes = 0;
				  uint64_t spanSize = spanArgument.size();
				  auto const& arg0 = spanArgument[0];
				  if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, bool> ||
					       std::is_same_v<std::decay_t<decltype(arg0)>, std::_Bit_reference>) {
				    spanBytes = spanSize * static_cast<uint64_t>(sizeof(bool));
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int8_t>) {
				    spanBytes = spanSize * static_cast<uint64_t>(sizeof(int8_t));
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int16_t>) {
				    spanBytes = spanSize * static_cast<uint64_t>(sizeof(int16_t));
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int32_t>) {
				    spanBytes = spanSize * static_cast<uint64_t>(sizeof(int32_t));
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t>) {
				    spanBytes = spanSize * static_cast<uint64_t>(sizeof(int64_t));
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, float_t>) {
				    spanBytes = spanSize * static_cast<uint64_t>(sizeof(float_t));
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, double_t>) {
				    spanBytes = spanSize * static_cast<uint64_t>(sizeof(double_t));
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, std::string>) {
				    spanBytes = spanSize * static_cast<uint64_t>(sizeof(Argument));
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, boss::Symbol>) {
				    spanBytes = spanSize * static_cast<uint64_t>(sizeof(Argument));
				  } else {
				    print_type_name<std::decay_t<decltype(arg0)>>();
				    throw std::runtime_error("unknown type in span");
				  }
				  // std::cout << "SPAN BYTES: " << spanBytes << std::endl;
				  // std::cout << "ROUNDED SPAN BYTES: " << ((spanBytes + sizeof(Argument) - 1) & -sizeof(Argument)) << std::endl;
				  return (spanBytes + static_cast<uint64_t>(sizeof(Argument)) - 1) & -(static_cast<uint64_t>(sizeof(Argument)));
				},
				  std::forward<decltype(argument)>(argument));
                       });
          }
          return static_cast<uint64_t>(sizeof(Argument));
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
    return (countArgumentBytesDictStaticsAndSpans(std::get<Is>(tuple), dict, spanI) + ... + 0);
  };

  static uint64_t countArgumentBytesDict(boss::Expression const& input, SpanDictionary& dict) {
    size_t spanI = 0;
    uint64_t count = 0;
    int64_t level = 1;
    while (countArgumentBytesDictAtLevel(input, count, dict, spanI, level)) {
      level++;
    }
    return count;
  };

  static bool countArgumentBytesDictAtLevel(boss::Expression const& input, uint64_t& count, SpanDictionary& dict, size_t& spanI, int64_t level) {
    if (level == 1) {
      count += countArgumentBytesDictStaticsAndSpans(input, dict, spanI);
      return true;
    }
    bool recurse = false;
    std::visit(
	       [&count, &dict, &spanI, &level, &recurse](auto& input) {
		 if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
	     	   std::for_each(input.getDynamicArguments().begin(),
				 input.getDynamicArguments().end(),
				 [&count, &dict, &spanI, &level, &recurse](auto const& argument) {
				   recurse |= countArgumentBytesDictAtLevel(argument, count, dict, spanI, level - 1);
				 });
		 }
	       },
	       input);
    return recurse;
  };
  
  static uint64_t countArgumentBytesDictStaticsAndSpans(boss::Expression const& input, SpanDictionary& dict, size_t& spanI) {
    return std::visit(
	[&dict, &spanI](auto& input) -> size_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
            return Argument_EXPRESSION_SIZE +
	      countArgumentBytesInTupleDict(dict, spanI,
                       input.getStaticArguments(),
                       std::make_index_sequence<std::tuple_size_v<
                           std::decay_t<decltype(input.getStaticArguments())>>>()) +
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
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int16_t>) {
				    spanBytes = spanSize * Argument_SHORT_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int32_t>) {
				    spanBytes = spanSize * Argument_INT_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t>) {
				    if (dict.find(spanI) == dict.end()) {
				      spanBytes = spanSize * Argument_LONG_SIZE;
				    } else {
				      auto& spanDict = dict[spanI];
				      spanBytes = spanSize * getArgumentSizeFromDictSize(spanDict);
				    }
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, float_t>) {
				    spanBytes = spanSize * Argument_FLOAT_SIZE;
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, double_t>) {
				    if (dict.find(spanI) == dict.end()) {
				      spanBytes = spanSize * Argument_DOUBLE_SIZE;
				    } else {
				      auto& spanDict = dict[spanI];
				      spanBytes = spanSize * getArgumentSizeFromDictSize(spanDict);
				    }
				  } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, std::string>) {
				    if (dict.find(spanI) == dict.end()) {
				      spanBytes = spanSize * Argument_STRING_SIZE;
				    } else {
				      auto& spanDict = dict[spanI];
				      spanBytes = spanSize * getArgumentSizeFromDictSize(spanDict);
				    }
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
  };

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
				   input.getSpanArguments().begin(), input.getSpanArguments().end(), uint64_t(0),
                       [](uint64_t runningSum, auto const& argument) -> uint64_t {
                         return runningSum +
                                std::visit([&](auto const& argument) -> uint64_t { return argument.size(); },
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
  static uint64_t countStringBytesInTuple(std::unordered_set<std::string>& stringSet, bool dictEncodeStrings,
					  TupleLike const& tuple,
                                          std::index_sequence<Is...> /*unused*/) {
    return (countStringBytes(std::get<Is>(tuple), stringSet, dictEncodeStrings) + ... + 0);
  };

  static uint64_t countStringBytes(boss::Expression const& input, bool dictEncodeStrings = true) {
    std::unordered_set<std::string> stringSet;
    return 1 + countStringBytes(input, stringSet, dictEncodeStrings);
  }
  
  static uint64_t countStringBytes(boss::Expression const& input, std::unordered_set<std::string>& stringSet, bool dictEncodeStrings) {
    return std::visit(
        [&](auto& input) -> uint64_t {
          if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
	    uint64_t headBytes = !dictEncodeStrings * (strlen(input.getHead().getName().c_str()) + 1);
	    if (dictEncodeStrings && stringSet.find(input.getHead().getName()) == stringSet.end()) {
	      stringSet.insert(input.getHead().getName());
	      headBytes = strlen(input.getHead().getName().c_str()) + 1;
	    }
	    uint64_t staticArgsBytes =
	      countStringBytesInTuple(stringSet, dictEncodeStrings,
				      input.getStaticArguments(),
				      std::make_index_sequence<std::tuple_size_v<
				      std::decay_t<decltype(input.getStaticArguments())>>>());
	    uint64_t dynamicArgsBytes =
	      std::accumulate(input.getDynamicArguments().begin(),
			      input.getDynamicArguments().end(), uint64_t(0),
			      [&](uint64_t runningSum, auto const& argument) -> uint64_t {
				return runningSum + countStringBytes(argument, stringSet, dictEncodeStrings);
			      });
	    uint64_t spanArgsBytes =
	      std::accumulate(
			      input.getSpanArguments().begin(), input.getSpanArguments().end(), uint64_t(0),
                       [&](size_t runningSum, auto const& argument) -> uint64_t {
                         return runningSum +
                                std::visit(
                                    [&](auto const& argument) -> uint64_t {
                                      if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
                                                                  boss::Span<std::string>>) {
                                        return std::accumulate(
							       argument.begin(), argument.end(), uint64_t(0),
                                            [&](uint64_t innerRunningSum, auto const& stringArgument) -> uint64_t {
					      uint64_t resRunningSum = innerRunningSum +
						(!dictEncodeStrings * (strlen(stringArgument.c_str()) + 1));
					      if (dictEncodeStrings && stringSet.find(stringArgument) == stringSet.end()) {
						stringSet.insert(stringArgument);	
						resRunningSum += strlen(stringArgument.c_str()) + 1; 
					      }
					      return resRunningSum;
                                            });
                                      } else if constexpr(std::is_same_v<std::decay_t<decltype(argument)>,
							  boss::Span<boss::Symbol>>) {
                                        return std::accumulate(
							       argument.begin(), argument.end(), uint64_t(0),
                                            [&](uint64_t innerRunningSum, auto const& stringArgument) -> uint64_t {
					      uint64_t resRunningSum = innerRunningSum +
						(!dictEncodeStrings * (strlen(stringArgument.getName().c_str()) + 1));
					      if (dictEncodeStrings && stringSet.find(stringArgument.getName()) == stringSet.end()) {
						stringSet.insert(stringArgument.getName());	
						resRunningSum += strlen(stringArgument.getName().c_str()) + 1; 
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
		      expression.getSpanArguments().begin(), expression.getSpanArguments().end(), uint64_t(0),
		      [](uint64_t runningSum, auto const& spanArg) -> uint64_t {
			return runningSum +
			  std::visit([&](auto const& spanArg) -> uint64_t { return spanArg.size(); },
				     std::forward<decltype(spanArg)>(spanArg));
		      });
  }

// template <typename TupleLike, uint64_t... Is>
//   static void incSpanArgumentsInTuple(size_t& spanI, TupleLike const& tuple,
//                                         std::index_sequence<Is...> /*unused*/) {
//     (incSpanArguments(std::get<Is>(tuple), spanI), ...);
//   }
  
//   static void incSpanDynamicsArguments(boss::Expression const& input, size_t& spanI) {
//     return std::visit(
// 		      [&spanI](auto& input) {
//           if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
//             std::for_each(input.getDynamicArguments().begin(),
// 			  input.getDynamicArguments().end(),
// 			  [&spanI](auto const& argument) {
// 			    incSpanDynamicsArguments(argument, spanI);
// 			  });
// 	    std::for_each(
// 			  input.getSpanArguments().begin(), input.getSpanArguments().end(),
// 			  [&spanI](auto const& argument) {
// 			    spanI++;
// 			  });
//           }
//         },
//         input);
//   }

  uint64_t countArgumentsPacked(boss::ComplexExpression const& expression, SpanDictionary& spanDict) {
    size_t spanI = 0;
    return countArgumentsPacked(expression, spanDict, spanI);
  }

  // bool countArgumentsPackedAtLevel(boss::ComplexExpression const& expression, uint64_t& count, SpanDictionary& spanDict, size_t& spanI, int64_t level) {
  //   if (level == 1) {
  //     countUniqueArgumentsStaticsAndSpans(input, dict, spanI);
  //     return true;
  //   }
  //   bool recurse = false;
  //   std::visit(
  // 	       [&count, &dict, &spanI, &level, &recurse](auto& input) {
  // 		 if constexpr(std::is_same_v<std::decay_t<decltype(input)>, boss::ComplexExpression>) {
  // 	     	   std::for_each(input.getDynamicArguments().begin(),
  // 				 input.getDynamicArguments().end(),
  // 				 [&count, &dict, &spanI, &level, &recurse](auto const& argument) {
  // 				   recurse |= countArgumentsPackedAtLevel(argument, dict, spanI, level - 1);
  // 				 });
  // 		 }
  // 	       },
  // 	       input);
  //   return recurse;
  // }

  // uint64_t countArgumentsPackedDynamics(boss::ComplexExpression const& expression, SpanDictionary& spanDict, size_t spanIInput) {
  //   uint64_t dynamicsCount = expression.getDynamicArguments().size();
  //   std::for_each(expression.getDynamicArguments().begin(),
  // 		  expression.getDynamicArguments().end(),
  // 		  [&spanI](auto const& argument) {
  // 		    incSpanArguments(argument, spanI);
  // 		  });
  //   return dynamicsCount;
  // }

  uint64_t countArgumentsPacked(boss::ComplexExpression const& expression, SpanDictionary& spanDict, size_t spanIInput) {
    size_t spanI = spanIInput;
    uint64_t staticsCount = std::tuple_size_v<std::decay_t<decltype(expression.getStaticArguments())>>;
    uint64_t dynamicsCount = expression.getDynamicArguments().size();
    // incSpanArgumentsInTuple(spanI,
    // 			    expression.getStaticArguments(),
    // 			    std::make_index_sequence<std::tuple_size_v<
    // 			    std::decay_t<decltype(expression.getStaticArguments())>>>());
    
    uint64_t spansCount = 
      std::accumulate(
		      expression.getSpanArguments().begin(), expression.getSpanArguments().end(), uint64_t(0),
		      [&spanDict, &spanI](uint64_t runningSum, auto const& spanArg) -> uint64_t {
			return runningSum +
			  std::visit([&](auto const& spanArgument) -> uint64_t {
			    uint64_t spanSize = spanArgument.size();
			    auto const& arg0 = spanArgument[0];
			    uint64_t valsPerArg = static_cast<uint64_t>(sizeof(arg0) > sizeof(Argument) ? 1 : sizeof(Argument) / sizeof(arg0));
			    if (spanDict.find(spanI) != spanDict.end()) {
			      auto& dict = spanDict[spanI];
			      valsPerArg = sizeof(Argument) / getArgumentSizeFromDictSize(dict); 
			    }
			    spanI++;
			    return (spanSize + valsPerArg - 1) / valsPerArg;
			  },
			    std::forward<decltype(spanArg)>(spanArg));
		      });
    return staticsCount + dynamicsCount + spansCount;
  }
  
  template <typename TupleLike, uint64_t... Is>
  void flattenArgumentsInTuple(TupleLike&& tuple, std::index_sequence<Is...> /*unused*/,
                               uint64_t& argumentOutputI, uint64_t& typeOutputI, uint64_t& dictOutputI, SpanDictionary& spanDict,
			       size_t& spanI, std::unordered_map<std::string, size_t>& stringMap,
			       bool dictEncodeStrings) {
    (flattenArguments(std::get<Is>(tuple), argumentOutputI, typeOutputI, dictOutputI, spanDict, spanI, stringMap, dictEncodeStrings), ...);
  };

  // assuming RLE encode for now
  uint64_t flattenArguments(uint64_t argumentOutputI, uint64_t typeOutputI,
			    std::vector<boss::ComplexExpression>&& inputs,
			    uint64_t& expressionOutputI, uint64_t dictOutputI, SpanDictionary& spanDict,
			    bool dictEncodeStrings = true) {
    std::unordered_map<std::string, size_t> stringMap;
    size_t spanI = 0;
    return flattenArguments(argumentOutputI, typeOutputI, std::move(inputs), expressionOutputI, dictOutputI, spanDict, spanI, stringMap, dictEncodeStrings);
  }

  uint64_t flattenArguments(uint64_t argumentOutputI, uint64_t typeOutputI,
			    std::vector<boss::ComplexExpression>&& inputs,
                            uint64_t& expressionOutputI, uint64_t dictOutputI, SpanDictionary& spanDict, size_t& spanI,
			    std::unordered_map<std::string, size_t>& stringMap,
			    bool dictEncodeStrings) {
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
	 &dictEncodeStrings](boss::ComplexExpression&& input) {
          auto [head, statics, dynamics, spans] = std::move(input).decompose();
          flattenArgumentsInTuple(
              statics,
              std::make_index_sequence<std::tuple_size_v<std::decay_t<decltype(statics)>>>(),
              argumentOutputI, typeOutputI, dictOutputI, spanDict, spanI, stringMap, dictEncodeStrings);
          std::for_each(
              std::make_move_iterator(dynamics.begin()), std::make_move_iterator(dynamics.end()),
              [this, &argumentOutputI, &typeOutputI, &children, &expressionOutputI, nextLayerTypeOffset, nextLayerOffset,
               &childrenCountRunningSum, &childrenTypeCountRunningSum, &stringMap, &dictEncodeStrings, &spanDict, &spanI](auto&& argument) {
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
			// std::cout << "HEAD: " << argument.getHead().getName() << std::endl;
			// std::cout << "  argOutput: " << argumentOutputI << std::endl;
			// std::cout << "  typeOutput: " << typeOutputI << std::endl;
			// std::cout << "  exprOutput: " << expressionOutputI << std::endl;
			// std::cout << "  startChildArgOffset: " << startChildArgOffset << std::endl;
			// std::cout << "  endChildArgOffset: " << endChildArgOffset << std::endl;
			// std::cout << "  startChildArgTypeOffset: " << startChildTypeOffset << std::endl;
			// std::cout << "  endChildArgTypeOffset: " << endChildTypeOffset << std::endl;
			
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
              [this, &argumentOutputI, &typeOutputI, &dictOutputI, &spanDict, &spanI, &stringMap, &dictEncodeStrings](auto&& argument) {
                std::visit(
			   [&](auto&& spanArgument) {
			     auto spanSize = spanArgument.size();
			     auto const& arg0 = spanArgument[0];
			     if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, bool> ||
					  std::is_same_v<std::decay_t<decltype(arg0)>,
					  std::_Bit_reference>) {
			       size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
			       for (size_t i = 0; i < spanSize; i += valsPerArg) {
				 uint64_t tmp = 0;
				 for (size_t j = 0; j < valsPerArg && i+j < spanSize; j++) {
				   makeBoolArgumentType(root, typeOutputI++);
				   tmp |= static_cast<uint64_t>(spanArgument[i+j]) << (Argument_BOOL_SIZE * sizeof(Argument) * j);
				 }
				 *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			       }
			       // std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto arg) {
			       //   *makeBoolArgument(root, argumentOutputI++) = arg;
			       // });
			     } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int8_t>) {
			       size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
			       for (size_t i = 0; i < spanSize; i += valsPerArg) {
				 uint64_t tmp = 0;
				 for (size_t j = 0; j < valsPerArg && i+j < spanSize; j++) {
				   makeCharArgumentType(root, typeOutputI++);
				   tmp |= static_cast<uint64_t>(spanArgument[i+j]) << (Argument_CHAR_SIZE * sizeof(Argument) * j);
				 }
				 *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			       }
			     } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int16_t>) {
			       size_t valsPerArg = sizeof(Argument) / Argument_SHORT_SIZE;
			       for (size_t i = 0; i < spanSize; i += valsPerArg) {
				 uint64_t tmp = 0;
				 for (size_t j = 0; j < valsPerArg && i+j < spanSize; j++) {
				   makeShortArgumentType(root, typeOutputI++);
				   tmp |= static_cast<uint64_t>(spanArgument[i+j]) << (Argument_SHORT_SIZE * sizeof(Argument) * j);
				 }
				 *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			       }
			     } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int32_t>) {
			       size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
			       for (size_t i = 0; i < spanSize; i += valsPerArg) {
				 uint64_t tmp = 0;
				 for (size_t j = 0; j < valsPerArg && i+j < spanSize; j++) {
				   makeIntArgumentType(root, typeOutputI++);
				   tmp |= static_cast<uint64_t>(spanArgument[i+j]) << (Argument_INT_SIZE * sizeof(Argument) * j);
				 }
				 *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			       }
			     } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, int64_t>) {
			       if (spanDict.find(spanI) != spanDict.end()) {
				 auto& dict = spanDict[spanI];
				 int64_t dictStartI = dictOutputI;
				 for (auto& entry : dict) {
				   int64_t value = std::get<int64_t>(entry.first);
				   int32_t& offset = entry.second;
				   offset = dictOutputI;
				   *makeLongDictionaryEntry(root, dictOutputI++) = value;
				 }
				 size_t argumentSize = getArgumentSizeFromDictSize(dict);
				 size_t valsPerArg = sizeof(Argument) / argumentSize;
				 for (size_t i = 0; i < spanSize; i += valsPerArg) {
				   uint64_t tmp = 0;
				   for (size_t j = 0; j < valsPerArg && i+j < spanSize; j++) {
				     makeLongArgumentType(root, typeOutputI++);
				     if (argumentSize == Argument_CHAR_SIZE) {
				       int8_t val = static_cast<int8_t>(dict[DictKey(spanArgument[i+j])]);
				       tmp |= static_cast<uint64_t>(val) << (argumentSize * sizeof(Argument) * j);
				     } else if (argumentSize == Argument_INT_SIZE) {
				       int32_t val = dict[DictKey(spanArgument[i+j])]; 
				       tmp |= static_cast<uint64_t>(val) << (argumentSize * sizeof(Argument) * j);
				     }				
				   }
				   *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
				 }
				 setDictStartAndFlag(root, typeOutputI - spanSize, dictStartI, argumentSize);
			       } else {
				 std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
				   *makeLongArgument(root, argumentOutputI++, typeOutputI++) = arg;
				 });
			       }
			     } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>, float_t>) {
			       size_t valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE;
			       for (size_t i = 0; i < spanSize; i += valsPerArg) {
				 uint64_t tmp = 0;
				 for (size_t j = 0; j < valsPerArg && i+j < spanSize; j++) {
				   uint32_t rawVal;
				   std::memcpy(&rawVal, &spanArgument[i+j], sizeof(rawVal));
				   makeFloatArgumentType(root, typeOutputI++);
				   tmp |= static_cast<uint64_t>(rawVal) << (Argument_FLOAT_SIZE * sizeof(Argument) * j);
				 }
				 *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
			       }
			       // std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
			       //   *makeFloatArgument(root, argumentOutputI++) = arg;
			       // });
			     } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
						 double_t>) {
			       if (spanDict.find(spanI) != spanDict.end()) {
				 auto& dict = spanDict[spanI];
				 int64_t dictStartI = dictOutputI;
				 for (auto& entry : dict) {
				   double value = std::get<double>(entry.first);
				   int32_t& offset = entry.second;
				   offset = dictOutputI;
				   *makeDoubleDictionaryEntry(root, dictOutputI++) = value;
				 }
				 size_t argumentSize = getArgumentSizeFromDictSize(dict);
				 size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
				 for (size_t i = 0; i < spanSize; i += valsPerArg) {
				   uint64_t tmp = 0;
				   for (size_t j = 0; j < valsPerArg && i+j < spanSize; j++) {
				     // NEED DICT ENC LONG TYPE OR BIT ON LONG TYPE
				     makeDoubleArgumentType(root, typeOutputI++);
				     if (argumentSize == Argument_CHAR_SIZE) {
				       int8_t val = static_cast<int8_t>(dict[DictKey(spanArgument[i+j])]);
				       tmp |= static_cast<uint64_t>(val) << (argumentSize * sizeof(Argument) * j);
				     } else if (argumentSize == Argument_INT_SIZE) {
				       int32_t val = dict[DictKey(spanArgument[i+j])];
				       tmp |= static_cast<uint64_t>(val) << (argumentSize * sizeof(Argument) * j);
				     }				
				   }
				   *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
				 }
				 setDictStartAndFlag(root, typeOutputI - spanSize, dictStartI, argumentSize);
			       } else {
				 std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
				   *makeDoubleArgument(root, argumentOutputI++, typeOutputI++) = arg;
				 });
			       }
			     } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
						 std::string>) {
			       if (spanDict.find(spanI) != spanDict.end()) {
				 auto& dict = spanDict[spanI];
				 int64_t dictStartI = dictOutputI;
				 for (auto& entry : dict) {
				   std::string value = std::get<std::string>(entry.first);
				   int32_t& offset = entry.second;
				   offset = dictOutputI;
				   auto storedString =
				     checkMapAndStoreString(value, stringMap, dictEncodeStrings);
				   *makeStringDictionaryEntry(root, dictOutputI++) = storedString;
				 }
				 size_t argumentSize = getArgumentSizeFromDictSize(dict);
				 size_t valsPerArg = sizeof(Argument) / argumentSize;
				 for (size_t i = 0; i < spanSize; i += valsPerArg) {
				   uint64_t tmp = 0;
				   for (size_t j = 0; j < valsPerArg && i+j < spanSize; j++) {
				     makeLongArgumentType(root, typeOutputI++);
				     if (argumentSize == Argument_CHAR_SIZE) {
				       int8_t val = static_cast<int8_t>(dict[DictKey(spanArgument[i+j])]);
				       tmp |= static_cast<uint64_t>(val) << (argumentSize * sizeof(Argument) * j);
				     } else if (argumentSize == Argument_INT_SIZE) {
				       int32_t val = dict[DictKey(spanArgument[i+j])];
				       tmp |= static_cast<uint64_t>(val) << (argumentSize * sizeof(Argument) * j);
				     }				
				   }
				   *makeArgument(root, argumentOutputI++) = static_cast<int64_t>(tmp);
				 }
				 setDictStartAndFlag(root, typeOutputI - spanSize, dictStartI, argumentSize);
			       } else {
				 std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
				   auto storedString =
				     checkMapAndStoreString(arg, stringMap, dictEncodeStrings);
				   *makeStringArgument(root, argumentOutputI++, typeOutputI++) = storedString;
				 });
			       }
			     } else if constexpr(std::is_same_v<std::decay_t<decltype(arg0)>,
						 boss::Symbol>) {
			       std::for_each(spanArgument.begin(), spanArgument.end(), [&](auto& arg) {
				 auto storedString =
				   checkMapAndStoreString(arg.getName(), stringMap, dictEncodeStrings);
				 *makeSymbolArgument(root, argumentOutputI++, typeOutputI++) = storedString;
			       });
			     } else {
			       print_type_name<std::decay_t<decltype(arg0)>>();
			       throw std::runtime_error("unknown type");
			     }
			     spanI++;
			
			     if(spanSize >= ArgumentType_RLE_MINIMUM_SIZE) {
			       setRLEArgumentFlagOrPropagateTypes(root, typeOutputI - spanSize,
								  spanSize);
			       //  CHECK HERE NEXT
			     }
                    },
                    std::forward<decltype(argument)>(argument));
              });
        });
    if(!children.empty()) {
      return flattenArguments(argumentOutputI, typeOutputI, std::move(children), expressionOutputI, dictOutputI, spanDict, spanI, stringMap, dictEncodeStrings);
    }
    return argumentOutputI;
  }

  ////////////////////////////////   Surface Area ////////////////////////////////

public:
  explicit SerializedExpression(boss::Expression&& input, bool dictEncodeStrings = true, bool dictEncodeDoublesAndLongs = false) {
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
				    countArgumentBytes(input),
				    countExpressions(input),
				    countStringBytes(input, dictEncodeStrings),
				    allocateFunction);
    }
    std::visit(utilities::overload(
				   [this, &spanDict, &dictEncodeStrings](boss::ComplexExpression&& input) {
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
                     flattenArguments(argumentIterator, typeIterator, std::move(inputs), expressionIterator, dictIterator, spanDict, dictEncodeStrings);
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

  static void addIndexToStream(std::ostream& stream, SerializedExpression const& expr, size_t index, size_t typeIndex,
			       int64_t exprIndex, int64_t exprDepth) {
    for(auto i = 0; i < exprDepth; i++) {
      stream << "  ";
    }
    auto const& arguments = expr.flattenedArguments();
    auto const& types = expr.flattenedArgumentTypes();
    auto const& expressions = expr.expressionsBuffer();
    auto const& dicts = expr.spanDictionariesBuffer();
    auto const& root = expr.root;
    
    auto testIndex = typeIndex;
    bool isRLE = (types[testIndex] & ArgumentType_RLE_BIT) != 0u;
    while (!isRLE && testIndex >= 0 && testIndex > typeIndex - 4) {
      testIndex--;
      isRLE |= (types[testIndex] & ArgumentType_RLE_BIT) != 0u;
    }
    auto validTypeIndex = isRLE ? testIndex : typeIndex;
    auto argumentType = static_cast<ArgumentType>(types[validTypeIndex] & ArgumentType_MASK);

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
      // std::cout << "INDEX: " << index << std::endl;
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
	bool isDictEnc = (types[childTypeI] & ArgumentType_DICT_ENC_BIT) != 0U;

	if (isChildRLE) {
	  auto const argType = static_cast<ArgumentType>(types[childTypeI] & ArgumentType_MASK);
	  uint32_t spanSize =
	    (static_cast<uint32_t>(types[childTypeI + 4]) << 24) |
	    (static_cast<uint32_t>(types[childTypeI + 3]) << 16) |
	    (static_cast<uint32_t>(types[childTypeI + 2]) << 8)  |
	    (static_cast<uint32_t>(types[childTypeI + 1]));
	  uint64_t dictI = 0;
	  size_t dictOffsetArgumentSize = 0; 
	  if (isDictEnc) {
	    dictOffsetArgumentSize = (types[childTypeI] & ArgumentType_DICT_ENC_SIZE_BIT) == 0U ?
	      Argument_CHAR_SIZE :
	      Argument_INT_SIZE;
	    dictI =
	      (static_cast<uint64_t>(types[childTypeI + 12]) << 56) |
	      (static_cast<uint64_t>(types[childTypeI + 11]) << 48) |
	      (static_cast<uint64_t>(types[childTypeI + 10]) << 40) |
	      (static_cast<uint64_t>(types[childTypeI + 9]) <<  32) |
	      (static_cast<uint64_t>(types[childTypeI + 8]) << 24)  |
	      (static_cast<uint64_t>(types[childTypeI + 7]) << 16)  |
	      (static_cast<uint64_t>(types[childTypeI + 6]) << 8)   |
	      (static_cast<uint64_t>(types[childTypeI + 5]));
	  }
	  auto prevChildTypeI = childTypeI;

	  if (argType == ArgumentType::ARGUMENT_TYPE_BOOL) {
            auto valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE; 
	    for (; childTypeI < prevChildTypeI + spanSize; childI++) {
	      int64_t& arg = arguments[childI].asLong;
	      uint64_t tmp = static_cast<uint64_t>(arg);
	      for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize; i--, childTypeI++) {
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
	      for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize; i--, childTypeI++) {
		for(auto j = 0; j < exprDepth + 1; j++) {
		  stream << "  ";
		}
		stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " VALUE: ";
		uint8_t val = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		stream << static_cast<int32_t>(val) << " TYPE: CHAR";
		stream << "\n";
	      }
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_SHORT) {
	    auto valsPerArg = sizeof(Argument) / Argument_SHORT_SIZE; 
	    for (; childTypeI < prevChildTypeI + spanSize; childI++) {
	      int64_t& arg = arguments[childI].asLong;
	      uint64_t tmp = static_cast<uint64_t>(arg);
	      for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize; i--, childTypeI++) {
		for(auto j = 0; j < exprDepth + 1; j++) {
		  stream << "  ";
		}
		stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " VALUE: ";
		uint16_t val = static_cast<uint16_t>((tmp >> (Argument_SHORT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		stream << static_cast<int32_t>(val) << " TYPE: SHORT";
		stream << "\n";
	      }
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_INT) {
	    auto valsPerArg = sizeof(Argument) / Argument_INT_SIZE; 
	    for (; childTypeI < prevChildTypeI + spanSize; childI++) {
	      int64_t& arg = arguments[childI].asLong;
	      uint64_t tmp = static_cast<uint64_t>(arg);
	      for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize; i--, childTypeI++) {
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
	    if (isDictEnc) {
	      if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
		size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		for(; childTypeI < prevChildTypeI + spanSize; childI++) {
		  int64_t& arg = arguments[childI].asLong;
		  uint64_t tmp = static_cast<uint64_t>(arg);
		  for (int64_t i = 0;
		       i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
		       i--, childTypeI++) {
		    for(auto j = 0; j < exprDepth + 1; j++) {
		      stream << "  ";
		    }
		    stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " DICT INDEX (CHAR): ";
		    uint8_t dictOffset = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		    stream << dictI + static_cast<int8_t>(dictOffset) << " VALUE: ";
		    auto const& arg = dicts[(dictI + static_cast<int8_t>(dictOffset))];
		    stream << arg.asLong << " TYPE: LONG\n";
		  }
		}
	      } else if (dictOffsetArgumentSize == Argument_INT_SIZE) {
		size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		for(; childTypeI < prevChildTypeI + spanSize; childI++) {
		  int64_t& arg = arguments[childI].asLong;
		  uint64_t tmp = static_cast<uint64_t>(arg);
		  for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
		       i--, childTypeI++) {
		    for(auto j = 0; j < exprDepth + 1; j++) {
		      stream << "  ";
		    }
		    stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " DICT INDEX (INT): ";
		    uint32_t dictOffset = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		    stream << dictI + static_cast<int32_t>(dictOffset) << " VALUE: ";
		    auto const& arg = dicts[(dictI + static_cast<int32_t>(dictOffset))];
		    stream << arg.asLong << " TYPE: LONG\n";
		  }
		}
	      }
	    } else { 
	      for (; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
		addIndexToStream(stream, expr, childI++, childTypeI, childTypeI - expression.startChildTypeOffset, exprDepth + 1);
	      }
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_FLOAT) {
            auto valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE; 
	    for (; childTypeI < prevChildTypeI + spanSize; childI++) {
	      int64_t& arg = arguments[childI].asLong;
	      uint64_t tmp = static_cast<uint64_t>(arg);
	      for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize; i--, childTypeI++) {
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
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_DOUBLE) {
	    if (isDictEnc) {
	      if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
		size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		for(; childTypeI < prevChildTypeI + spanSize; childI++) {
		  int64_t& arg = arguments[childI].asLong;
		  uint64_t tmp = static_cast<uint64_t>(arg);
		  for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
		       i--, childTypeI++) {
		    for(auto j = 0; j < exprDepth + 1; j++) {
		      stream << "  ";
		    }
		    stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " DICT INDEX (CHAR): ";
		    uint8_t dictOffset = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		    stream << dictI + static_cast<int8_t>(dictOffset) << " VALUE: ";
		    auto const& arg = dicts[(dictI + static_cast<int8_t>(dictOffset))];
		    stream << arg.asDouble << " TYPE: DOUBLE\n";
		  }
		}
	      } else if (dictOffsetArgumentSize == Argument_INT_SIZE) {
		size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		for(; childTypeI < prevChildTypeI + spanSize; childI++) {
		  int64_t& arg = arguments[childI].asLong;
		  uint64_t tmp = static_cast<uint64_t>(arg);
		  for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
		       i--, childTypeI++) {
		    for(auto j = 0; j < exprDepth + 1; j++) {
		      stream << "  ";
		    }
		    stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " DICT INDEX (INT): ";
		    uint32_t dictOffset = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		    stream << dictI + static_cast<int8_t>(dictOffset) << " VALUE: ";
		    auto const& arg = dicts[(dictI + static_cast<int32_t>(dictOffset))];
		    stream << arg.asDouble << " TYPE: DOUBLE\n";
		  }
		}
	      }
	    } else { 
	      for (; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
		addIndexToStream(stream, expr, childI++, childTypeI, childTypeI - expression.startChildTypeOffset, exprDepth + 1);
	      }
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_STRING) {
	    if (isDictEnc) {
	      if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
		size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		for(; childTypeI < prevChildTypeI + spanSize; childI++) {
		  int64_t& arg = arguments[childI].asLong;
		  uint64_t tmp = static_cast<uint64_t>(arg);
		  for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
		       i--, childTypeI++) {
		    for(auto j = 0; j < exprDepth + 1; j++) {
		      stream << "  ";
		    }
		    stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " DICT INDEX (CHAR): ";
		    uint8_t dictOffset = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		    stream << dictI + static_cast<int8_t>(dictOffset) << " VALUE: ";
		    auto const& arg = dicts[(dictI + static_cast<int8_t>(dictOffset))];
		    stream << std::string(viewString(root, arg.asString)) << " TYPE: STRING\n";
		  }
		}
	      } else if (dictOffsetArgumentSize == Argument_INT_SIZE) {
		size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		for(; childTypeI < prevChildTypeI + spanSize; childI++) {
		  int64_t& arg = arguments[childI].asLong;
		  uint64_t tmp = static_cast<uint64_t>(arg);
		  for (int64_t i = 0; i < valsPerArg && childTypeI < prevChildTypeI + spanSize;
		       i--, childTypeI++) {
		    for(auto j = 0; j < exprDepth + 1; j++) {
		      stream << "  ";
		    }
		    stream << "ARG INDEX: " << childI << " TYPE INDEX: " << childTypeI << " SUB-EXPR INDEX: " << childTypeI - expression.startChildTypeOffset << " DICT INDEX (INT): ";
		    uint32_t dictOffset = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
		    stream << dictI + static_cast<int8_t>(dictOffset) << " VALUE: ";
		    auto const& arg = dicts[(dictI + static_cast<int32_t>(dictOffset))];
		    stream << std::string(viewString(root, arg.asString)) << " TYPE: STRING\n";
		  }
		}
	      }
	    } else { 
	      for (; childTypeI < prevChildTypeI + spanSize; childTypeI++) {
		addIndexToStream(stream, expr, childI++, childTypeI, childTypeI - expression.startChildTypeOffset, exprDepth + 1);
	      }
	    }
	  } else if (argType == ArgumentType::ARGUMENT_TYPE_SYMBOL) {
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
      auto const& isDictEnc = (type & ArgumentType_DICT_ENC_BIT) != 0U;

      // std::cout << "TYPE: " << (int64_t)(type & (~ArgumentType_RLE_BIT)) << " isRLE: " << (int64_t)isRLE << std::endl;

      if(isRLE) {

        auto const argType = static_cast<ArgumentType>(type & ArgumentType_MASK);
        uint32_t size =
	  (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 4]) << 24) |
	  (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 3]) << 16) |
	  (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 2]) << 8)  |
	  (static_cast<uint32_t>(flattenedArgumentTypes()[childTypeIndex + 1]));
	uint64_t dictI = 0;
	size_t dictOffsetArgumentSize = 0; 
	if (isDictEnc) {
	  dictOffsetArgumentSize = (type & ArgumentType_DICT_ENC_SIZE_BIT) == 0U ?
	    Argument_CHAR_SIZE :
	    Argument_INT_SIZE;
	  dictI =
	    (static_cast<uint64_t>(flattenedArgumentTypes()[childTypeIndex + 12]) << 56) |
	    (static_cast<uint64_t>(flattenedArgumentTypes()[childTypeIndex + 11]) << 48) |
	    (static_cast<uint64_t>(flattenedArgumentTypes()[childTypeIndex + 10]) << 40) |
	    (static_cast<uint64_t>(flattenedArgumentTypes()[childTypeIndex + 9]) <<  32) |
	    (static_cast<uint64_t>(flattenedArgumentTypes()[childTypeIndex + 8]) << 24)  |
	    (static_cast<uint64_t>(flattenedArgumentTypes()[childTypeIndex + 7]) << 16)  |
	    (static_cast<uint64_t>(flattenedArgumentTypes()[childTypeIndex + 6]) << 8)   |
	    (static_cast<uint64_t>(flattenedArgumentTypes()[childTypeIndex + 5]));
	}
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
		     for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
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
		     for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
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
                {ArgumentType::ARGUMENT_TYPE_SHORT,
                 [&] {
                   std::vector<int16_t> data;
                   data.reserve(size);
		   size_t valsPerArg = sizeof(Argument) / Argument_SHORT_SIZE;
		   for(; childTypeIndex < prevChildTypeIndex + size;) {
                     int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
			  i--, childTypeIndex++) {
		       uint16_t val = static_cast<uint16_t>((tmp >> (Argument_SHORT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
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
		     for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
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
		   // std::cout << "PREV CHILD TYPE INDEX: " << prevChildTypeIndex << " SPAN SIZE: " << size << std::endl;
		   if (isDictEnc) {
		     if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
		       size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		       for(; childTypeIndex < prevChildTypeIndex + size;) {
			 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
			 uint64_t tmp = static_cast<uint64_t>(arg);
			 for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
			      i--, childTypeIndex++) {
			   uint8_t dictOffset = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
			   auto const& arg = spanDictionariesBuffer()[(dictI + static_cast<int8_t>(dictOffset))];
			   data.push_back(arg.asLong);
			 }
		       }
		     } else if (dictOffsetArgumentSize == Argument_INT_SIZE) {
		       size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		       for(; childTypeIndex < prevChildTypeIndex + size;) {
			 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
			 uint64_t tmp = static_cast<uint64_t>(arg);
			 for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
			      i--, childTypeIndex++) {
			   uint32_t dictOffset = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
			   auto const& arg = spanDictionariesBuffer()[(dictI + static_cast<int32_t>(dictOffset))];
			   data.push_back(arg.asLong);
			 }
		       }
		     }
		   } else { 
		     for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++, childArgIndex++) {
		       auto const& arg = flattenedArguments()[childArgIndex];
		       data.push_back(arg.asLong);
		     }
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
		     for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
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
                   if (isDictEnc) {
		     if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
		       size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		       for(; childTypeIndex < prevChildTypeIndex + size;) {
			 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
			 uint64_t tmp = static_cast<uint64_t>(arg);
			 for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
			      i--, childTypeIndex++) {
			   uint8_t dictOffset = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
			   auto const& arg = spanDictionariesBuffer()[(dictI + static_cast<int8_t>(dictOffset))];
			   data.push_back(arg.asDouble);
			 }
		       }
		     } else if (dictOffsetArgumentSize == Argument_INT_SIZE) {
		       size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		       for(; childTypeIndex < prevChildTypeIndex + size;) {
			 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
			 uint64_t tmp = static_cast<uint64_t>(arg);
			 for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
			      i--, childTypeIndex++) {
			   uint32_t dictOffset = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
			   auto const& arg = spanDictionariesBuffer()[(dictI + static_cast<int32_t>(dictOffset))];
			   data.push_back(arg.asDouble);
			 }
		       }
		     }
		   } else {
		     for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++, childArgIndex++) {
		       auto const& arg = flattenedArguments()[childArgIndex];
		       data.push_back(arg.asDouble);
		     }
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
                {ArgumentType::ARGUMENT_TYPE_STRING, [&childArgIndex, &childTypeIndex, &prevChildTypeIndex, &size, &isDictEnc, &dictOffsetArgumentSize, &dictI, this] {
                   std::vector<std::string> data;
                   data.reserve(size);
		   if (isDictEnc) {
		     if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
		       size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		       for(; childTypeIndex < prevChildTypeIndex + size;) {
			 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
			 uint64_t tmp = static_cast<uint64_t>(arg);
			 for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
			      i--, childTypeIndex++) {
			   uint8_t dictOffset = static_cast<uint8_t>((tmp >> (Argument_CHAR_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
			   auto const& arg = spanDictionariesBuffer()[(dictI + static_cast<int8_t>(dictOffset))];
			   data.push_back(std::string(viewString(root, arg.asString)));
			 }
		       }
		     } else if (dictOffsetArgumentSize == Argument_INT_SIZE) {
		       size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		       for(; childTypeIndex < prevChildTypeIndex + size;) {
			 int64_t& arg = flattenedArguments()[childArgIndex++].asLong;
			 uint64_t tmp = static_cast<uint64_t>(arg);
			 for (int64_t i = 0; i < valsPerArg && childTypeIndex < prevChildTypeIndex + size;
			      i--, childTypeIndex++) {
			   uint32_t dictOffset = static_cast<uint32_t>((tmp >> (Argument_INT_SIZE * sizeof(Argument) * i)) & 0xFFFFFFFFUL);
			   auto const& arg = spanDictionariesBuffer()[(dictI + static_cast<int32_t>(dictOffset))];
			   data.push_back(std::string(viewString(root, arg.asString)));
			 }
		       }
		     }
		   } else {
		     for(; childTypeIndex < prevChildTypeIndex + size; childTypeIndex++, childArgIndex++) {
		       auto const& arg = flattenedArguments()[childArgIndex];
		       data.push_back(std::string(viewString(root, arg.asString)));
		     }
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
				      auto valsPerArg = sizeof(arg0) > sizeof(Argument) ?
					1 : sizeof(Argument) / sizeof(arg0);
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
      auto stopTypeIndex = typeIndex < (ArgumentType_RLE_MINIMUM_SIZE - 1) ?
	0 : typeIndex - (ArgumentType_RLE_MINIMUM_SIZE - 1);
      auto testIndex = typeIndex;
      bool isRLE = (buffer.flattenedArgumentTypes()[testIndex] & ArgumentType_RLE_BIT) != 0u;
      while (!isRLE && testIndex >= 0 && testIndex > stopTypeIndex) {
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

    // ALTER TO CHANGE TYPE OFFSET TOO
    LazilyDeserializedExpression operator()(size_t childOffset, size_t childTypeOffset) const {
      auto const& expr = expression();
      // std::cout << "START CHILD OFFSET: " << expr.startChildOffset << std::endl;
      // std::cout << "END CHILD OFFSET: " << expr.endChildOffset << std::endl;
      // std::cout << "START CHILD TYPE OFFSET: " << expr.startChildTypeOffset << std::endl;
      // std::cout << "END CHILD TYPE OFFSET: " << expr.endChildTypeOffset << std::endl;
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

    bool currentIsExpression() const {
      auto const& argumentType = (buffer.flattenedArgumentTypes()[typeIndex] & ArgumentType_MASK);
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
	// std::cout << "Size: " << size << std::endl;
	return size;
      }
      return 0;
    }
    
    std::pair<uint64_t, size_t> currentIsDictionaryEncoded() const {
      auto const& argumentTypes = buffer.flattenedArgumentTypes();
      auto const& type = argumentTypes[typeIndex];
      auto const& isDictEnc = (type & ArgumentType_DICT_ENC_BIT) != 0u;
      if (isDictEnc) {
	size_t dictOffsetArgumentSize = (type & ArgumentType_DICT_ENC_SIZE_BIT) == 0u ?
	  Argument_CHAR_SIZE :
	  Argument_INT_SIZE;
	uint64_t dictI =
	  (static_cast<uint64_t>(argumentTypes[typeIndex + 12]) << 56) |
	  (static_cast<uint64_t>(argumentTypes[typeIndex + 11]) << 48) |
	  (static_cast<uint64_t>(argumentTypes[typeIndex + 10]) << 40) |
	  (static_cast<uint64_t>(argumentTypes[typeIndex + 9]) <<  32) |
	  (static_cast<uint64_t>(argumentTypes[typeIndex + 8]) << 24)  |
	  (static_cast<uint64_t>(argumentTypes[typeIndex + 7]) << 16)  |
	  (static_cast<uint64_t>(argumentTypes[typeIndex + 6]) << 8)   |
	  (static_cast<uint64_t>(argumentTypes[typeIndex + 5]));
        return {dictI, dictOffsetArgumentSize};
      }
      return {0, 0};
    }

    boss::Symbol getCurrentExpressionHead() const {
      auto const& expr = expression();
      return boss::Symbol(viewString(buffer.root, expr.symbolNameOffset));
    }
    
    template<typename IntT>
    static inline IntT extractField(uint64_t tmp, size_t shiftAmt) {
      return static_cast<IntT>((tmp >> shiftAmt) & 0xFFFFFFFFUL);
    }

    template<typename T, typename S, typename U>
    boss::expressions::ExpressionSpanArguments getCurrentExpressionAsSpanAsTypeWithIndices(const std::vector<U>& indices, int64_t spanSize) const {
      static_assert(std::is_convertible<T, S>::value, "Cannot convert stored type to requested span type in getCurrentExpressionAsSpanAsTypeWithIndices. Change from <T, S, U> as T cannot be converted to S.");
      constexpr size_t THREADING_THRESHOLD = 256000;
      auto const& arguments = buffer.flattenedArguments();
      auto const& expr = expression();
      auto const& startChildOffset = expr.startChildOffset;
      
      const size_t n = indices.size();
      constexpr size_t valsPerArg = sizeof(T) > sizeof(Argument) ? 1 : sizeof(Argument) / sizeof(T);
      constexpr size_t shiftAmt = sizeof(T) > sizeof(Argument) ?
	sizeof(Argument) * sizeof(Argument) :
	sizeof(Argument) * sizeof(T);

      constexpr size_t valsPerArgMask = valsPerArg - 1;
      constexpr size_t valsPerArgShift = []{
	size_t s = 0;
	size_t v = valsPerArg;
	while ((v >>= 1) > 0) ++s;
	return s;
      }();

      if (spanSize <= 0) {
	spanSize = n;
      }

      boss::expressions::ExpressionSpanArguments res;
      res.reserve((n / spanSize) + 1);

      for (size_t spanI = 0; spanI < n; spanI += spanSize) {
	size_t currSize = spanSize < (n - spanI) ? spanSize : (n - spanI);
	if constexpr(std::is_same_v<T, boss::Symbol>) {
	  std::vector<S> data;
	  data.reserve(currSize);
	  for (size_t i = 0; i < currSize; i++) {
	    const auto& index = indices[i + spanI];
	    size_t childOffset = index >> valsPerArgShift;
	    int64_t inArgI = (index & valsPerArgMask);
	  
	    auto& arg = arguments[startChildOffset + childOffset];
	    uint64_t tmp = static_cast<uint64_t>(arg.asLong);
	    data.emplace_back(static_cast<S>(viewString(buffer.root, arg.asString)));
	  }
	  res.push_back(std::move(boss::expressions::Span<S>(std::move(data))));
	} else {
	  std::vector<S> data(currSize);

	  if (n > THREADING_THRESHOLD && currSize > THREADING_THRESHOLD) {
	    #pragma omp parallel for schedule(static) num_threads(20)
	    for (size_t i = 0; i < currSize; i++) {
	      const auto& index = indices[i + spanI];
	      size_t childOffset = index >> valsPerArgShift;
	      int64_t inArgI = (index & valsPerArgMask);
	  
	      auto& arg = arguments[startChildOffset + childOffset];
	      uint64_t tmp = static_cast<uint64_t>(arg.asLong);

	      if constexpr(std::is_same_v<T, bool>) {
		data[i] = static_cast<S>(static_cast<bool>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
	      } else if constexpr(std::is_same_v<T, int8_t>) {
		data[i] = static_cast<S>(static_cast<int8_t>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
	      } else if constexpr (std::is_same_v<T, int16_t>) {
		data[i] = static_cast<S>(static_cast<int16_t>(extractField<uint16_t>(tmp, shiftAmt * inArgI)));
	      } else if constexpr (std::is_same_v<T, int32_t>) {
		data[i] = static_cast<S>(static_cast<int32_t>(extractField<uint32_t>(tmp, shiftAmt * inArgI)));
	      } else if constexpr (std::is_same_v<T, float_t>) {
		uint32_t val = extractField<uint32_t>(tmp, shiftAmt * inArgI);
		union { uint32_t iVal; float f; } u;
		u.iVal = val;
		data[i] = static_cast<S>(u.f);
	      } else if constexpr (std::is_same_v<T, int64_t>) {
		data[i] = static_cast<S>(arg.asLong);
	      } else if constexpr (std::is_same_v<T, double_t>) {
		data[i] = static_cast<S>(arg.asDouble);
	      } else if constexpr (std::is_same_v<T, std::string>) {
		data[i] = static_cast<S>(std::string(viewString(buffer.root, arg.asString)));
	      } else {
		static_assert(sizeof(T) == 0, "Unsupported type in getCurrentExpressionAsSpanAsTypeWithIndices<T, S, U>()");
	      }
	    }
	  } else {
	    for (size_t i = 0; i < currSize; i++) {
	      const auto& index = indices[i + spanI];
	      size_t childOffset = index >> valsPerArgShift;
	      int64_t inArgI = (index & valsPerArgMask);
	  
	      auto& arg = arguments[startChildOffset + childOffset];
	      uint64_t tmp = static_cast<uint64_t>(arg.asLong);

	      if constexpr(std::is_same_v<T, bool>) {
		data[i] = static_cast<S>(static_cast<bool>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
	      } else if constexpr(std::is_same_v<T, int8_t>) {
		data[i] = static_cast<S>(static_cast<int8_t>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
	      } else if constexpr (std::is_same_v<T, int16_t>) {
		data[i] = static_cast<S>(static_cast<int16_t>(extractField<uint16_t>(tmp, shiftAmt * inArgI)));
	      } else if constexpr (std::is_same_v<T, int32_t>) {
		data[i] = static_cast<S>(static_cast<int32_t>(extractField<uint32_t>(tmp, shiftAmt * inArgI)));
	      } else if constexpr (std::is_same_v<T, float_t>) {
		uint32_t val = extractField<uint32_t>(tmp, shiftAmt * inArgI);
		union { uint32_t iVal; float f; } u;
		u.iVal = val;
		data[i] = static_cast<S>(u.f);
	      } else if constexpr (std::is_same_v<T, int64_t>) {
		data[i] = static_cast<S>(arg.asLong);
	      } else if constexpr (std::is_same_v<T, double_t>) {
		data[i] = static_cast<S>(arg.asDouble);
	      } else if constexpr (std::is_same_v<T, std::string>) {
		data[i] = static_cast<S>(std::string(viewString(buffer.root, arg.asString)));
	      } else {
		static_assert(sizeof(T) == 0, "Unsupported type in getCurrentExpressionAsSpanAsTypeWithIndices<T, S, U>()");
	      }
	    }
	  }
	  res.push_back(std::move(boss::expressions::Span<S>(std::move(data))));
	}
      }
      return res;
    }
    
    template<typename T, typename U>
    boss::expressions::ExpressionSpanArguments getCurrentExpressionAsSpanWithIndices(const std::vector<U>& indices, int64_t spanSize) const {
      return getCurrentExpressionAsSpanAsTypeWithIndices<T, T, U>(indices, spanSize);
    }

    template<typename T>
    boss::expressions::ExpressionSpanArguments getCurrentExpressionAsSpanWithIndices(ArgumentType type, const std::vector<T>& indices, int64_t spanSize) const {
      switch(type) {
      case ArgumentType::ARGUMENT_TYPE_BOOL:
	return getCurrentExpressionAsSpanWithIndices<bool, T>(indices, spanSize);
      case ArgumentType::ARGUMENT_TYPE_CHAR:
	return getCurrentExpressionAsSpanWithIndices<int8_t, T>(indices, spanSize);
      case ArgumentType::ARGUMENT_TYPE_SHORT:
	return getCurrentExpressionAsSpanWithIndices<int16_t, T>(indices, spanSize);
      case ArgumentType::ARGUMENT_TYPE_INT:
	return getCurrentExpressionAsSpanWithIndices<int32_t, T>(indices, spanSize);
      case ArgumentType::ARGUMENT_TYPE_LONG:
	return getCurrentExpressionAsSpanWithIndices<int64_t, T>(indices, spanSize);
      case ArgumentType::ARGUMENT_TYPE_FLOAT:
	return getCurrentExpressionAsSpanWithIndices<float_t, T>(indices, spanSize);
      case ArgumentType::ARGUMENT_TYPE_DOUBLE:
	return getCurrentExpressionAsSpanWithIndices<double_t, T>(indices, spanSize);
      case ArgumentType::ARGUMENT_TYPE_STRING:
	return getCurrentExpressionAsSpanWithIndices<std::string, T>(indices, spanSize);
      case ArgumentType::ARGUMENT_TYPE_SYMBOL:
	return getCurrentExpressionAsSpanWithIndices<boss::Symbol, T>(indices, spanSize);
      case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
	break;
      }
      throw std::runtime_error("Invalid type in getCurrentExpressionAsSpanWithIndices");
    }

    template<typename T, typename S, typename U>
    boss::Span<S> getCurrentExpressionAsSpanAsTypeWithIndices(const std::vector<U>& indices) const {
      static_assert(std::is_convertible<T, S>::value, "Cannot convert stored type to requested span type in getCurrentExpressionAsSpanAsTypeWithIndices. Change from <T, S, U> as T cannot be converted to S.");
      auto const& arguments = buffer.flattenedArguments();
      auto const& expr = expression();
      auto const& startChildOffset = expr.startChildOffset;
      
      const size_t n = indices.size();
      constexpr size_t valsPerArg = sizeof(T) > sizeof(Argument) ? 1 : sizeof(Argument) / sizeof(T);
      constexpr size_t shiftAmt = sizeof(T) > sizeof(Argument) ?
	sizeof(Argument) * sizeof(Argument) :
	sizeof(Argument) * sizeof(T);

      constexpr size_t valsPerArgMask = valsPerArg - 1;
      constexpr size_t valsPerArgShift = []{
	size_t s = 0;
	size_t v = valsPerArg;
	while ((v >>= 1) > 0) ++s;
	return s;
      }();

      if constexpr(std::is_same_v<T, boss::Symbol>) {
	std::vector<S> data;
	data.reserve(n);
	for (size_t i = 0; i < n; i++) {
	  const auto& index = indices[i];
	  size_t childOffset = index >> valsPerArgShift;
	  int64_t inArgI = (index & valsPerArgMask);
	  
	  auto& arg = arguments[startChildOffset + childOffset];
	  uint64_t tmp = static_cast<uint64_t>(arg.asLong);
	  data.emplace_back(static_cast<S>(viewString(buffer.root, arg.asString)));
	}
	return boss::expressions::Span<S>(std::move(data));
      } else {
	std::vector<S> data(n);

	for (size_t i = 0; i < n; i++) {
	  const auto& index = indices[i];
	  size_t childOffset = index >> valsPerArgShift;
	  int64_t inArgI = (index & valsPerArgMask);
	  
	  auto& arg = arguments[startChildOffset + childOffset];
	  uint64_t tmp = static_cast<uint64_t>(arg.asLong);

	  if constexpr(std::is_same_v<T, bool>) {
	    data[i] = static_cast<S>(static_cast<bool>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
	  } else if constexpr(std::is_same_v<T, int8_t>) {
	    data[i] = static_cast<S>(static_cast<int8_t>(extractField<uint8_t>(tmp, shiftAmt * inArgI)));
	  } else if constexpr (std::is_same_v<T, int16_t>) {
	    data[i] = static_cast<S>(static_cast<int16_t>(extractField<uint16_t>(tmp, shiftAmt * inArgI)));
	  } else if constexpr (std::is_same_v<T, int32_t>) {
	    data[i] = static_cast<S>(static_cast<int32_t>(extractField<uint32_t>(tmp, shiftAmt * inArgI)));
	  } else if constexpr (std::is_same_v<T, float_t>) {
	    uint32_t val = extractField<uint32_t>(tmp, shiftAmt * inArgI);
	    union { uint32_t i; float f; } u;
	    u.i = val;
	    data[i] = static_cast<S>(u.f);
	  } else if constexpr (std::is_same_v<T, int64_t>) {
	    data[i] = static_cast<S>(arg.asLong);
	  } else if constexpr (std::is_same_v<T, double_t>) {
	    data[i] = static_cast<S>(arg.asDouble);
	  } else if constexpr (std::is_same_v<T, std::string>) {
	    data[i] = static_cast<S>(std::string(viewString(buffer.root, arg.asString)));
	  } else {
	    static_assert(sizeof(T) == 0, "Unsupported type in getCurrentExpressionAsSpanAsTypeWithIndices<T, S, U>()");
	  }
	}
	return boss::expressions::Span<S>(std::move(data));
      }
    }

    template<typename T, typename U>
    boss::Span<T> getCurrentExpressionAsSpanWithIndices(const std::vector<U>& indices) const {
      return getCurrentExpressionAsSpanAsTypeWithIndices<T, T, U>(indices);
    }

    template<typename T>
    boss::expressions::ExpressionSpanArgument getCurrentExpressionAsSpanWithIndices(ArgumentType type, const std::vector<T>& indices) const {
      switch(type) {
      case ArgumentType::ARGUMENT_TYPE_BOOL:
	return getCurrentExpressionAsSpanWithIndices<bool, T>(indices);
      case ArgumentType::ARGUMENT_TYPE_CHAR:
	return getCurrentExpressionAsSpanWithIndices<int8_t, T>(indices);
      case ArgumentType::ARGUMENT_TYPE_SHORT:
	return getCurrentExpressionAsSpanWithIndices<int16_t, T>(indices);
      case ArgumentType::ARGUMENT_TYPE_INT:
	return getCurrentExpressionAsSpanWithIndices<int32_t, T>(indices);
      case ArgumentType::ARGUMENT_TYPE_LONG:
	return getCurrentExpressionAsSpanWithIndices<int64_t, T>(indices);
      case ArgumentType::ARGUMENT_TYPE_FLOAT:
	return getCurrentExpressionAsSpanWithIndices<float_t, T>(indices);
      case ArgumentType::ARGUMENT_TYPE_DOUBLE:
	return getCurrentExpressionAsSpanWithIndices<double_t, T>(indices);
      case ArgumentType::ARGUMENT_TYPE_STRING:
	return getCurrentExpressionAsSpanWithIndices<std::string, T>(indices);
      case ArgumentType::ARGUMENT_TYPE_SYMBOL:
	return getCurrentExpressionAsSpanWithIndices<boss::Symbol, T>(indices);
      case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
	break;
      }
      throw std::runtime_error("Invalid type in getCurrentExpressionAsSpanWithIndices");
    }

    boss::expressions::ExpressionSpanArguments getCurrentExpressionAsSpanWithTypeAndSize(ArgumentType type, size_t size, int64_t spanSize) const {
      auto const& arguments = buffer.flattenedArguments();
      if (spanSize <= 0) {
	spanSize = size;
      }
      boss::expressions::ExpressionSpanArguments res;
      res.reserve((size / spanSize) + 1);
      auto const spanFunctors =
          std::unordered_map<ArgumentType,
	std::function<void()>>{
              {ArgumentType::ARGUMENT_TYPE_BOOL,
               [&] {
		 constexpr size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
		 constexpr size_t shiftAmt = sizeof(Argument) * Argument_BOOL_SIZE;

		 size_t tempI = 0;
		 for (size_t spanI = 0; spanI < size; spanI += spanSize) {
		   size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
		   std::vector<bool> data(currSize);
		   for (size_t i = 0; i < currSize && tempI < size; tempI++) {
		     int64_t& arg = arguments[argumentIndex + tempI].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t j = 0;
			  j < valsPerArg && i < currSize;
			  j--, i++) {
		       uint8_t val = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		       data[i] = static_cast<bool>(val);
		     }
		   }
		   res.push_back(std::move(boss::expressions::Span<bool>(std::move(data))));
		 }
               }},
              {ArgumentType::ARGUMENT_TYPE_CHAR,
               [&] {
		 for (size_t spanI = 0; spanI < size; spanI += spanSize) {
		   size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
		   auto base64 = &arguments[argumentIndex + spanI];
		   auto base = reinterpret_cast<int8_t*>(base64);
		   res.push_back(std::move(boss::expressions::Span<int8_t>(base, currSize, nullptr)));
		 }
               }},
              {ArgumentType::ARGUMENT_TYPE_SHORT,
               [&] {
		 for (size_t spanI = 0; spanI < size; spanI += spanSize) {
		   size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
		   auto base64 = &arguments[argumentIndex + spanI];
		   auto base = reinterpret_cast<int16_t*>(base64);
		   res.push_back(std::move(boss::expressions::Span<int16_t>(base, currSize, nullptr)));
		 }
               }},
              {ArgumentType::ARGUMENT_TYPE_INT,
               [&] {
		 for (size_t spanI = 0; spanI < size; spanI += spanSize) {
		   size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
		   auto base64 = &arguments[argumentIndex + spanI];
		   auto base = reinterpret_cast<int32_t*>(base64);
		   res.push_back(std::move(boss::expressions::Span<int32_t>(base, currSize, nullptr)));
		 }
               }},
              {ArgumentType::ARGUMENT_TYPE_LONG,
               [&] {
		 for (size_t spanI = 0; spanI < size; spanI += spanSize) {
		   size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
		   auto base64 = &arguments[argumentIndex + spanI];
		   auto base = reinterpret_cast<int64_t*>(base64);
		   res.push_back(std::move(boss::expressions::Span<int64_t>(base, currSize, nullptr)));
		 }
               }},
              {ArgumentType::ARGUMENT_TYPE_FLOAT,
               [&] {
		 for (size_t spanI = 0; spanI < size; spanI += spanSize) {
		   size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
		   auto base64 = &arguments[argumentIndex + spanI];
		   auto base = reinterpret_cast<float_t*>(base64);
		   res.push_back(std::move(boss::expressions::Span<float_t>(base, currSize, nullptr)));
		 }
               }},
              {ArgumentType::ARGUMENT_TYPE_DOUBLE,
               [&] {
		 for (size_t spanI = 0; spanI < size; spanI += spanSize) {
		   size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
		   auto base64 = &arguments[argumentIndex + spanI];
		   auto base = reinterpret_cast<double_t*>(base64);
		   res.push_back(std::move(boss::expressions::Span<double_t>(base, currSize, nullptr)));
		 }
               }},
              {ArgumentType::ARGUMENT_TYPE_STRING,
               [&] {
		 size_t tempI = 0;
		 for (size_t spanI = 0; spanI < size; spanI += spanSize) {
		   size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
		   std::vector<std::string> data(currSize);
		   for(size_t i = 0; i < currSize && tempI < size; i++, tempI++) {
		     auto const& arg = arguments[argumentIndex + tempI];
		     data[i] = std::string(viewString(buffer.root, arg.asString));
		   }
		   res.push_back(std::move(boss::expressions::Span<std::string>(std::move(data))));
		 }
               }},
              {ArgumentType::ARGUMENT_TYPE_SYMBOL, [&] {
	        size_t tempI = 0;
		for (size_t spanI = 0; spanI < size; spanI += spanSize) {
		  size_t currSize = spanSize < (size - spanI) ? spanSize : (size - spanI);
		  std::vector<boss::Symbol> data;
		  data.reserve(currSize);
		  for(size_t i = 0; i < currSize && tempI < size; i++, tempI++) {
		    auto const& arg = arguments[argumentIndex + tempI];
		    data.emplace_back(viewString(buffer.root, arg.asString));
		  }
		  res.push_back(std::move(boss::expressions::Span<boss::Symbol>(std::move(data))));
		}
	      }}};
      spanFunctors.at(type)();
      return res;
    }
    
    boss::expressions::ExpressionSpanArgument getCurrentExpressionAsSpanWithTypeAndSize(ArgumentType type, size_t size) const {
      auto const& arguments = buffer.flattenedArguments();
      auto const spanFunctors =
          std::unordered_map<ArgumentType,
                             std::function<boss::expressions::ExpressionSpanArgument()>>{
              {ArgumentType::ARGUMENT_TYPE_BOOL,
               [&] {
                 std::vector<bool> data(size);
		 constexpr size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
		 constexpr size_t shiftAmt = sizeof(Argument) * Argument_BOOL_SIZE;
		 auto tempI = 0;
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI].asLong;
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = 0;
			j < valsPerArg && i < size;
			j--, i++) {
		     uint8_t val = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		     data[i] = static_cast<bool>(val);
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
		 auto base64 = &arguments[argumentIndex];
		 auto base = reinterpret_cast<int8_t*>(base64);
		 return boss::expressions::Span<int8_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_SHORT,
               [&] {
		 auto base64 = &arguments[argumentIndex];
		 auto base = reinterpret_cast<int16_t*>(base64);
		 return boss::expressions::Span<int16_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_INT,
               [&] {
		 auto base64 = &arguments[argumentIndex];
		 auto base = reinterpret_cast<int32_t*>(base64);
		 return boss::expressions::Span<int32_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_LONG,
               [&] {
		 auto base64 = &arguments[argumentIndex];
		 auto base = reinterpret_cast<int64_t*>(base64);
		 return boss::expressions::Span<int64_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_FLOAT,
               [&] {
		 auto base64 = &arguments[argumentIndex];
		 auto base = reinterpret_cast<float_t*>(base64);
		 return boss::expressions::Span<float_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_DOUBLE,
               [&] {
		 auto base64 = &arguments[argumentIndex];
		 auto base = reinterpret_cast<double_t*>(base64);
		 return boss::expressions::Span<double_t>(base, size, nullptr);
               }},
              {ArgumentType::ARGUMENT_TYPE_STRING,
               [&] {
                 std::vector<std::string> data(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data[i] = std::string(viewString(buffer.root, arg.asString));
                 }
                 return boss::expressions::Span<std::string>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_SYMBOL, [&] {
		std::vector<boss::Symbol> data;
		data.reserve(size);
		for(size_t i = 0; i < size; i++) {
		  auto const& arg = arguments[argumentIndex + i];
		  data.emplace_back(viewString(buffer.root, arg.asString));
		}
		return boss::expressions::Span<boss::Symbol>(std::move(data));
               }}};
      return spanFunctors.at(type)();
    }

    boss::expressions::ExpressionSpanArgument getCurrentExpressionAsSpanWithTypeAndSizeWithCopy(ArgumentType type, size_t size) const {
      auto const& arguments = buffer.flattenedArguments();
      auto const spanFunctors =
          std::unordered_map<ArgumentType,
                             std::function<boss::expressions::ExpressionSpanArgument()>>{
              {ArgumentType::ARGUMENT_TYPE_BOOL,
               [&] {
                 std::vector<bool> data(size);
		 constexpr size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
		 constexpr size_t shiftAmt = sizeof(Argument) * Argument_BOOL_SIZE;
		 auto tempI = 0;
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI].asLong;
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = 0;
			j < valsPerArg && i < size;
			j--, i++) {
		     uint8_t val = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		     data[i] = static_cast<bool>(val);
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
		 std::vector<int8_t> data(size);
                 constexpr size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		 constexpr size_t shiftAmt = sizeof(Argument) * Argument_CHAR_SIZE;
		 auto tempI = 0;
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI].asLong;
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = 0;
			j < valsPerArg && i < size;
			j--, i++) {
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
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI].asLong;
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = 0;
			j < valsPerArg && i < size;
			j--, i++) {
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
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI].asLong;
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = 0;
			j < valsPerArg && i < size;
			j--, i++) {
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
                   auto const& arg = arguments[argumentIndex + i];
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
		 for (size_t i = 0; i < size; tempI++) {
		   int64_t& arg = arguments[argumentIndex + tempI].asLong;
		   uint64_t tmp = static_cast<uint64_t>(arg);
		   for (int64_t j = 0;
			j < valsPerArg && i < size;
			j--, i++) {
		     uint32_t val = static_cast<uint32_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		     union { uint32_t i; float f; } u;
		     u.i = val;
		     // float realVal;
		     // std::memcpy(&realVal, &val, sizeof(realVal));
		     data[i] = u.f;
		   }
		 }
                 // for(size_t i = 0; i < size; i++) {
                 //   auto const& arg = arguments[argumentIndex + i];
                 //   data[i] = arg.asFloat);
                 // }
                 return boss::expressions::Span<float_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_DOUBLE,
               [&] {
		 std::vector<double_t> data(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data[i] = arg.asDouble;
                 }
                 return boss::expressions::Span<double_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_STRING,
               [&] {
                 std::vector<std::string> data(size);
                 for(size_t i = 0; i < size; i++) {
                   auto const& arg = arguments[argumentIndex + i];
                   data[i] = std::string(viewString(buffer.root, arg.asString));
                 }
                 return boss::expressions::Span<std::string>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_SYMBOL, [&] {
		std::vector<boss::Symbol> data;
		data.reserve(size);
		for(size_t i = 0; i < size; i++) {
		  auto const& arg = arguments[argumentIndex + i];
		  data.emplace_back(viewString(buffer.root, arg.asString));
		}
		return boss::expressions::Span<boss::Symbol>(std::move(data));
               }}};
      return spanFunctors.at(type)();
    }

    boss::expressions::ExpressionSpanArgument getCurrentExpressionAsDictEncodedSpanWithTypeAndSize(ArgumentType type, size_t size, uint64_t dictI, size_t dictOffsetArgumentSize) const {
      auto const& arguments = buffer.flattenedArguments();
      auto const& dicts = buffer.spanDictionariesBuffer();
      auto const spanFunctors =
          std::unordered_map<ArgumentType,
                             std::function<boss::expressions::ExpressionSpanArgument()>>{
              {ArgumentType::ARGUMENT_TYPE_LONG,
               [&] {
                 std::vector<int64_t> data(size);
                 if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
		   constexpr size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		   constexpr size_t shiftAmt = sizeof(Argument) * Argument_CHAR_SIZE;
		   auto tempI = 0;
		   for(size_t i = 0; i < size; tempI++) {
		     int64_t& arg = arguments[argumentIndex + tempI].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t j = 0;
			  j < valsPerArg && i < size;
			  j--, i++) {
		       uint8_t dictOffset = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		       auto const& arg = dicts[(dictI + static_cast<int8_t>(dictOffset))];
		       data[i] = arg.asLong;
		     }
		   }
		 } else if (dictOffsetArgumentSize == Argument_INT_SIZE) {
		   constexpr size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		   constexpr size_t shiftAmt = sizeof(Argument) * Argument_INT_SIZE;
		   auto tempI = 0;
		   for(size_t i = 0; i < size; tempI++) {
		     int64_t& arg = arguments[argumentIndex + tempI].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t j = 0;
			  j < valsPerArg && i < size;
			  j--, i++) {
		       uint32_t dictOffset = static_cast<uint32_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		       auto const& arg = dicts[(dictI + static_cast<int32_t>(dictOffset))];
		       data[i] = arg.asLong;
		     }
		   }
		 }
                 return boss::expressions::Span<int64_t>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_DOUBLE,
               [&] {
                 std::vector<double> data(size);
                 if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
		   constexpr size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		   constexpr size_t shiftAmt = sizeof(Argument) * Argument_CHAR_SIZE;
		   auto tempI = 0;
		   for(size_t i = 0; i < size; tempI++) {
		     int64_t& arg = arguments[argumentIndex + tempI].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t j = 0;
			  j < valsPerArg && i < size;
			  j--, i++) {
		       uint8_t dictOffset = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		       auto const& arg = dicts[(dictI + static_cast<int8_t>(dictOffset))];
		       data[i] = arg.asDouble;
		     }
		   }
		 } else if (dictOffsetArgumentSize == Argument_INT_SIZE) {
		   constexpr size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		   constexpr size_t shiftAmt = sizeof(Argument) * Argument_INT_SIZE;
		   auto tempI = 0;
		   for(size_t i = 0; i < size; tempI++) {
		     int64_t& arg = arguments[argumentIndex + tempI].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t j = 0;
			  j < valsPerArg && i < size;
			  j--, i++) {
		       uint32_t dictOffset = static_cast<uint32_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		       auto const& arg = dicts[(dictI + static_cast<int32_t>(dictOffset))];
		       data[i] = arg.asDouble;
		     }
		   }
		 }
                 return boss::expressions::Span<double>(std::move(data));
               }},
              {ArgumentType::ARGUMENT_TYPE_STRING,
               [&] {
                 std::vector<std::string> data(size);
                 if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
		   constexpr size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
		   constexpr size_t shiftAmt = sizeof(Argument) * Argument_CHAR_SIZE;
		   auto tempI = 0;
		   for(size_t i = 0; i < size; tempI++) {
		     int64_t& arg = arguments[argumentIndex + tempI].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t j = 0;
			  j < valsPerArg && i < size;
			  j--, i++) {
		       uint8_t dictOffset = static_cast<uint8_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		       auto const& arg = dicts[(dictI + static_cast<int8_t>(dictOffset))];
		       data[i] = std::string(viewString(buffer.root, arg.asString));
		     }
		   }
		 } else if (dictOffsetArgumentSize == Argument_INT_SIZE) {
		   constexpr size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
		   constexpr size_t shiftAmt = sizeof(Argument) * Argument_INT_SIZE;
		   auto tempI = 0;
		   for(size_t i = 0; i < size; tempI++) {
		     int64_t& arg = arguments[argumentIndex + tempI].asLong;
		     uint64_t tmp = static_cast<uint64_t>(arg);
		     for (int64_t j = 0;
			  j < valsPerArg && i < size;
			  j--, i++) {
		       uint32_t dictOffset = static_cast<uint32_t>((tmp >> (shiftAmt * j)) & 0xFFFFFFFFUL);
		       auto const& arg = dicts[(dictI + static_cast<int32_t>(dictOffset))];
		       data[i] = std::string(viewString(buffer.root, arg.asString));
		     }
		   }
		 }
                 return boss::expressions::Span<std::string>(std::move(data));
               }}};
      return spanFunctors.at(type)();
    }
    
    boss::expressions::ExpressionSpanArgument getCurrentExpressionAsSpan() const {
      size_t size = currentIsRLE();
      assert(size != 0);
      auto [dictI, dictOffsetArgSize] = currentIsDictionaryEncoded();
      auto const& type = getCurrentExpressionType();
      if (dictOffsetArgSize == Argument_CHAR_SIZE || dictOffsetArgSize == Argument_INT_SIZE) {
	return std::move(getCurrentExpressionAsDictEncodedSpanWithTypeAndSize(type, size, dictI, dictOffsetArgSize));
      }
      return std::move(getCurrentExpressionAsSpanWithTypeAndSize(type, size));
    }
    
    boss::expressions::ExpressionSpanArgument getCurrentExpressionAsSpanWithCopy() const {
      size_t size = currentIsRLE();
      assert(size != 0);
      auto [dictI, dictOffsetArgSize] = currentIsDictionaryEncoded();
      auto const& type = getCurrentExpressionType();
      if (dictOffsetArgSize == Argument_CHAR_SIZE || dictOffsetArgSize == Argument_INT_SIZE) {
	return std::move(getCurrentExpressionAsDictEncodedSpanWithTypeAndSize(type, size, dictI, dictOffsetArgSize));
      }
      return std::move(getCurrentExpressionAsSpanWithTypeAndSizeWithCopy(type, size));
    }

    template<typename T>
    inline T getCurrentExpressionInSpanAtAs(size_t spanArgI) const {
      auto& argument = buffer.flattenedArguments()[argumentIndex];
      uint64_t tmp = static_cast<uint64_t>(argument.asLong);

      if constexpr (std::is_same_v<T, bool>) {
	constexpr size_t valsPerArg = sizeof(Argument) / Argument_BOOL_SIZE;
	constexpr size_t shiftAmt = sizeof(Argument) * Argument_BOOL_SIZE;
	int64_t inArgI = (spanArgI % valsPerArg);
	return static_cast<bool>(extractField<uint8_t>(tmp, shiftAmt * inArgI));
        // uint32_t val = static_cast<uint8_t>((shiftAmt * inArgI) & 0xFFFFFFFFUL);
	// return static_cast<bool>(val);
      } else if constexpr (std::is_same_v<T, int8_t>) {
	constexpr size_t valsPerArg = sizeof(Argument) / Argument_CHAR_SIZE;
	constexpr size_t shiftAmt = sizeof(Argument) * Argument_CHAR_SIZE;
	int64_t inArgI = (spanArgI % valsPerArg);
	return static_cast<int8_t>(extractField<uint8_t>(tmp, shiftAmt * inArgI));
      } else if constexpr (std::is_same_v<T, int16_t>) {
	constexpr size_t valsPerArg = sizeof(Argument) / Argument_SHORT_SIZE;
	constexpr size_t shiftAmt = sizeof(Argument) * Argument_SHORT_SIZE;
	int64_t inArgI = (spanArgI % valsPerArg);
	return static_cast<int16_t>(extractField<uint16_t>(tmp, shiftAmt * inArgI));
      } else if constexpr (std::is_same_v<T, int32_t>) {
	constexpr size_t valsPerArg = sizeof(Argument) / Argument_INT_SIZE;
	constexpr size_t shiftAmt = sizeof(Argument) * Argument_INT_SIZE;
	int64_t inArgI = (spanArgI % valsPerArg);
	return static_cast<int32_t>(extractField<uint32_t>(tmp, shiftAmt * inArgI));
      } else if constexpr (std::is_same_v<T, int64_t>) {
	return argument.asLong;
      } else if constexpr (std::is_same_v<T, float_t>) {
	constexpr size_t valsPerArg = sizeof(Argument) / Argument_FLOAT_SIZE;
	constexpr size_t shiftAmt = sizeof(Argument) * Argument_FLOAT_SIZE;
	int64_t inArgI = (spanArgI % valsPerArg);
	uint32_t val = extractField<uint32_t>(tmp, shiftAmt * inArgI);
	union { int32_t i; float f; } u;
	u.i = val;
	return u.f;
      } else if constexpr (std::is_same_v<T, double_t>) {
        return argument.asDouble;
      } else if constexpr (std::is_same_v<T, std::string>) {
        return viewString(buffer.root, argument.asString);
      } else if constexpr (std::is_same_v<T, boss::Symbol>) {
        return boss::Symbol(viewString(buffer.root, argument.asString));
      } else {
	static_assert(sizeof(T) == 0, "Unsupported type passes to getCurrentExpressionInSpanAtAs<T>()");
      }
    }

    inline boss::Expression getCurrentExpressionInSpanAtAs(size_t spanArgI, ArgumentType argumentType) const {
      // std::cout << "ARGI: " << argumentIndex << " TYPEI: " << typeIndex << " ARG TYPE: " << static_cast<int32_t>(argumentType) << std::endl;
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
      return "ErrorDeserialisingExpressionInSpan"_("ArgumentIndex"_(static_cast<int64_t>(argumentIndex)),
					     "TypeIndex"_(static_cast<int64_t>(typeIndex)),
					     "InSpanIndex"_(static_cast<int64_t>(spanArgI)),
					     "ArgumentType"_(static_cast<int64_t>(argumentType)));
    }

    template<typename T>
    T getCurrentExpressionInDictEncodedSpanAtAs(size_t spanArgI, uint64_t dictI, size_t dictOffsetArgumentSize) const {
      auto& argument = buffer.flattenedArguments()[argumentIndex];
      uint64_t tmp = static_cast<uint64_t>(argument.asLong);
      size_t valsPerArg = sizeof(Argument) / dictOffsetArgumentSize;
      int64_t inArgI = (spanArgI % valsPerArg);

      int32_t dictOffset;
      if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
	uint8_t val = static_cast<uint8_t>((tmp >> (dictOffsetArgumentSize * sizeof(Argument) * inArgI)) & 0xFFFFFFFFUL);
	dictOffset = static_cast<int32_t>(static_cast<int8_t>(val));
      } else if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
	uint32_t val = static_cast<uint32_t>((tmp >> (dictOffsetArgumentSize * sizeof(Argument) * inArgI)) & 0xFFFFFFFFUL);
	dictOffset = static_cast<int32_t>(val);
      }
      auto& dictArg = buffer.spanDictionariesBuffer()[(dictI + dictOffset)];
      if constexpr (std::is_same_v<T, int64_t>) {
	return dictArg.asLong;
      } else if constexpr (std::is_same_v<T, double_t>) {
	return dictArg.asDouble;
      } else if constexpr (std::is_same_v<T, std::string>) {
	return viewString(buffer.root, dictArg.asString);
      } else {
	static_assert(sizeof(T) == 0, "Unsupported type passes to getCurrentExpressionInDictEncodedSpanAtAs<T>()");
      }
    }

    boss::Expression getCurrentExpressionInDictEncodedSpanAtAs(size_t spanArgI, uint64_t dictI, size_t dictOffsetArgumentSize, ArgumentType argumentType) const {
      // std::cout << "ARGI: " << argumentIndex << " TYPEI: " << typeIndex << std::endl;
      auto& argument = buffer.flattenedArguments()[argumentIndex];
      uint64_t tmp = static_cast<uint64_t>(argument.asLong);
      size_t valsPerArg = sizeof(Argument) / dictOffsetArgumentSize;
      int64_t inArgI = (spanArgI % valsPerArg);

      int32_t dictOffset;
      if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
	uint8_t val = static_cast<uint8_t>((tmp >> (dictOffsetArgumentSize * sizeof(Argument) * inArgI)) & 0xFFFFFFFFUL);
	dictOffset = static_cast<int32_t>(static_cast<int8_t>(val));
      } else if (dictOffsetArgumentSize == Argument_CHAR_SIZE) {
	uint32_t val = static_cast<uint32_t>((tmp >> (dictOffsetArgumentSize * sizeof(Argument) * inArgI)) & 0xFFFFFFFFUL);
	dictOffset = static_cast<int32_t>(val);
      }

      switch(argumentType) {
      case ArgumentType::ARGUMENT_TYPE_LONG:
        return getCurrentExpressionInDictEncodedSpanAtAs<int64_t>(spanArgI, dictI, dictOffsetArgumentSize);
      case ArgumentType::ARGUMENT_TYPE_DOUBLE:
        return getCurrentExpressionInDictEncodedSpanAtAs<double_t>(spanArgI, dictI, dictOffsetArgumentSize);
      case ArgumentType::ARGUMENT_TYPE_STRING:
        return getCurrentExpressionInDictEncodedSpanAtAs<std::string>(spanArgI, dictI, dictOffsetArgumentSize);
      case ArgumentType::ARGUMENT_TYPE_BOOL:
      case ArgumentType::ARGUMENT_TYPE_CHAR:
      case ArgumentType::ARGUMENT_TYPE_SHORT:
      case ArgumentType::ARGUMENT_TYPE_INT:
      case ArgumentType::ARGUMENT_TYPE_FLOAT:
      case ArgumentType::ARGUMENT_TYPE_SYMBOL:
      case ArgumentType::ARGUMENT_TYPE_EXPRESSION:
	break;
      }
      return "ErrorDeserialisingExpressionInDictEncodedSpan"_("ArgumentIndex"_(static_cast<int64_t>(argumentIndex)),
					     "TypeIndex"_(static_cast<int64_t>(typeIndex)),
					     "InSpanIndex"_(static_cast<int64_t>(spanArgI)),
					     "DictIndex"_(static_cast<int64_t>(dictI)),
					     "InDictIndex"_(static_cast<int64_t>(dictOffset)),
					     "ArgumentType"_(static_cast<int64_t>(argumentType)));
    }

    boss::Expression getCurrentExpressionInSpanAt(size_t spanArgI) const {
      auto argumentType = getCurrentExpressionType();
      return getCurrentExpressionInSpanAtAs(spanArgI, argumentType);
    }
    
    boss::Expression getCurrentExpressionInDictEncodedSpanAt(size_t spanArgI, uint64_t dictI, size_t dictOffsetArgSize) const {
      auto argumentType = getCurrentExpressionType();
      return getCurrentExpressionInDictEncodedSpanAtAs(spanArgI, dictI, dictOffsetArgSize, argumentType);
    }

    template<typename T>
    T getCurrentExpressionAs() const {
      auto& argument = buffer.flattenedArguments()[argumentIndex];
      if constexpr (std::is_same_v<T, bool>) {
	return argument.asBool;
      } else if constexpr (std::is_same_v<T, int8_t>) {
	return argument.asChar;
      } else if constexpr (std::is_same_v<T, int16_t>) {
	return argument.asShort;
      } else if constexpr (std::is_same_v<T, int32_t>) {
	return argument.asInt;
      } else if constexpr (std::is_same_v<T, int64_t>) {
	return argument.asLong;
      } else if constexpr (std::is_same_v<T, float_t>) {
	return argument.asFloat;
      } else if constexpr (std::is_same_v<T, double_t>) {
	return argument.asDouble;
      } else if constexpr (std::is_same_v<T, std::string>) {
        return viewString(buffer.root, argument.asString);
      } else if constexpr (std::is_same_v<T, boss::Symbol>) {
        return boss::Symbol(viewString(buffer.root, argument.asString));
      } else if constexpr (std::is_same_v<T, boss::Expression>) {
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
      // std::cout << "ROOT METADATA: " << std::endl;
      // std::cout << "  argumentCount: " << root->argumentCount << std::endl;
      // std::cout << "  argumentBytesCount: " << root->argumentBytesCount << std::endl;
      // std::cout << "  expressionCount: " << root->expressionCount << std::endl;
      // std::cout << "  argumentDictionaryBytesCount: " << root->argumentDictionaryBytesCount << std::endl;
      // std::cout << "  stringArgumentsFillIndex: " << root->stringArgumentsFillIndex << std::endl;
      // std::cout << "  originalAddress: " << root->originalAddress << std::endl;
      // std::cout << "ROOT: " << root << std::endl;
      // std::cout << "ARGS: " << flattenedArguments() << std::endl;
      // std::cout << "TYPES: " << flattenedArgumentTypes() << std::endl;
      // std::cout << "EXPRS: " << expressionsBuffer() << std::endl;
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
