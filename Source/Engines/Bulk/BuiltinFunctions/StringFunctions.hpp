#pragma once

#include <string>

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class StringFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template allowedTypes<std::string>().template registerFunction<2>(
        "StringJoin", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> std::string { return a + b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<std::string>().template registerFunction<2>(
        "StringContainsQ", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a.find(b) != std::string::npos; },
              lhsBatchPtr, rhsBatchPtr);
        });
  }
};

} // namespace boss::engines::bulk
