#pragma once

#include "../../../Expression.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class SymbolicFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template allowedTypes<std::string>().template registerFunction<1>(
        "Symbol", [](auto&& batchPtr) {
          return OperatorUtils::evaluateElements(
              [](auto const& name) -> Symbol { return Symbol(name); }, batchPtr);
        });
  }
};

} // namespace boss::engines::bulk
