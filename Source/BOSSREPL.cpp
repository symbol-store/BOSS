#include "ExpressionParser.hpp"

extern "C" {
#include <chibi/eval.h>
#include <chibi/sexp.h>
}

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
// clang-tidy misc-include-cleaner incorrectly flags <memory> as unused when
// std::unique_ptr is instantiated with a type from a transitively-included header.
#include <memory> // NOLINT(misc-include-cleaner)
#include <string>
#include <utility>
#include <vector>

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

namespace {

/* ─── REPL and evaluation ─── */

void print_result(sexp ctx, sexp boss_print_proc, sexp result) {
  sexp_gc_var3(rooted_result, arg_list, apply_result);
  sexp_gc_preserve3(ctx, rooted_result, arg_list, apply_result);
  rooted_result = result;
  if(sexp_exceptionp(rooted_result)) {
    sexp_print_exception(ctx, rooted_result, sexp_current_error_port(ctx));
  } else if(rooted_result != SEXP_VOID) {
    bool printed = false;
    if(sexp_procedurep(boss_print_proc)) {
      arg_list = sexp_list1(ctx, rooted_result);
      apply_result = sexp_apply(ctx, boss_print_proc, arg_list);
      if(sexp_exceptionp(apply_result)) {
        sexp_print_exception(ctx, apply_result, sexp_current_error_port(ctx));
      } else if(sexp_stringp(apply_result)) {
        sexp_write_string(ctx, sexp_string_data(apply_result), sexp_current_output_port(ctx));
        printed = true;
      }
    }
    if(!printed) {
      sexp_write(ctx, rooted_result, sexp_current_output_port(ctx));
      sexp_newline(ctx, sexp_current_output_port(ctx));
    }
  }
  sexp_gc_release3(ctx);
}

bool is_incomplete_input(sexp obj) {
  return sexp_exceptionp(obj) &&
         (std::strstr(sexp_string_data(sexp_slot_ref(obj, 1)), "missing trailing") != nullptr);
}

void run_repl(sexp ctx, sexp env, bool raw) {
  sexp_gc_var6(obj, result, in, out, port, boss_print_proc);
  sexp_gc_preserve6(ctx, obj, result, in, out, port, boss_print_proc);
  in = sexp_current_input_port(ctx);
  out = sexp_current_output_port(ctx);
  boss_print_proc = sexp_env_ref(ctx, env, sexp_intern(ctx, "boss-print", -1), SEXP_FALSE);

#ifdef HAVE_READLINE
  std::string input_buf;
  int depth = 0;

  while(true) {
    const char* prompt = (depth == 0) ? "boss> " : "....> ";
    std::unique_ptr<char, decltype(&std::free)> line_ptr(readline(prompt), std::free);

    if(line_ptr == nullptr) {
      std::cout << '\n';
      break;
    }

    char* line = line_ptr.get();

    if(depth == 0 && line[0] == '\0') {
      continue;
    }

    if(line[0] != '\0') {
      add_history(line);
    }

    if(!input_buf.empty()) {
      input_buf += '\n';
    }
    input_buf += line;

    port = sexp_open_input_string(ctx, sexp_c_string(ctx, input_buf.c_str(), input_buf.size()));
    obj = sexp_read(ctx, port);

    if(obj == SEXP_EOF || is_incomplete_input(obj)) {
      depth = 1;
      continue;
    }

    if(sexp_exceptionp(obj)) {
      sexp_print_exception(ctx, obj, sexp_current_error_port(ctx));
      input_buf.clear();
      depth = 0;
      continue;
    }

    result = raw ? sexp_eval(ctx, obj, env) : boss::eval_expr(ctx, env, obj);
    print_result(ctx, boss_print_proc, result);
    input_buf.clear();
    depth = 0;
  }
#else
  while(true) {
    sexp_write_string(ctx, "boss> ", out);
    sexp_flush(ctx, out);
    obj = sexp_read(ctx, in);

    if(obj == SEXP_EOF) {
      break;
    }

    if(sexp_exceptionp(obj)) {
      sexp_print_exception(ctx, obj, sexp_current_error_port(ctx));
      continue;
    }

    result = raw ? sexp_eval(ctx, obj, env) : boss::eval_expr(ctx, env, obj);
    print_result(ctx, boss_print_proc, result);
  }
#endif

  sexp_gc_release6(ctx);
}

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog << " [options] [file]\n"
            << "  -p <expr>   Evaluate BOSS expression and exit\n"
            << "  -e <expr>   Evaluate raw Scheme expression and exit\n"
            << "  --raw       REPL without boss-eval wrapping\n"
            << "  --help      Show this help\n"
            << "  file        Load and evaluate a Scheme file\n";
}

// Returns -1 to continue, or an exit code (0 or 1) to exit immediately.
int parse_args(int argc, char** argv, bool& raw_mode, std::string& input_file,
               std::vector<std::pair<std::string, bool>>& exprs) {
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
  return -1;
}

int run_file(sexp ctx, sexp env, std::string const& input_file) {
  sexp_gc_var2(filename, result);
  sexp_gc_preserve2(ctx, filename, result);
  filename = sexp_c_string(ctx, input_file.c_str(), -1);
  result = sexp_load(ctx, filename, env);
  sexp_gc_release2(ctx);
  if(sexp_exceptionp(result)) {
    sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
    return 1;
  }
  return 0;
}

int run_exprs(sexp ctx, sexp env, std::vector<std::pair<std::string, bool>> const& exprs) {
  int exit_code = 0;
  sexp_gc_var2(result, boss_print_proc);
  sexp_gc_preserve2(ctx, result, boss_print_proc);
  boss_print_proc = sexp_env_ref(ctx, env, sexp_intern(ctx, "boss-print", -1), SEXP_FALSE);
  for(auto const& [text, raw] : exprs) {
    result = raw ? sexp_eval_string(ctx, text.c_str(), -1, env)
                 : boss::eval_string(ctx, env, text.c_str());
    print_result(ctx, boss_print_proc, result);
    if(sexp_exceptionp(result)) {
      exit_code = 1;
    }
  }
  sexp_gc_release2(ctx);
  return exit_code;
}

} // namespace

int main(int argc, char** argv) try {
  bool raw_mode = false;
  std::string input_file;
  std::vector<std::pair<std::string, bool>> exprs;

  int const parse_result = parse_args(argc, argv, raw_mode, input_file, exprs);
  if(parse_result >= 0) {
    return parse_result;
  }

  sexp ctx = boss::initialize_boss_context();
  if(ctx == nullptr) {
    return 1;
  }
  boss::BossContextGuard const ctx_guard {ctx};
  sexp env = sexp_context_env(ctx);

  int exit_code = 0;
  if(!input_file.empty()) {
    exit_code = run_file(ctx, env, input_file);
  } else if(!exprs.empty()) {
    exit_code = run_exprs(ctx, env, exprs);
  } else {
    run_repl(ctx, env, raw_mode);
  }

  return exit_code;
} catch(std::exception const& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
