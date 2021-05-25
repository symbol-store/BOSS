#pragma once

#include "../OperatorUtils.hpp"

namespace boss::engines::bulk {

template <typename BatchPrototypes> class ArithmeticFunctions {
  using Utils = OperatorUtils<BatchPrototypes>;

public:
  static void registerAll(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Plus", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return Utils::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a + b; }, lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Minus", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return Utils::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a - b; }, lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Times", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return Utils::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a * b; }, lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<2>(
        "Divide", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return Utils::evaluateElements(
              [](auto const& a, auto const& b) -> auto { return a / b; }, lhsBatchPtr, rhsBatchPtr);
        });
    prototypes.template allowedTypes<int, float>().template registerFunction<1>(
        "Negation", [](auto&& lhsBatchPtr) {
          return Utils::evaluateElements(
              [](auto const& a) -> auto { return -a; }, lhsBatchPtr);
        });
    prototypes.template allowedTypes<float>().template registerFunction<3>(
        "Lerp", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr, auto&& ratioBatchPtr) {
          return Utils::evaluateElements(

              [](auto const& a, auto const& b, auto const& t) -> auto { return a + (b - a) * t; },
              lhsBatchPtr, rhsBatchPtr, ratioBatchPtr);
        });
  }
};

} // namespace boss::engines::bulk
