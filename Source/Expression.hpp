#pragma once

#include <string>
#include <variant>
#include <vector>
namespace boss {
class Expression {
public:
  class Symbol {
    std::string const name;

  public:
    explicit Symbol(std::string const& name) : name(name){};
    std::string const& getName() const { return name; };
  };

  using ReturnType = std::variant<bool, int, float, std::string, Symbol, Expression>;
  using ArgumentType = ReturnType;

  using ArgumentList = std::vector<ArgumentType>;

private:
  std::string const head;
  ArgumentList const arguments;

public:
  Expression(std::string const& head, ArgumentList const& args)
      : head(head), arguments(args.begin(), args.end()){};
  ArgumentList const& getArguments() const { return arguments; };
  std::string const& getHead() const { return head; };
};

} // namespace boss
