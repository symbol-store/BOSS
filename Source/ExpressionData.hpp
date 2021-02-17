#pragma once

#include <string>
#include <variant>
#include <vector>

namespace boss {

template <typename T> struct ArgumentData {
  using Type = T;
  int arraySize;
  int stride;
  void const* data;
};

struct ComplexExpressionData;
using ExpressionData = std::variant<ArgumentData<bool>, ArgumentData<int>, ArgumentData<float>,
                                    ArgumentData<std::string>, ComplexExpressionData>;

struct ComplexExpressionData {
  std::string head;
  std::vector<ExpressionData> arguments;
};

} // namespace boss
