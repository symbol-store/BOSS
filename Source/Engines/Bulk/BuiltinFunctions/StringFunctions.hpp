#pragma once

#include "../OperatorUtils.hpp"

#include <string>

namespace boss::engines::bulk {

template <typename BatchPrototypes> class StringFunctions {
  using Utils = OperatorUtils<BatchPrototypes>;

public:
  static void registerAll(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<std::string>().template registerFunction<2>(
        "StringJoin", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return Utils::evaluateElements(
              [](auto const& a, auto const& b) -> std::string { return a + b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    prototypes.template allowedTypes<std::string>().template registerFunction<2>(
        "StringContainsQ", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return Utils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a.find(b) != std::string::npos; },
              lhsBatchPtr, rhsBatchPtr);
        });
  }
};

} // namespace boss::engines::bulk
