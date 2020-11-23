#include <string>
#include <variant>
class Expression {
public:
  class Symbol {
    std::string const name;

  public:
    explicit Symbol(std::string const& name) : name(name){};
    std::string const& getName() const { return name; };
  };

  using ReturnType = std::variant<int, std::string, Symbol, bool>;
  using ArgumentType = std::variant<Expression, std::string, int, bool>;

private:
  std::string const& head;
  std::vector<ArgumentType> const& arguments;

public:
  Expression(std::string const& head, std::vector<ArgumentType> const& arguments)
      : head(head), arguments(arguments){};
  ReturnType get();
  std::vector<ArgumentType> const& getArguments() const { return arguments; };
  std::string const& getHead() const { return head; };
};
