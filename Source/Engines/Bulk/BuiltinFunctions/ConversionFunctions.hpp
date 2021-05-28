#pragma once

#include <iomanip>
#include <sstream>

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class ConversionFunctions {

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();

    operatorRegistry.template allowedTypes<std::string>().template registerFunction<1>(
        "UnixTime", [](auto const& batch) {
          return OperatorUtils::evaluateElements(
              [](auto const& str) -> int {
                std::istringstream iss;
                iss.str(str);
                struct std::tm tm = {};
                iss >> std::get_time(&tm, "%Y-%m-%d");
                int value = std::mktime(&tm);
                return value;
              },
              batch);
        });
  }
};

} // namespace boss::engines::bulk
