#pragma once

extern "C" {
#include <chibi/eval.h>
#include <chibi/sexp.h>
}

#include "BOSS.hpp"
#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include "Utilities.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <variant>

namespace boss {

using utilities::operator""_;

struct EvalResult {
  bool is_error;
  std::string text;
};

struct BossContextGuard {
  // Initialised so a default-constructed guard destroys nothing; the destructor below would
  // otherwise pass an indeterminate pointer to sexp_destroy_context.
  sexp ctx = nullptr;
  // Spelled out rather than left to aggregate initialisation. Deleting the copy and move
  // members below makes this a non-aggregate from C++20 on, so `BossContextGuard{someCtx}`
  // would stop compiling for a consumer building at a newer standard than BOSS itself --
  // and this header is installed for exactly those consumers.
  BossContextGuard() = default;
  explicit BossContextGuard(sexp context) : ctx(context) {}
  BossContextGuard(BossContextGuard const&) = delete;
  BossContextGuard(BossContextGuard&&) = delete;
  BossContextGuard& operator=(BossContextGuard const&) = delete;
  BossContextGuard& operator=(BossContextGuard&&) = delete;
  ~BossContextGuard() {
    if(ctx != nullptr) {
      sexp_destroy_context(ctx);
    }
  }
};

/* ─── Convert BOSS Expression to chibi sexp ─── */

/// Implementation helpers. Not part of the public API of this installed header: they exist to
/// build the scheme environment, and are free to change shape without notice.
namespace detail {

inline sexp expr_to_sexp(sexp ctx, boss::Expression const& expr);

inline sexp complex_expr_to_sexp(sexp ctx, boss::ComplexExpression const& expr) {
  auto const& head = expr.getHead().getName();
  auto const& args = expr.getDynamicArguments();
  sexp_gc_var2(lst, item);
  sexp_gc_preserve2(ctx, lst, item);
  lst = SEXP_NULL;
  for(auto it = args.rbegin(); it != args.rend(); ++it) {
    item = expr_to_sexp(ctx, *it);
    lst = sexp_cons(ctx, item, lst);
  }
  if(!head.empty()) {
    item = sexp_intern(ctx, head.c_str(), -1);
    lst = sexp_cons(ctx, item, lst);
  }
  sexp_gc_release2(ctx);
  return lst;
}

inline sexp expr_to_sexp(sexp ctx, boss::Expression const& expr) {
  return std::visit(
      boss::utilities::overload(
          [&](bool v) -> sexp { return v ? SEXP_TRUE : SEXP_FALSE; },
          [&](std::int8_t v) -> sexp { return sexp_make_fixnum(v); },
          [&](std::int32_t v) -> sexp { return sexp_make_fixnum(v); },
          [&](std::int64_t v) -> sexp { return sexp_make_integer(ctx, v); },
          [&](std::float_t v) -> sexp { return sexp_make_flonum(ctx, v); },
          [&](std::double_t v) -> sexp { return sexp_make_flonum(ctx, v); },
          [&](std::string const& v) -> sexp {
            return sexp_c_string(ctx, v.c_str(), static_cast<sexp_sint_t>(v.size()));
          },
          [&](boss::Symbol const& v) -> sexp { return sexp_intern(ctx, v.getName().c_str(), -1); },
          [&](boss::ComplexExpression const& v) -> sexp { return complex_expr_to_sexp(ctx, v); }),
      static_cast<boss::Expression::SuperType const&>(expr));
}

/* ─── BOSS Scheme setup ─── */

constexpr int kSrfiFormattingLibrary = 166;
constexpr int kSrfiGeneratorsLibrary = 158;
constexpr auto kInt32Min = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
constexpr auto kInt32Max = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());

inline void setup_boss_scheme(sexp ctx, sexp env) {
  using boss::Expression;
  /* _(...) groups sub-expressions into bare parens — unlike "name"_(...) which
     produces (name ...), _(...) produces just (...). expr_to_sexp strips the empty head. */
  auto _ = ""_;
  auto eval = [&](auto&& expr) {
    sexp_gc_var1(sexp_form);
    sexp_gc_preserve1(ctx, sexp_form);
    sexp_form = expr_to_sexp(ctx, Expression(std::forward<decltype(expr)>(expr)));
    sexp_eval(ctx, sexp_form, env);
    sexp_gc_release1(ctx);
  };

  eval("import"_(_("srfi"_, kSrfiFormattingLibrary), _("srfi"_, kSrfiGeneratorsLibrary),
                 _("chibi"_, "match"_)));

  eval("define"_(_("boss-print"_, "x"_), "show"_(false, _("pretty"_, "x"_), "nl"_)));

  eval("define"_("bossTypeID"_, "quote"_(_("bool"_, "int8"_, "int32"_, "long"_, "float"_, "double"_,
                                           "string"_, "symbol"_, "complexExpression"_))));

  eval("define"_(
      _("convert-to-boss-expression"_, "x"_),
      "match"_(
          "x"_, _(_("quote"_("quote"_), "argument"_), "convert-to-boss-expression"_("argument"_)),
          _(_("head"_, "arguments"_, "..."_),
            "newComplexBOSSExpression"_("symbolNameToNewBOSSSymbol"_("symbol->string"_("head"_)),
                                        "length"_("arguments"_),
                                        "map"_("convert-to-boss-expression"_, "arguments"_))),
          _(_("?"_, "boolean?"_, "b"_), "boolToNewBOSSExpression"_("b"_)),
          _(_("?"_, "exact-integer?"_, "i"_),
            "if"_("and"_(">="_("i"_, kInt32Min), "<="_("i"_, kInt32Max)),
                  "intToNewBOSSExpression"_("i"_), "longToNewBOSSExpression"_("i"_))),
          _(_("?"_, "real?"_, "f"_), "doubleToNewBOSSExpression"_("f"_)),
          _(_("?"_, "string?"_, "s"_), "stringToNewBOSSExpression"_("s"_)),
          _(_("?"_, "symbol?"_, "s"_),
            "symbolNameToNewBOSSExpression"_("symbol->string"_("s"_))))));

  eval("define"_(
      _("convert-from-boss-expression"_, "x"_),
      "let*"_(
          _(_("type-index"_, "getBOSSExpressionTypeID"_("x"_)),
            _("type"_, "if"_("<"_("type-index"_, "length"_("bossTypeID"_)),
                             "list-ref"_("bossTypeID"_, "type-index"_), false))),
          "case"_(
              "type"_,
              _(_("complexExpression"_),
                "let"_(_(_("args"_, "getArgumentsFromBOSSExpression"_("x"_))),
                       "dynamic-wind"_(
                           "lambda"_(_(), false),
                           "lambda"_(
                               _(), "quasiquote"_(
                                        _("unquote"_("string->symbol"_("bossSymbolToNewString"_(
                                              "getHeadFromBOSSExpression"_("x"_)))),
                                          "unquote-splicing"_("generator-map->list"_(
                                              "lambda"_(_("i"_),
                                                        "convert-from-boss-expression"_(
                                                            "getArgumentFromBOSSExpressionArray"_(
                                                                "args"_, "i"_))),
                                              "make-iota-generator"_(
                                                  "getArgumentCountFromBOSSExpression"_("x"_))))))),
                           "lambda"_(_(), "freeBOSSArguments"_("args"_))))),
              _(_("int32"_), "getIntValueFromBOSSExpression"_("x"_)),
              _(_("int8"_), "getCharValueFromBOSSExpression"_("x"_)),
              _(_("string"_), "getNewStringValueFromBOSSExpression"_("x"_)),
              _(_("long"_), "getLongValueFromBOSSExpression"_("x"_)),
              _(_("double"_), "getDoubleValueFromBOSSExpression"_("x"_)),
              _(_("float"_), "getFloatValueFromBOSSExpression"_("x"_)),
              _(_("bool"_), "getBoolValueFromBOSSExpression"_("x"_)),
              _(_("symbol"_), "string->symbol"_("getNewSymbolNameFromBOSSExpression"_("x"_))),
              _("else"_, "show"_(false, "unknown, type: ", "type-index"_))))));

  eval("define-syntax"_(
      "boss-eval"_,
      "syntax-rules"_(_(),
                      _(_("boss-eval"_, "query"_),
                        "let"_(_(_("expr"_, "convert-to-boss-expression"_("quote"_("query"_)))),
                               "boss-expression-transfer!"_("expr"_),
                               "convert-from-boss-expression"_("BOSSEvaluate"_("expr"_)))))));
}

} // namespace detail

/* ─── Evaluation utilities ─── */

inline sexp eval_expr(sexp ctx, sexp env, sexp expr) {
  sexp_gc_var2(form, wrapped);
  sexp_gc_preserve2(ctx, form, wrapped);
  wrapped = sexp_list2(ctx, sexp_intern(ctx, "boss-eval", -1), expr);
  form = sexp_eval(ctx, wrapped, env);
  sexp_gc_release2(ctx);
  return form;
}

inline sexp eval_string(sexp ctx, sexp env, const char* str) {
  sexp_gc_var2(expr, port);
  sexp_gc_preserve2(ctx, expr, port);
  port = sexp_open_input_string(ctx, sexp_c_string(ctx, str, -1));
  expr = sexp_read(ctx, port);
  if(sexp_exceptionp(expr)) {
    sexp_gc_release2(ctx);
    return expr;
  }
  expr = eval_expr(ctx, env, expr);
  sexp_gc_release2(ctx);
  return expr;
}

/* ─── Initialize a full BOSS chibi context ─── */

inline sexp initialize_boss_context() {
  sexp_scheme_init();
  sexp ctx = sexp_make_eval_context(nullptr, nullptr, nullptr, 0, 0);
  if(ctx == nullptr || sexp_exceptionp(ctx)) {
    std::cerr << "Failed to initialize chibi-scheme context\n";
    return nullptr;
  }
  sexp_gc_var2(env, res);
  sexp_gc_preserve2(ctx, env, res);
  env = sexp_context_env(ctx);
  res = sexp_load_standard_env(ctx, env, SEXP_SEVEN);
  if(sexp_exceptionp(res)) {
    std::cerr << "Failed to load standard environment\n";
    sexp_gc_release2(ctx);
    sexp_destroy_context(ctx);
    return nullptr;
  }
  sexp_load_standard_ports(ctx, res, stdin, stdout, stderr, 0);
  env = sexp_make_env(ctx);
  sexp_env_parent(env) = res;
  sexp_context_env(ctx) = env;
  res = sexp_init_library(ctx, nullptr, 0, env, sexp_version, SEXP_ABI_IDENTIFIER);
  if(sexp_exceptionp(res)) {
    std::cerr << "Failed to initialize BOSS FFI bindings\n";
    sexp_print_exception(ctx, res, sexp_current_error_port(ctx));
    sexp_gc_release2(ctx);
    sexp_destroy_context(ctx);
    return nullptr;
  }
  detail::setup_boss_scheme(ctx, env);
  sexp_gc_release2(ctx);
  return ctx;
}

/* ─── Parse, evaluate, and serialize result to string ─── */

inline EvalResult evaluate_expression(sexp ctx, sexp env, std::string const& expr_str,
                                      bool pretty = true) {
  sexp_gc_var4(result, out_port, result_str, arg_list);
  sexp_gc_preserve4(ctx, result, out_port, result_str, arg_list);
  result = eval_string(ctx, env, expr_str.c_str());
  bool const is_error = sexp_exceptionp(result);
  std::string text;
  if(is_error) {
    out_port = sexp_open_output_string(ctx);
    sexp_print_exception(ctx, result, out_port);
    result_str = sexp_get_output_string(ctx, out_port);
  } else if(result != SEXP_VOID) {
    if(pretty) {
      // Not `sexp const`: sexp is a pointer typedef, so const there binds to the pointer and
      // misc-misplaced-const rejects it (the build runs clang-tidy with warnings-as-errors).
      sexp print_proc = sexp_env_ref(ctx, env, sexp_intern(ctx, "boss-print", -1), SEXP_FALSE);
      // boss-print may be missing from the environment, bound to a non-procedure, or may
      // raise; its result is only usable if it actually came back as a string.
      if(sexp_procedurep(print_proc)) {
        arg_list = sexp_list1(ctx, result);
        result_str = sexp_apply(ctx, print_proc, arg_list);
      }
    }
    if(!sexp_stringp(result_str)) {
      result_str = sexp_write_to_string(ctx, result); // fall back to chibi's own writer
    }
  }
  if(sexp_stringp(result_str)) {
    text = sexp_string_data(result_str);
  }
  sexp_gc_release4(ctx);
  return {is_error, text};
}

/* ─── Chibi-backed pretty printer (used by Shims/BossPrettyPrint.cpp) ─── */

// Returns false without writing to `stream` if this thread has no usable chibi context or
// boss-print is unavailable, or if the printer raises, so the caller can fall back to the
// compact renderer instead of producing empty output.
inline bool pretty_print_expression(std::ostream& stream,
                                    boss::ComplexExpression const& expression) {
  struct ThreadContext {
    BossContextGuard guard {initialize_boss_context()};
    // SEXP_FALSE, not nullptr, is chibi's "no value" sentinel, and it is what keeps the
    // sexp_procedurep(print_proc) test below safe: sexp_pointerp(nullptr) is true, so that
    // check would read a tag through a null pointer if print_proc were ever left null.
    sexp env = guard.ctx == nullptr ? SEXP_FALSE : sexp_context_env(guard.ctx);
    sexp print_proc =
        (env == nullptr || env == SEXP_FALSE)
            ? SEXP_FALSE
            : sexp_env_ref(guard.ctx, env, sexp_intern(guard.ctx, "boss-print", -1), SEXP_FALSE);
  };
  // const: the per-thread context is built once by its member initialisers and only read
  // afterwards -- the chibi calls below take the handles by value.
  thread_local ThreadContext const tls;
  if(tls.guard.ctx == nullptr || !sexp_procedurep(tls.print_proc)) {
    return false;
  }
  // Not `sexp const`: sexp is a pointer typedef, so const there binds to the pointer rather
  // than the pointee and misc-misplaced-const rejects it (clang-tidy runs as errors here).
  sexp ctx = tls.guard.ctx;
  sexp_gc_var3(form, arg_list, result_str);
  sexp_gc_preserve3(ctx, form, arg_list, result_str);
  form = detail::complex_expr_to_sexp(ctx, expression);
  arg_list = sexp_list1(ctx, form);
  result_str = sexp_apply(ctx, tls.print_proc, arg_list);
  bool const rendered = !sexp_exceptionp(result_str) && sexp_stringp(result_str);
  if(rendered) {
    stream << sexp_string_data(result_str);
  }
  sexp_gc_release3(ctx);
  return rendered;
}

} // namespace boss
