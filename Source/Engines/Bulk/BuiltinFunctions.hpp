#pragma once

#include "BatchTemplates.hpp"
#include "SymbolPool.hpp"

#include "../../Expression.hpp"

#include <string>
#include <utility>
#include <vector>

namespace boss::engines::bulk {

/****************** class BuiltinFunctions ********************/

/* Helper class just for registering all the builin functions */
/**************************************************************/

template <typename... SupportedTypes> class BuiltinFunctions {
public:
  using BatchTemplates = BatchTemplates<SupportedTypes...>;
  using CompoundBatch = typename BatchTemplates::CompoundBatch;
  using AnyExpressionBatch = typename BatchTemplates::AnyExpressionBatch;

  BuiltinFunctions(BatchTemplates& templates) {
    auto& symbolPool = DefaultSymbolPool::instance();

    // Arithmetic
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Plus", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a + b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Minus", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a - b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Times", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a * b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Divide", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a / b; }, lhsBatch, rhsBatch);
        });

    // Comparison
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Equal", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a == b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "NotEqual", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a != b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Less", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a < b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "LessEqual", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a <= b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "Greater", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a > b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool, int, float>().template registerFunction<2>(
        "GreaterEqual", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a >= b; }, lhsBatch, rhsBatch);
        });

    // Logic
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "And", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a && b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool>().template registerFunction<2>(
        "Or", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a || b; }, lhsBatch, rhsBatch);
        });
    templates.template allowedTypes<bool>().template registerFunction<1>(
        "Not", [](auto const& batch) {
          return BuiltinFunctions::evaluateElements([](auto const& a) -> bool { return !a; },
                                                    batch);
        });

    // Strings
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringJoin", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> std::string { return a + b; }, lhsBatch,
              rhsBatch);
        });
    templates.template allowedTypes<std::string>().template registerFunction<2>(
        "StringContainsQ", [](auto const& lhsBatch, auto const& rhsBatch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a.find(b) != std::string::npos; },
              lhsBatch, rhsBatch);
        });

    // Symbolic
    templates.template allowedTypes<std::string>().template registerFunction<1>(
        "Symbol", [](auto const& batch) {
          return BuiltinFunctions::evaluateElements(
              [](auto const& name) -> Symbol { return Symbol(name); }, batch);
        });

    // Collections
    templates.template argBatchTypes<CompoundBatch, RLEBatch<int>>().template registerFunction<2>(
        "Extract", [](auto const& batchExpr, auto const& batchNth) {
          // assuming batchNth is a fixed value along the rows...
          return batchExpr.extract(*batchNth.begin() - 1);
        });
  }
  
private:
  // helpers to retrieve return type for a specific set of Batch argument types
  template <typename T> using FromBatchTypeToElementType = typename T::ValueType;
  template <typename T> struct BatchIsRLE { static constexpr auto value = T::IsRLE::value; };
  template <typename Func, typename... BatchTypes>
  using ReturnType = typename std::invoke_result_t<Func, FromBatchTypeToElementType<BatchTypes>...>;

  // helpers to iterate and evaluate on each element of a batch
  template <typename Func, typename... BatchIn>
  static BatchPtr evaluateElements(Func&& func, BatchIn const&... in) {
    auto apply = [&](auto& out, auto&&... inIt) {
      auto outIt = out.begin();
      for(; outIt != out.end() && ((inIt != in.end()) && ...); ++outIt, ((++inIt), ...)) {
        *outIt = func((*inIt)...);
      }
    };

    using ReturnType = ReturnType<std::decay_t<decltype(func)>, std::decay_t<decltype(in)>...>;
    if constexpr(std::is_same_v<ReturnType, Symbol>) {
      // assuming symbol to be always a single output
      // (different symbols must be dispatched to different batches!)
      auto* outputBatch = new SymbolBatch(1);
      apply(*outputBatch, in.begin()...);
      return BatchPtr(outputBatch);
    } else if constexpr(std::is_same_v<ReturnType, ComplexExpression>) {
      auto* outputBatch = new CompoundBatch();
      apply(*outputBatch, in.begin()...);
      return BatchPtr(outputBatch);
    } else {
      size_t outputSize = 1;
      (..., [&outputSize, &in]() { outputSize = std::max(outputSize, in.size()); }());
      if constexpr((... && BatchIsRLE<std::decay_t<decltype(in)>>::value)) {
        auto* outputBatch = new RLEBatch<ReturnType>(outputSize, ReturnType());
        apply(*outputBatch, in.begin()...);
        return BatchPtr(outputBatch);
      } else {
        auto* outputBatch = new ValueBatch<ReturnType>(outputSize, ReturnType());
        apply(*outputBatch, in.begin()...);
        return BatchPtr(outputBatch);
      }
    }
  }
};

} // namespace boss::engines::bulk
