#pragma once

#include "../../../Expression.hpp"
#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class SymbolicFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<SymbolOperator>("Symbol");
  }

private:
  class SymbolOperator : public OperatorBuilder<1>::OperatorForTypes<std::string> {
  public:
    template <typename BatchType> auto evaluate(BatchType&& batchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& name) -> Symbol { return Symbol(name); }, batchPtr);
    }
  };
};

} // namespace boss::engines::bulk
