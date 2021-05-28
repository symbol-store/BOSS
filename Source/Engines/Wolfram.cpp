#include <cstring>
#ifdef WSINTERFACE
#include "../Utilities.hpp"
#include "Wolfram.hpp"
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#define STRINGIFY(x) #x        // NOLINT
#define STRING(x) STRINGIFY(x) // NOLINT

namespace boss::engines::wolfram {
using std::set;
using std::string;
using std::to_string;
using std::vector;
using boss::utilities::operator""_;
using std::string_literals::operator""s;
using std::endl;
struct NOOPConsole : public std::ostringstream {
  template <typename T> std::ostream& operator<<(T /*unused*/) { return *this; }
  std::ostream& operator<<(std::ostream& (*/*pf*/)(std::ostream&)) { return *this; };
};
static NOOPConsole noOpConsole; // NOLINT

struct EngineImplementation {
  constexpr static char const* const DefaultNamespace = "BOSS`";
  WSENV environment = {};
  WSLINK link = {};

  static char const* removeNamespace(char const* symbolName) {
    if(strncmp(DefaultNamespace, symbolName, strlen(DefaultNamespace)) == 0) {
      return symbolName + strlen(DefaultNamespace);
    }
    return symbolName;
  }

  static auto mangle(std::string normalizedName) {
    normalizedName = std::regex_replace(normalizedName, std::regex("_"), "$$0");
    normalizedName = std::regex_replace(normalizedName, std::regex("\\."), "$$1");
    return normalizedName;
  }

  static auto demangle(std::string normalizedName) {
    normalizedName = std::regex_replace(normalizedName, std::regex("$0"), "_");
    normalizedName = std::regex_replace(normalizedName, std::regex("$1"), ".");
    return normalizedName;
  }

  void putExpressionOnLink(Expression const& expression, std::string namespaceIdentifier,
                           std::ostream& console) {
    std::visit(
        boss::utilities::overload(
            [&](bool a) {
              console << (a ? "True" : "False");
              WSPutSymbol(link, (a ? "True" : "False"));
            },
            [&](int a) {
              console << a;
              WSPutInteger(link, a);
            },
            [&](char const* a) {
              console << a;
              WSPutString(link, a);
            },
            [&](float a) {
              console << a;
              WSPutFloat(link, a);
            },
            [&](Symbol const& a) {
              auto normalizedName = mangle(a.getName());
              auto unnamespacedSymbols = set<string>{"TimeZone"};
              auto namespaced =
                  (unnamespacedSymbols.count(normalizedName) > 0 ? "" : namespaceIdentifier) +
                  normalizedName;
              console << namespaced;
              WSPutSymbol(link, namespaced.c_str());
            },
            [&](std::string const& a) {
              console << "\"" << a << "\"";
              WSPutString(link, a.c_str());
            },
            [&](ComplexExpression const& expression) {
              console << (namespaceIdentifier + expression.getHead().getName()) << "[";
              WSPutFunction(link, (namespaceIdentifier + expression.getHead().getName()).c_str(),
                            expression.getArguments().size());
              for(auto it = expression.getArguments().begin();
                  it != expression.getArguments().end(); ++it) {
                auto const& argument = *it;
                if(it != expression.getArguments().begin()) {
                  console << ", ";
                }
                putExpressionOnLink(argument, namespaceIdentifier, console);
              }
              console << "]";
            }),
        expression);
  }

  Expression readExpressionFromLink() {
    auto resultType = WSGetType(link);
    if(resultType == WSTKSTR) {
      char const* resultAsCString = nullptr;
      WSGetString(link, &resultAsCString);
      auto result = std::string(resultAsCString);
      WSReleaseString(link, resultAsCString);
      return result;
    }
    if(resultType == WSTKINT) {
      int result = 0;
      WSGetInteger(link, &result);
      return result;
    }
    if(resultType == WSTKFUNC) {
      auto const* resultHead = "";
      auto numberOfArguments = 0;
      auto success = WSGetFunction(link, &resultHead, &numberOfArguments);
      if(success == 0) {
        throw std::runtime_error("error when getting function "s + WSErrorMessage(link));
      }
      auto resultArguments = vector<Expression>();
      for(auto i = 0U; i < numberOfArguments; i++) {
        resultArguments.push_back(readExpressionFromLink());
      }
      auto result = ComplexExpression(Symbol(removeNamespace(resultHead)), resultArguments);
      WSReleaseSymbol(link, resultHead);
      return result;
    }
    if(resultType == WSTKSYM) {
      auto const* result = "";
      WSGetSymbol(link, &result);
      auto resultingSymbol = Symbol(demangle(removeNamespace(result)));
      WSReleaseSymbol(link, result);
      if(std::string("True") == resultingSymbol.getName()) {
        return true;
      }
      if(std::string("False") == resultingSymbol.getName()) {
        return false;
      }
      return resultingSymbol;
    }
    if(resultType == WSTKERROR) {
      const char* messageAsCString = WSErrorMessage(link);
      auto message = string(messageAsCString);
      WSReleaseErrorMessage(link, messageAsCString);
      throw std::runtime_error(message);
    }
    throw std::logic_error("unsupported return type: " + std::to_string(resultType));
  }

  static utilities::ExpressionBuilder namespaced(utilities::ExpressionBuilder const& builder) {
    return utilities::ExpressionBuilder(Symbol(DefaultNamespace + Symbol(builder).getName()));
  }
  static Symbol namespaced(Symbol const& name) { return Symbol(DefaultNamespace + name.getName()); }
  static ComplexExpression namespaced(ComplexExpression const& name) {
    return ComplexExpression(Symbol(DefaultNamespace + name.getHead().getName()),
                             name.getArguments());
  }

  void evalWithoutNamespace(Expression const& expression) { evaluate(expression, ""); };

  void DefineFunction(Symbol const& name, const vector<Expression>& arguments,
                      Expression const& definition, vector<Symbol> const& attributes = {}) {
    evalWithoutNamespace("SetDelayed"_(namespaced(ComplexExpression(name, arguments)), definition));
    for(auto const& it : attributes) {
      evalWithoutNamespace("SetAttributes"_(namespaced(name), it));
    }
  };

  void loadRelationalOperators() {
    DefineFunction("Where"_, {"Pattern"_("condition"_, "Blank"_())},
                   "Function"_("tuple"_, "ReplaceAll"_("condition"_, "tuple"_)), {"HoldFirst"_});

    DefineFunction("Column"_, {"Pattern"_("input"_, "Blank"_()), "Pattern"_("column"_, "Blank"_())},
                   "Extract"_("input"_, "column"_), {"HoldFirst"_});

    DefineFunction(
        "As"_, {"Pattern"_("projections"_, "BlankSequence"_())},
        "Function"_("tuple"_,
                    "Association"_("Thread"_("Rule"_(
                        "Part"_("List"_("projections"_), "Span"_(1, "All"_, 2)),
                        "ReplaceAll"_("Part"_("List"_("projections"_), "Span"_(2, "All"_, 2)),
                                      "tuple"_))))),
        {"HoldAll"_});

    DefineFunction("By"_, {"Pattern"_("projections"_, "BlankSequence"_())},
                   "Function"_("tuple"_, "ReplaceAll"_("List"_("projections"_), "tuple"_)),
                   {"HoldAll"_});

    DefineFunction("GetPersistentTableIfSymbol"_, {"Pattern"_("input"_, "Blank"_("Symbol"_))},
                   "Database"_("input"_));
    DefineFunction("GetPersistentTableIfSymbol"_, {"Pattern"_("input"_, "Blank"_())}, "input"_,
                   {"HoldAll"_});

    DefineFunction(
        "Project"_, {"Pattern"_("input"_, "Blank"_()), "Pattern"_("projection"_, "Blank"_())},
        "Map"_("projection"_, namespaced("GetPersistentTableIfSymbol"_)("input"_)), {"HoldAll"_});

    DefineFunction(
        "ProjectAll"_, {"Pattern"_("input"_, "Blank"_()), "Pattern"_("projection"_, "Blank"_())},
        "Map"_("KeyMap"_("Function"_("oldname"_, "Symbol"_("StringJoin"_(
                                                     DefaultNamespace, "SymbolName"_("projection"_),
                                                     "$1", "SymbolName"_("oldname"_))))),
               namespaced("GetPersistentTableIfSymbol"_)("input"_)),
        {"HoldAll"_});

    DefineFunction(
        "Select"_, {"Pattern"_("input"_, "Blank"_()), "Pattern"_("predicate"_, "Blank"_())},
        "Select"_(namespaced("GetPersistentTableIfSymbol"_)("input"_), "predicate"_), {"HoldAll"_});

    DefineFunction(
        "Group"_,
        {"Pattern"_("inputName"_, "Blank"_()), "Pattern"_("groupFunction"_, "Blank"_()),
         "Pattern"_("aggregateFunctions"_, "BlankSequence"_())},
        "With"_(
            "List"_("Set"_("input"_, namespaced("GetPersistentTableIfSymbol"_)("inputName"_))),

            "Values"_("GroupBy"_(
                "input"_, "groupFunction"_,
                "Function"_(
                    "groupedInput"_,
                    "Merge"_(
                        "Map"_(
                            "Function"_(
                                "aggregateFunction"_,
                                "Construct"_(
                                    "Switch"_(
                                        "aggregateFunction"_, namespaced("Count"_),
                                        "Composition"_(
                                            "Association"_,
                                            "Construct"_("CurryApplied"_("Rule"_, 2), "Count"_),
                                            "Length"_),
                                        "Blank"_(),
                                        "Composition"_("Fold"_("Plus"_),
                                                       "Apply"_("KeyTake"_, "aggregateFunction"_))),
                                    "groupedInput"_)),
                            "List"_("aggregateFunctions"_)),
                        "First"_))))));

    DefineFunction(
        "Group"_, {"Pattern"_("input"_, "Blank"_()), "Pattern"_("aggregateFunction"_, "Blank"_())},
        namespaced("Group"_)("input"_, "Function"_(0), "aggregateFunction"_), {"HoldAll"_});

    DefineFunction("Order"_,
                   {"Pattern"_("input"_, "Blank"_()), "Pattern"_("orderFunction"_, "Blank"_())},
                   "SortBy"_("input"_, "orderFunction"_), {"HoldAll"_});

    DefineFunction("Top"_,
                   {"Pattern"_("input"_, "Blank"_()), "Pattern"_("orderFunction"_, "Blank"_()),
                    "Pattern"_("number"_, "Blank"_("Integer"_))},
                   "TakeSmallestBy"_("input"_, "orderFunction"_, "number"_), {"HoldAll"_});

    DefineFunction(
        "Join"_,
        {"Pattern"_("left"_, "Blank"_()), "Pattern"_("right"_, "Blank"_()),
         "Pattern"_("predicate"_, "Blank"_("Function"_))},
        "Select"_("Flatten"_("Outer"_("Composition"_("Merge"_("First"_), "List"_),
                                      namespaced("GetPersistentTableIfSymbol"_)("left"_),
                                      namespaced("GetPersistentTableIfSymbol"_)("right"_), 1),
                             1),
                  "predicate"_));
  }

  void loadDDLOperators() {
    DefineFunction(
        "CreateTable"_,
        {"Pattern"_("relation"_, "Blank"_()), "Pattern"_("attributes"_, "BlankSequence"_())},
        "CompoundExpression"_("Set"_("Database"_("relation"_), "List"_()),
                              "Set"_("Schema"_("relation"_), "List"_("attributes"_))),
        {"HoldFirst"_});

    DefineFunction(
        "InsertInto"_,
        {"Pattern"_("relation"_, "Blank"_()), "Pattern"_("tuple"_, "BlankSequence"_())},
        "CompoundExpression"_("AppendTo"_("Database"_("relation"_),
                                          "Association"_("Thread"_(
                                              "Rule"_("Schema"_("relation"_), "List"_("tuple"_))))),
                              "Null"_),
        {"HoldFirst"_});
  }

  void loadShimLayer() {
    evalWithoutNamespace("Set"_("BOSSVersion"_, 1));

    for(std::string const& it :
        vector{"Plus", "Minus", "Length", "Times", "And", "UnixTime", "StringJoin", "Greater",
               "Symbol", "UndefinedFunction", "Evaluate", "Set", "SortBy", "Values", "List", "Rule",
               "Equal", "Extract", "StringContainsQ"}) {
      evalWithoutNamespace("Set"_(namespaced(Symbol(it)), Symbol("System`" + it)));
    }

    DefineFunction("Function"_,
                   {"Pattern"_("arg"_, "Blank"_()), "Pattern"_("definition"_, "Blank"_())},
                   "Function"_("arg"_, "definition"_), {"HoldRest"_});
    DefineFunction("Function"_, {"Pattern"_("definition"_, "Blank"_())}, "Function"_("definition"_),
                   {"HoldRest"_});

    DefineFunction("Return"_, {"Pattern"_("result"_, "Blank"_("List"_))},
                   "Map"_("Function"_("x"_, "If"_("MatchQ"_("x"_, "Blank"_("Association"_)),
                                                  "Values"_("x"_), "x"_)),
                          "result"_));
    DefineFunction("Return"_, {"Pattern"_("result"_, "Blank"_())}, "result"_);

    loadDDLOperators();

    loadRelationalOperators();
  };

  EngineImplementation() {
    environment = WSInitialize(nullptr);
    if(environment == nullptr) {
      throw std::runtime_error("could not initialize wstp environment");
    }
    auto error = 0;
    link = WSOpenString(
        environment,
        "-linkmode launch -linkname \"" STRING(MATHEMATICA_KERNEL_EXECUTABLE) "\" -wstp", &error);
    if(error != 0) {
      throw std::runtime_error("could not open wstp link -- error code: " + to_string(error));
    }
  }

  EngineImplementation(EngineImplementation&&) = default;
  EngineImplementation(EngineImplementation const&) = delete;
  EngineImplementation& operator=(EngineImplementation&&) = default;
  EngineImplementation& operator=(EngineImplementation const&) = delete;

  ~EngineImplementation() {
    WSClose(link);
    WSDeinitialize(environment);
  }

  Expression evaluate(Expression const& e,
                      std::string const& namespaceIdentifier = DefaultNamespace,
                      std::ostream& console =
#ifdef NDEBUG
                          noOpConsole
#else
                          std::cout
#endif // NDEBUG
  ) {
    putExpressionOnLink("Return"_(e), namespaceIdentifier, console);
    console << endl;
    WSEndPacket(link);
    int pkt = 0;
    while(((pkt = WSNextPacket(link)) != 0) && (pkt != RETURNPKT)) {
      WSNewPacket(link);
    }
    return readExpressionFromLink();
  }
};

Engine::Engine() : impl([]() -> EngineImplementation& { return *(new EngineImplementation()); }()) {
  impl.loadShimLayer();
}
Engine::~Engine() { delete &impl; }

Expression Engine::evaluate(Expression const& e) { return impl.evaluate(e); }
} // namespace boss::engines::wolfram

#endif // WSINTERFACE
