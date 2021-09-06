#include "../Engine.hpp"
#include "../Utilities.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <variant>

namespace boss::engines::bulk {
using namespace std;
class Engine : public boss::Engine {
private:

public:
  class Op {
  public:
    virtual boss::Expression operator()(ExpressionArguments const& args) = 0;
    virtual ~Op() = default;
  };

  template <typename Subclass, typename... AcceptableTypes> // one per argument
  class Operator : public Op {
    using type_indices = index_sequence_for<AcceptableTypes...>;

  public:
    boss::Expression operator()(ExpressionArguments const& args) override {
      return (*this)(args, index_sequence_for<AcceptableTypes...>{});
    }

    template <size_t... I>
    boss::Expression operator()(ExpressionArguments const& args, index_sequence<I...> /*unused*/) {
      return ((Subclass&)*this)((get<AcceptableTypes>(args.at(I)))...);
    };
    static auto getSignatures() { return ""; }
    ~Operator() override = default;
  };

  class Plus : public Operator<Plus, int, int> {
  public:
    auto operator()(int a, int b) { return a + b; }

    ~Plus() override = default;
    ;
  };

  class StringJoin : public Operator<StringJoin, string, string> {
  public:
    boss::Expression operator()(string const& a, string const& b) { return a + b; };
    ~StringJoin() override = default;
  };

  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = delete;
  Engine() = default;
  ;
  boss::Expression evaluate(Expression const& e) {
    map<string, unique_ptr<Op>> operatorDirectory;
    operatorDirectory.emplace("Plus", new Plus());
    operatorDirectory.emplace("StringJoin", new StringJoin());

    return std::visit(boss::utilities::overload(
                          [&](boss::ComplexExpression const& e) {
                            auto const& name = e.getHead().getName();
                            auto& op = operatorDirectory.at(name);
                            auto args = e.getArguments();
                            std::transform(begin(args), end(args), begin(args),
                                           [this](auto const& unevaluate) {
                                             return evaluate(unevaluate);
                                           });
                            return op->operator()(args);
                          },
                          [](auto& expression/*unused*/) {
                            return boss::Expression(expression);
                          }),
                      e);
    // return {};
  };
  ~Engine(){};
};

} // namespace boss::engines::bulk
