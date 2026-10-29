#include "ExpressionParser.hpp"

#include <chibi/eval.h>
#include <mutex>

void boss::initializeSchemeOnce() {
  static std::once_flag schemeInitialized;
  std::call_once(schemeInitialized, sexp_scheme_init);
}
