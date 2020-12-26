#pragma once

#include "BatchTemplates.hpp"
#include "SymbolPool.hpp"

#include "../../Expression.hpp"

namespace boss::engines::bulk {

/****************** class BuiltinFunctions ********************/

/* Helper class just for registering all the builin functions */
/**************************************************************/

class BuiltinFunctions {
public:
  BuiltinFunctions(BatchTemplates& templates) {
    auto& symbolPool = SymbolPool<>::instance();

    // Arithmetic
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "Plus", [](auto const& a, auto const& b) -> auto { return a + b; });
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "Minus", [](auto const& a, auto const& b) -> auto { return a - b; });
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "Times", [](auto const& a, auto const& b) -> auto { return a * b; });
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "Divide", [](auto const& a, auto const& b) -> auto { return a / b; });

    // Comparison
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "Equal", [](auto const& a, auto const& b) -> bool { return a == b; });
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "NotEqual", [](auto const& a, auto const& b) -> bool { return a != b; });
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "Less", [](auto const& a, auto const& b) -> bool { return a < b; });
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "LessEqual", [](auto const& a, auto const& b) -> bool { return a <= b; });
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "Greater", [](auto const& a, auto const& b) -> bool { return a > b; });
    templates.allowedTypes<bool, int, float>().registerFunction<2>(
        "GreaterEqual", [](auto const& a, auto const& b) -> bool { return a >= b; });

    // Logic
    templates.allowedTypes<bool>().registerFunction<2>(
        "And", [](auto const& a, auto const& b) -> bool { return a && b; });
    templates.allowedTypes<bool>().registerFunction<2>(
        "Or", [](auto const& a, auto const& b) -> bool { return a || b; });
    templates.allowedTypes<bool>().registerFunction<1>("Not",
                                                       [](auto const& a) -> bool { return !a; });

    // Strings
    templates.allowedTypes<std::string>().registerFunction<2>(
        "StringJoin", [](auto const& a, auto const& b) -> std::string { return a + b; });

    // Symbolic
    templates.allowedTypes<std::string>().registerFunction<1>(
        "Symbol", [](auto const& name) -> Expression::Symbol { return Expression::Symbol(name); });
  }
};

} // namespace boss::engines::bulk
