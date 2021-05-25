#pragma once

#include "../OperatorUtils.hpp"

#include <iomanip>
#include <sstream>

namespace boss::engines::bulk {

template <typename BatchPrototypes> class ConversionFunctions {
  using Utils = OperatorUtils<BatchPrototypes>;

public:
  static void registerAll(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<std::string>().template registerFunction<1>(
        "UnixTime", [](auto const& batch) {
          return Utils::evaluateElements(
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
