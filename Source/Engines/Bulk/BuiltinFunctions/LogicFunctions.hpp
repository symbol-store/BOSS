#pragma once

#include "../OperatorUtils.hpp"

namespace boss::engines::bulk {

template <typename BatchPrototypes> class LogicFunctions {
  using Utils = OperatorUtils<BatchPrototypes>;

public:
  static void registerAll(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<bool>().template registerFunction<2>(
        "And", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return Utils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a && b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    prototypes.template allowedTypes<bool>().template registerFunction<2>(
        "Or", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return Utils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a || b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    prototypes.template allowedTypes<bool>().template registerFunction<1>(
        "Not", [](auto const& batch) {
          return Utils::evaluateElements([](auto const& a) -> bool { return !a; }, batch);
        });
  }
};

} // namespace boss::engines::bulk
