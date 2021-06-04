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
    template <typename ValueType> BulkExpression evaluate(ValueType const& value) const {
      return function()(value);
    }

    template <typename ValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<ValueType>> const& arrayPtr) const {
      return OperatorUtils::evaluateElements(function(), arrayPtr);
    }

  private:
    static auto function() {
      return [](auto const& name) { return Symbol(name); };
    }
  };
};

} // namespace boss::engines::bulk
