#pragma once

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class ComparisonFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template allowedTypes<bool, int, float, std::string>()
        .template registerFunction<2>("Equal", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          using BatchTypeA = typename std::decay_t<decltype(lhsBatchPtr)>::BatchType;
          using BatchTypeB = typename std::decay_t<decltype(rhsBatchPtr)>::BatchType;
          using ValueTypeA = typename BatchTypeA::ValueType;
          using ValueTypeB = typename BatchTypeB::ValueType;
          if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
            return OperatorUtils::evaluateElements(
                [](auto const& a, auto const& b) -> bool {
                  return static_cast<ValueTypeA>(a) == static_cast<ValueTypeB>(b);
                },
                lhsBatchPtr, rhsBatchPtr);
          } else {
            return OperatorUtils::evaluateElements(
                [](auto const& /*a*/, auto const& /*b*/) -> bool { return false; }, lhsBatchPtr,
                rhsBatchPtr);
          }
        });
    operatorRegistry.template allowedTypes<bool, int, float, std::string>()
        .template registerFunction<2>("NotEqual", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          using BatchTypeA = typename std::decay_t<decltype(lhsBatchPtr)>::BatchType;
          using BatchTypeB = typename std::decay_t<decltype(rhsBatchPtr)>::BatchType;
          using ValueTypeA = typename BatchTypeA::ValueType;
          using ValueTypeB = typename BatchTypeB::ValueType;
          if constexpr(std::is_convertible_v<ValueTypeB, ValueTypeA>) {
            return OperatorUtils::evaluateElements(
                [](auto const& a, auto const& b) -> bool {
                  return static_cast<ValueTypeA>(a) != static_cast<ValueTypeB>(b);
                },
                lhsBatchPtr, rhsBatchPtr);
          } else {
            return OperatorUtils::evaluateElements(
                [](auto const& /*a*/, auto const& /*b*/) -> bool { return true; }, lhsBatchPtr,
                rhsBatchPtr);
          }
        });
    operatorRegistry.template allowedTypes<int, float>().template registerFunction<2>(
        "Less", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a < b; }, lhsBatchPtr, rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<int, float>().template registerFunction<2>(
        "LessEqual", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a <= b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<int, float>().template registerFunction<2>(
        "Greater", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a > b; }, lhsBatchPtr, rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<int, float>().template registerFunction<2>(
        "GreaterEqual", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a >= b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
  }
};

} // namespace boss::engines::bulk
