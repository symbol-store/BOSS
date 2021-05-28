#pragma once

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class LogicFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template allowedTypes<bool>().template registerFunction<2>(
        "And", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a && b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<bool>().template registerFunction<2>(
        "Or", [](auto&& lhsBatchPtr, auto&& rhsBatchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& a, auto const& b) -> bool { return a || b; }, lhsBatchPtr,
              rhsBatchPtr);
        });
    operatorRegistry.template allowedTypes<bool>().template registerFunction<1>(
        "Not", [](auto const& batch) {
          return OperatorUtils::evaluateElements([](auto const& a) -> bool { return !a; }, batch);
        });
  }
};

} // namespace boss::engines::bulk
