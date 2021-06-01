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
    template <typename BatchType> auto evaluate(BatchType&& batchPtr) const {
      return OperatorUtils::evaluateElements(
          [](auto const& str) -> int {
            std::istringstream iss;
            iss.str(str);
            struct std::tm tm = {};
            iss >> std::get_time(&tm, "%Y-%m-%d");
            int value = std::mktime(&tm);
            return value;
          },
          batchPtr);
    }
  };
};

} // namespace boss::engines::bulk
