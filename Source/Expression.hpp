#include <string>
#include <variant>
namespace boss {
class Expression {
public:
  class Symbol {
    std::string const name;

  public:
    explicit Symbol(std::string const& name) : name(name){};
    std::string const& getName() const { return name; };
  };

  using ReturnType = std::variant<Expression, int, std::string, Symbol, bool>;
  using ArgumentType = ReturnType;

private:
  std::string const head;
  std::vector<ArgumentType> const& arguments;

public:
  Expression(std::string const& head, std::vector<ArgumentType> const& arguments)
      : head(head), arguments(arguments){};
  std::vector<ArgumentType> const& getArguments() const { return arguments; };
  std::string const& getHead() const { return head; };
};

} // namespace boss
