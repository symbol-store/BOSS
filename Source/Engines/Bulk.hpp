#include "../Engine.hpp"
#include "../Utilities.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace boss::engines::bulk {
class Engine : public boss::Engine {
public:
  class Op {
  public:
    virtual boss::Expression operator()(ExpressionArguments const& args) = 0;
    virtual ~Op() = default;
  };

  template <template <typename...> typename Subclass,
            typename... AcceptableTypes> // one per argument
  class Operator : public Op {
    using type_indices = std::index_sequence_for<AcceptableTypes...>;

  public:
    boss::Expression operator()(ExpressionArguments const& args) override {
      return (*this)(args, type_indices{});
    }

    template <size_t... I>
    boss::Expression operator()(ExpressionArguments const& args,
                                std::index_sequence<I...> /*unused*/) {

      return ((Subclass<AcceptableTypes...>&)*this)((std::get<AcceptableTypes>(args.at(I)))...);
    };
    static auto getSignatures() { return ""; }
    ~Operator() override = default;
  };

  template <typename... ArgumentTypes> class Plus : public Operator<Plus, ArgumentTypes...> {
  public:
    template <typename A, typename B> auto operator()(A a, B b) { return a + b; }

    auto operator()(ArgumentTypes... args) { return (args + ...); }

    ~Plus() override = default;
  };

  template <typename... ArgumentTypes>
  class StringJoin : public Operator<StringJoin, ArgumentTypes...> {
  public:
    auto operator()(ArgumentTypes... args) { return (args + ...); }
    ~StringJoin() override = default;
  };

  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = delete;
  Engine() = default;

  class OperatorDirectory : public std::map<std::string, std::unique_ptr<Op>> {
  public:
    template <template <typename...> typename Operator, typename... AcceptedTypes>
    auto emplace(std::string const& name) {
      return map::emplace(name, new Operator<AcceptedTypes...>());
    }
  };

  boss::Expression evaluate(Expression const& e) {

    OperatorDirectory operatorDirectory;
    operatorDirectory.emplace<Plus, int, int>("Plus");
    operatorDirectory.emplace<StringJoin, std::string, std::string>("StringJoin");

    return std::visit(boss::utilities::overload(
                          [&](boss::ComplexExpression const& e) {
                            auto const& name = e.getHead().getName();
                            auto& op = operatorDirectory.at(name);
                            auto args = e.getArguments();
                            std::transform(
                                begin(args), end(args), begin(args),
                                [this](auto const& unevaluate) { return evaluate(unevaluate); });
                            return op->operator()(args);
                          },
                          [](auto& expression /*unused*/) { return boss::Expression(expression); }),
                      e);
  };
  ~Engine() = default;
};

} // namespace boss::engines::bulk
