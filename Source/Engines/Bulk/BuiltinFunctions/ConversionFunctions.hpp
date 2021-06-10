#pragma once

#include "../Operator.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class ConversionFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<UnixTimeOperator>("UnixTime");
  }

private:
  class UnixTimeOperator : public OperatorBuilder<1>::OperatorForTypes<std::string> {
  public:
    template <typename ValueType> BulkExpression evaluate(ValueType const& value) const {
      return function()(value);
    }

    template <typename ValueType>
    BulkExpression evaluate(std::shared_ptr<ValueArray<ValueType>> const& arrayPtr) const {
      return OperatorUtils::evaluateForEachTuple(function(), arrayPtr);
    }

  private:
    static auto function() {
      return [](auto const& str) {
        std::istringstream iss;
        iss.str(str);
        struct std::tm tm = {};
        iss >> std::get_time(&tm, "%Y-%m-%d");
        int value = std::mktime(&tm);
        return value;
      };
    }
  };
};

} // namespace boss::engines::bulk
