#pragma once
#include <string>
#include <variant>
#include <vector>
namespace boss {
class Symbol {
  std::string name;

public:
  explicit Symbol(std::string const& name) : name(name){};
  std::string const& getName() const { return name; };
};

template <typename T, typename... Args> struct variant_amend;

template <typename... Args0, typename... Args1>
struct variant_amend<std::variant<Args0...>, Args1...> {
  using type = std::variant<Args0..., Args1...>;
};

using AtomicExpression = std::variant<bool, int, float, std::string, Symbol>;
class ComplexExpression;
using Expression = variant_amend<AtomicExpression, ComplexExpression>::type;
using ExpressionArguments = std::vector<Expression>;

class ComplexExpression {
private:
  Symbol head;
  ExpressionArguments arguments;

public:
  explicit ComplexExpression(Symbol const& head, ExpressionArguments const& arguments)
      : head(head), arguments(arguments){};
  ExpressionArguments const& getArguments() const { return arguments; };
  Symbol const& getHead() const { return head; };
};

} // namespace boss
