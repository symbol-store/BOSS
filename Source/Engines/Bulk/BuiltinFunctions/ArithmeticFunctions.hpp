#pragma once

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class ArithmeticFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template allowedTypes<int, float>().template registerFunction<2>(
        "Plus", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a + b; }, lhsBatchPtr, rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<int, float>().template registerFunction<2>(
        "Minus", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a - b; }, lhsBatchPtr, rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<int, float>().template registerFunction<2>(
        "Times", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a * b; }, lhsBatchPtr, rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<int, float>().template registerFunction<2>(
        "Divide", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a / b; }, lhsBatchPtr, rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<int, float>().template registerFunction<1>(
        "Negation", [](auto&& lhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a) -> auto { return -a; }, lhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<float>().template registerFunction<3>(
        "Lerp", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr, auto&& ratioBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b, auto const& t) -> auto { return a + (b - a) * t; },
              lhsBatchPtr, rhsBatchPtr, ratioBatchPtr);
        });
  }
};

} // namespace boss::engines::bulk
