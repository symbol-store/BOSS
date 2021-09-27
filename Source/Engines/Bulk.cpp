#include "Bulk.hpp"

namespace boss::engines::bulk {

OperatorDirectory& Engine::getOperatorDirectory() {
  static OperatorDirectory operatorDirectory;
  return operatorDirectory;
}

boss::Expression Engine::evaluate(Expression const& e) {
  return std::visit(
      boss::utilities::overload(
          [&](boss::ComplexExpression const& e) -> boss::Expression {
            auto const& name = e.getHead().getName();
            auto args = e.getArguments();
            std::transform(begin(args), end(args), begin(args),
                           [this](auto const& unevaluate) { return evaluate(unevaluate); });
            auto typeID =
                std::accumulate(args.begin(), args.end(), 0, [](size_t id, auto const& arg) {
                  return id * variant_size_v<boss::Expression::SuperType> + (arg.index() + 1);
                });
            if(getOperatorDirectory().count({name, typeID}) == 0) {
              return e;
            }
            return (*getOperatorDirectory().at({name, typeID}))(args);
          },
          [](auto& expression /*unused*/) { return boss::Expression(expression); }),
      e);
}
} // namespace boss::engines::bulk

////////////////////////// Let's define some operators /////////////////////////

template <typename... ArgumentTypes>
class Greater : public boss::engines::bulk::Operator<Greater, ArgumentTypes...> {
public:
  using ArgumentTypesT = variant<tuple<int, int>, tuple<float, float>>;
  template <typename T1, typename T2> bool operator()(T1 t1, T2 t2) { return t1 > t2; }
  ~Greater() override = default;
};
namespace {
static boss::engines::bulk::Engine::Register<Greater> const r("Greater");
}

template <typename... ArgumentTypes>
class Plus : public boss::engines::bulk::Operator<Plus, ArgumentTypes...> {
public:
  using ArgumentTypesT = variant<tuple<int, int>, tuple<float, float>, tuple<int, int, int>>;
  auto operator()(ArgumentTypes... args) { return (args + ...); }
  ~Plus() override = default;
};
namespace {
static boss::engines::bulk::Engine::Register<Plus> const r1("Plus");
}

template <typename... ArgumentTypes>
class StringJoin : public boss::engines::bulk::Operator<StringJoin, ArgumentTypes...> {
public:
  using ArgumentTypesT = variant<tuple<string, string>, tuple<string, string, string>>;
  auto operator()(ArgumentTypes... args) { return (args + ...); }
  ~StringJoin() override = default;
};
namespace {
static boss::engines::bulk::Engine::Register<StringJoin> const r2("StringJoin");
}

template <typename... ArgumentTypes>
class Sym : public boss::engines::bulk::Operator<Sym, ArgumentTypes...> {
public:
  using ArgumentTypesT = variant<tuple<string>>;
  auto operator()(string const& name) { return boss::Symbol(name); }
  ~Sym() override = default;
};
namespace {
static boss::engines::bulk::Engine::Register<Sym> const r3("Symbol");
}
