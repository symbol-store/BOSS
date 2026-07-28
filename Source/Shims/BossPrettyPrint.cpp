// Installs the chibi-backed pretty-print hook read by Expression.hpp's operator<<
// for ComplexExpression. Linking this translation unit into a binary enables
// multi-line pretty output for `stream << boss::pretty << expr`; omitting it
// leaves prettyPrintHook() returning nullptr, and the manipulator becomes a
// silent no-op that falls through to the compact renderer. The hook also falls
// back to the compact renderer when it is installed but cannot render, by
// returning false.

#include "../ExpressionParser.hpp"

namespace {
struct HookInstaller {
  HookInstaller() { boss::prettyPrintHook() = &boss::pretty_print_expression; }
};
HookInstaller const globalHookInstaller {};
} // namespace
