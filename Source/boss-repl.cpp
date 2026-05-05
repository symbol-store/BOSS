#include "Expression.hpp"
#include "ExpressionUtilities.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

extern "C" {
#include <chibi/eval.h>
}

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

extern "C" {
/* Forward declaration of the chibi-ffi generated init function */
sexp sexp_init_library(sexp ctx, sexp self, sexp_sint_t n, sexp env, const char* version,
                       const sexp_abi_identifier_t abi);
}

using boss::utilities::operator""_;

/* ─── Convert BOSS Expression directly to chibi sexp ─── */

static sexp expr_to_sexp(sexp ctx, boss::Expression const& expr) {
  return std::visit(
      boss::utilities::overload(
          [&](bool v) -> sexp { return v ? SEXP_TRUE : SEXP_FALSE; },
          [&](std::int8_t v) -> sexp { return sexp_make_fixnum(v); },
          [&](std::int32_t v) -> sexp { return sexp_make_fixnum(v); },
          [&](std::int64_t v) -> sexp { return sexp_make_integer(ctx, v); },
          [&](std::float_t v) -> sexp { return sexp_make_flonum(ctx, v); },
          [&](std::double_t v) -> sexp { return sexp_make_flonum(ctx, v); },
          [&](std::string const& v) -> sexp { return sexp_c_string(ctx, v.c_str(), v.size()); },
          [&](boss::Symbol const& v) -> sexp { return sexp_intern(ctx, v.getName().c_str(), -1); },
          [&](boss::ComplexExpression const& v) -> sexp {
            auto const& head = v.getHead().getName();
            auto const& args = v.getDynamicArguments();
            sexp lst = SEXP_NULL;
            for(auto it = args.rbegin(); it != args.rend(); ++it) {
              lst = sexp_cons(ctx, expr_to_sexp(ctx, *it), lst);
            }
            if(!head.empty())
              lst = sexp_cons(ctx, sexp_intern(ctx, head.c_str(), -1), lst);
            return lst;
          }),
      static_cast<boss::Expression::SuperType const&>(expr));
}

/* ─── Build the BOSS Scheme initialization code as chibi sexps ─── */

static void build_and_eval_boss_scheme(sexp ctx, sexp env) {
  using boss::Expression;
  /* _(...) groups sub-expressions into bare parens — unlike "name"_(...) which
     produces (name ...), _(...) produces just (...). expr_to_sexp strips the empty head. */
  auto _ = ""_;
  auto eval = [&](auto&& expr) {
    sexp_eval(ctx, expr_to_sexp(ctx, Expression(std::forward<decltype(expr)>(expr))), env);
  };

  eval("import"_(_("srfi"_, 166), _("srfi"_, 158), _("chibi"_, "match"_)));

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
            "if"_("and"_(">="_("i"_, (std::int64_t)-2147483648),
                         "<="_("i"_, (std::int32_t)2147483647)),
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

/* ─── REPL and evaluation ─── */

static void print_result(sexp ctx, sexp result) {
  if(sexp_exceptionp(result)) {
    sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
  } else if(result != SEXP_VOID) {
    sexp_write(ctx, result, sexp_current_output_port(ctx));
    sexp_newline(ctx, sexp_current_output_port(ctx));
  }
}

/* Wrap an expression in (boss-eval <expr>) and evaluate it */
static sexp boss_eval_expr(sexp ctx, sexp env, sexp expr) {
  sexp_gc_var1(form);
  sexp_gc_preserve1(ctx, form);
  form = sexp_eval(ctx, sexp_list2(ctx, sexp_intern(ctx, "boss-eval", -1), expr), env);
  sexp_gc_release1(ctx);
  return form;
}

/* Evaluate a string as a BOSS expression (parse then wrap in boss-eval) */
static sexp boss_eval_string(sexp ctx, sexp env, const char* str) {
  sexp_gc_var2(expr, port);
  sexp_gc_preserve2(ctx, expr, port);
  port = sexp_open_input_string(ctx, sexp_c_string(ctx, str, -1));
  expr = sexp_read(ctx, port);
  if(sexp_exceptionp(expr)) {
    sexp_gc_release2(ctx);
    return expr;
  }
  expr = boss_eval_expr(ctx, env, expr);
  sexp_gc_release2(ctx);
  return expr;
}

static void run_repl(sexp ctx, sexp env, bool raw) {
  sexp_gc_var5(obj, result, in, out, port);
  sexp_gc_preserve5(ctx, obj, result, in, out, port);
  in = sexp_current_input_port(ctx);
  out = sexp_current_output_port(ctx);

#ifdef HAVE_READLINE
  std::string input_buf;
  int depth = 0;

  while(true) {
    const char* prompt = (depth == 0) ? "boss> " : "....> ";
    char* line = readline(prompt);

    if(!line) {
      std::cout << '\n';
      break;
    }

    if(depth == 0 && line[0] == '\0') {
      free(line); // readline allocates with malloc
      continue;
    }

    if(line[0] != '\0')
      add_history(line);

    if(!input_buf.empty())
      input_buf += '\n';
    input_buf += line;
    free(line); // readline allocates with malloc

    port = sexp_open_input_string(ctx, sexp_c_string(ctx, input_buf.c_str(), input_buf.size()));
    obj = sexp_read(ctx, port);

    if(obj == SEXP_EOF ||
       (sexp_exceptionp(obj) &&
        std::strstr(sexp_string_data(sexp_slot_ref(obj, 1)), "missing trailing"))) {
      depth = 1;
      continue;
    }

    if(sexp_exceptionp(obj)) {
      sexp_print_exception(ctx, obj, sexp_current_error_port(ctx));
      input_buf.clear();
      depth = 0;
      continue;
    }

    result = raw ? sexp_eval(ctx, obj, env) : boss_eval_expr(ctx, env, obj);
    print_result(ctx, result);
    input_buf.clear();
    depth = 0;
  }
#else
  while(true) {
    sexp_write_string(ctx, "boss> ", out);
    sexp_flush(ctx, out);
    obj = sexp_read(ctx, in);

    if(obj == SEXP_EOF)
      break;

    if(sexp_exceptionp(obj)) {
      sexp_print_exception(ctx, obj, sexp_current_error_port(ctx));
      continue;
    }

    result = raw ? sexp_eval(ctx, obj, env) : boss_eval_expr(ctx, env, obj);
    print_result(ctx, result);
  }
#endif

  sexp_gc_release4(ctx);
}

static void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog << " [options] [file]\n"
            << "  -p <expr>   Evaluate BOSS expression and exit\n"
            << "  -e <expr>   Evaluate raw Scheme expression and exit\n"
            << "  --raw       REPL without boss-eval wrapping\n"
            << "  --help      Show this help\n"
            << "  file        Load and evaluate a Scheme file\n";
}

int main(int argc, char** argv) {
  bool raw_mode = false;
  std::string input_file;
  std::vector<std::pair<std::string, bool>> exprs;

  for(int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if(arg == "-p" && i + 1 < argc) {
      exprs.emplace_back(argv[++i], false);
    } else if(arg == "-e" && i + 1 < argc) {
      exprs.emplace_back(argv[++i], true);
    } else if(arg == "--raw") {
      raw_mode = true;
    } else if(arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    } else if(arg[0] != '-' && input_file.empty()) {
      input_file = arg;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      print_usage(argv[0]);
      return 1;
    }
  }

  sexp_scheme_init();
  sexp ctx = sexp_make_eval_context(NULL, NULL, NULL, 0, 0);
  if(!ctx || sexp_exceptionp(ctx)) {
    std::cerr << "Failed to initialize chibi-scheme context\n";
    return 1;
  }

  sexp env = sexp_context_env(ctx);
  sexp res = sexp_load_standard_env(ctx, env, SEXP_SEVEN);
  if(sexp_exceptionp(res)) {
    std::cerr << "Failed to load standard environment\n";
    sexp_destroy_context(ctx);
    return 1;
  }

  sexp_load_standard_ports(ctx, res, stdin, stdout, stderr, 0);
  env = sexp_make_env(ctx);
  sexp_env_parent(env) = res;
  sexp_context_env(ctx) = env;

  res = sexp_init_library(ctx, NULL, 0, env, sexp_version, SEXP_ABI_IDENTIFIER);
  if(sexp_exceptionp(res)) {
    std::cerr << "Failed to initialize BOSS FFI bindings\n";
    sexp_print_exception(ctx, res, sexp_current_error_port(ctx));
    sexp_destroy_context(ctx);
    return 1;
  }

  build_and_eval_boss_scheme(ctx, env);

  int exit_code = 0;
  if(!input_file.empty()) {
    sexp_gc_var2(filename, result);
    sexp_gc_preserve2(ctx, filename, result);
    filename = sexp_c_string(ctx, input_file.c_str(), -1);
    result = sexp_load(ctx, filename, env);
    sexp_gc_release2(ctx);
    if(sexp_exceptionp(result)) {
      sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
      exit_code = 1;
    }
    sexp_destroy_context(ctx);
    return exit_code;
  } else if(!exprs.empty()) {
    for(auto const& [text, raw] : exprs) {
      sexp result = raw ? sexp_eval_string(ctx, text.c_str(), -1, env)
                        : boss_eval_string(ctx, env, text.c_str());
      print_result(ctx, result);
      if(sexp_exceptionp(result))
        exit_code = 1;
    }
  } else {
    run_repl(ctx, env, raw_mode);
  }

  sexp_destroy_context(ctx);
  return exit_code;
}
