#include <cstring>
#include <spdlog/common.h>
#ifdef WSINTERFACE
#include "../BOSS.hpp"
#include "../ExpressionUtilities.hpp"
#include "Wolfram.hpp"
#include "spdlog/cfg/env.h"
#include "spdlog/sinks/basic_file_sink.h"
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
using ExpressionBuilder = boss::utilities::ExtensibleExpressionBuilder<WolframExpressionSystem>;
static ExpressionBuilder operator""_(const char* name, size_t /*unused*/) {
  return ExpressionBuilder(name);
};

using std::set;
using std::string;
using std::to_string;
using std::vector;
using std::string_literals::operator""s;
using std::endl;

static class WolframLogStream {
public:
  WolframLogStream() { spdlog::cfg::load_env_levels(); }

  template <typename T> WolframLogStream& operator<<(T unused) {
    str << unused;
    return *this;
  }
  WolframLogStream& operator<<(std::ostream& (*/*pf*/)(std::ostream&)) {
    logger().trace(str.str());
    str.str("");
    return *this;
  };

private:
  static spdlog::logger& logger() {
    static auto instance = spdlog::basic_logger_mt("console", "wolframlog.m");
    static std::once_flag flag;
    std::call_once(flag, [&] { instance->sinks()[0]->set_pattern("%v"); });
    return *instance;
  }

  std::stringstream str;

} console; // NOLINT

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

  void putExpressionOnLink(Expression const& expression, std::string const& namespaceIdentifier) {
    std::visit(boss::utilities::overload(
                   [&](bool a) {
                     console << (a ? "True" : "False");
                     WSPutSymbol(link, (a ? "True" : "False"));
                   },
                   [&](int a) {
                     console << a;
                     WSPutInteger(link, a);
                   },
                   [&](std::vector<int> values) {
                     putExpressionOnLink(ComplexExpression("List"_, {values.begin(), values.end()}),
                                         namespaceIdentifier);
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
                         (unnamespacedSymbols.count(normalizedName) > 0 ? ""
                                                                        : namespaceIdentifier) +
                         normalizedName;
                     console << namespaced;
                     WSPutSymbol(link, namespaced.c_str());
                   },
                   [&](std::string const& a) {
                     console << "\"" << a << "\"";
                     WSPutString(link, a.c_str());
                   },
                   [&](ComplexExpression const& expression) {
                     auto headName = namespaceIdentifier + expression.getHead().getName();
                     if(headName == namespaceIdentifier + "list") {
                       headName = "List";
                     }
                     console << (headName) << "[";
                     WSPutFunction(link, (headName).c_str(), (int)expression.getArguments().size());
                     for(auto it = expression.getArguments().begin();
                         it != expression.getArguments().end(); ++it) {
                       auto const& argument = *it;
                       if(it != expression.getArguments().begin()) {
                         console << ", ";
                       }
                       putExpressionOnLink(argument, namespaceIdentifier);
                     }
                     console << "]";
                   }),
               (Expression::SuperType const&)expression);
  }

  boss::Expression readExpressionFromLink() const {
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
    if(resultType == WSTKREAL) {
      float result = 0;
      WSGetFloat(link, &result);
      return result;
    }
    if(resultType == WSTKFUNC) {
      auto const* resultHead = "";
      auto numberOfArguments = 0;
      auto success = WSGetFunction(link, &resultHead, &numberOfArguments);
      if(success == 0) {
        throw std::runtime_error("error when getting function "s + WSErrorMessage(link));
      }
      auto resultArguments = vector<boss::Expression>();
      for(auto i = 0U; i < numberOfArguments; i++) {
        resultArguments.push_back(readExpressionFromLink());
      }
      auto result =
          boss::ComplexExpression(boss::Symbol(removeNamespace(resultHead)), resultArguments);
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

  static ExpressionBuilder namespaced(ExpressionBuilder const& builder) {
    return ExpressionBuilder(Symbol(DefaultNamespace + Symbol(builder).getName()));
  }
  static auto namespaced(Symbol const& name) {
    return ExpressionBuilder(Symbol(DefaultNamespace + name.getName()));
  }
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
                   "Function"_("tuple"_, "ReplaceAll"_("Unevaluated"_("condition"_), "tuple"_)),
                   {"HoldFirst"_});

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

    DefineFunction(
        "GetPersistentTableIfSymbol"_, {"Pattern"_("input"_, "Blank"_("Symbol"_))},
        "If"_("Greater"_("Length"_("Database"_("input"_)), 0),
              "MovingMap"_(
                  "Function"_("Association"_("ReplaceAll"_(
                      "Normal"_("Part"_("Slot"_(1), 2)),
                      "RuleDelayed"_(
                          "HoldPattern"_(
                              "Rule"_("Pattern"_("attribute"_, "Blank"_()),
                                      namespaced("Interpolate"_)("Pattern"_("by"_, "Blank"_())))),
                          "Rule"_("attribute"_,
                                  "Divide"_("Total"_("Map"_("Extract"_("Key"_("attribute"_)),
                                                            ("Drop"_("Slot"_(1), "List"_(2))))),
                                            2)))))),
                  "Database"_("input"_), "List"_(2, "Center"_, "Automatic"_), "Fixed"),
              "Database"_("input"_)));
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

            "KeyValueMap"_(
                "Function"_(
                    "List"_("groupkey"_, "groupresult"_),
                    "Append"_("groupresult"_,
                              "Thread"_("Rule"_(
                                  "Quiet"_("Check"_("Extract"_("groupFunction"_, "List"_(2, 1)),
                                                    "Unique"_("groupKey"_))),
                                  "groupkey"_)))),
                "GroupBy"_(
                    "input"_, "groupFunction"_,
                    "Function"_(
                        "groupedInput"_,
                        "Merge"_(
                            "Map"_(
                                "Function"_(
                                    "aggregateFunction"_,
                                    "Construct"_(
                                        "Switch"_("aggregateFunction"_, namespaced("Count"_),
                                                  "Composition"_(
                                                      "Association"_,
                                                      "Construct"_("CurryApplied"_("Rule"_, 2),
                                                                   "Count"_),
                                                      "Length"_),
                                                  "Blank"_(),
                                                  "Composition"_(
                                                      "Fold"_("Plus"_),
                                                      "Apply"_("KeyTake"_, "aggregateFunction"_))),
                                        "groupedInput"_)),
                                "List"_("aggregateFunctions"_)),
                            "First"_))))));

    DefineFunction(
        "Group"_,
        {"Pattern"_("inputName"_, "Blank"_()), "Pattern"_("aggregateFunctions"_, "Blank"_())},
        "With"_("List"_("Set"_("input"_, namespaced("GetPersistentTableIfSymbol"_)("inputName"_))),
                "List"_("Merge"_(
                    "Map"_("Function"_(
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
                                   "input"_)),
                           "List"_("aggregateFunctions"_)),
                    "First"_))),
        {"HoldAll"_});

    DefineFunction("Order"_,
                   {"Pattern"_("input"_, "Blank"_()), "Pattern"_("orderFunction"_, "Blank"_())},
                   "SortBy"_(namespaced("GetPersistentTableIfSymbol"_)("input"_), "orderFunction"_),
                   {"HoldAll"_});

    DefineFunction("Top"_,
                   {"Pattern"_("input"_, "Blank"_()), "Pattern"_("orderFunction"_, "Blank"_()),
                    "Pattern"_("number"_, "Blank"_("Integer"_))},
                   "MinimalBy"_(namespaced("GetPersistentTableIfSymbol"_)("input"_),
                                "orderFunction"_, "UpTo"_("number"_)),
                   {"HoldAll"_});

    DefineFunction(
        "Join"_,
        {"Pattern"_("left"_, "Blank"_()), "Pattern"_("right"_, "Blank"_()),
         "Pattern"_("predicate"_, "Blank"_("Function"_))},
        "Select"_("Flatten"_("Outer"_("Composition"_("Merge"_("First"_), "List"_),
                                      namespaced("GetPersistentTableIfSymbol"_)("left"_),
                                      namespaced("GetPersistentTableIfSymbol"_)("right"_), 1),
                             1),
                  "predicate"_));

    DefineFunction(
        "Join"_,
        {"Pattern"_("leftInput"_, "Blank"_()), "Pattern"_("rightInput"_, "Blank"_()),
         namespaced("Where"_)(namespaced("Equal"_)("Pattern"_("leftAttribute"_, "Blank"_()),
                                                   "Pattern"_("rightAttribute"_, "Blank"_())))},
        "With"_(
            "List"_("Set"_("ht"_, "CreateDataStructure"_("HashTable"))),
            "CompoundExpression"_(
                "Map"_("Function"_(
                           "buildTuple"_,
                           "ht"_("Insert", "Rule"_("ReplaceAll"_("leftAttribute"_, "buildTuple"_),
                                                   "Append"_("ht"_("Lookup",
                                                                   "ReplaceAll"_("leftAttribute"_,
                                                                                 "buildTuple"_),
                                                                   "Function"_("List"_())),
                                                             "buildTuple"_)))),
                       namespaced("GetPersistentTableIfSymbol"_)("leftInput"_)),
                "Flatten"_(
                    "Map"_(
                        "Function"_(
                            "probeTuple"_,
                            "Map"_("Function"_(
                                       "buildTuple"_,
                                       "Merge"_("List"_("probeTuple"_, "buildTuple"_), "First"_)),
                                   "ht"_("Lookup", "ReplaceAll"_("rightAttribute"_, "probeTuple"_),
                                         "Function"_("List"_())))),
                        namespaced("GetPersistentTableIfSymbol"_)("rightInput"_)),
                    1))));
  }

  void loadDataLoadingOperators() {
    DefineFunction(
        "Load"_, {"Pattern"_("relation"_, "Blank"_()), "Pattern"_("from"_, "Blank"_("String"_))},
        "CompoundExpression"_(
            "Set"_("Database"_("relation"_),
                   "Map"_("Function"_("tuple"_,
                                      "Association"_("Thread"_("Rule"_(
                                          "Map"_("First"_, "Schema"_("relation"_)), "tuple"_)))),
                          "Normal"_("SemanticImport"_(
                              "from"_,
                              "Map"_("Function"_(
                                         "type"_,
                                         "Replace"_("Extract"_("type"_, 2),
                                                    "List"_( //
                                                        "Rule"_(namespaced("INTEGER"_), "Integer"),
                                                        "Rule"_(namespaced("CHAR"_), "String"),
                                                        "Rule"_(namespaced("VARCHAR"_), "String"),
                                                        "Rule"_(namespaced("DECIMAL"_), "Number"),
                                                        "Rule"_(namespaced("DATE"_), "Date")))),
                                     "Schema"_("relation"_)),
                              "Rule"_("Delimiters", "List"_("|")), "Rule"_("HeaderLines"_, 0))))),
            "Null"_));
  }

  void loadDDLOperators() {
    DefineFunction(
        "CreateTable"_,
        {"Pattern"_("relation"_, "Blank"_()), "Pattern"_("attributes"_, "BlankSequence"_())},
        "CompoundExpression"_(
            "Set"_("Database"_("relation"_), "List"_()),
            "Set"_("Schema"_("relation"_),
                   "Map"_("Function"_("a"_, "If"_("SameQ"_(namespaced("List"_), "Head"_("a"_)),
                                                  "a"_, "List"_("a"_))),
                          "List"_("attributes"_)))),
        {"HoldFirst"_});

    DefineFunction(
        "InsertInto"_,
        {"Pattern"_("relation"_, "Blank"_()), "Pattern"_("tuple"_, "BlankSequence"_())},
        "CompoundExpression"_(
            "AppendTo"_("Database"_("relation"_),
                        "Association"_("Thread"_(
                            "Rule"_("Map"_("First"_, "Schema"_("relation"_)), "List"_("tuple"_))))),
            "Null"_),
        {"HoldFirst"_});
  }

  void loadSymbolicOperators() {
    DefineFunction(
        "Assuming"_,
        {"Pattern"_("input"_, "Blank"_()), "Pattern"_("assumptions"_, "BlankSequence"_())},
        "ReplaceAll"_(namespaced("GetPersistentTableIfSymbol"_)("input"_),
                      "Rule"_("First"_("assumptions"_), "Last"_("assumptions"_))),
        {"HoldAll"_});
  }

  void loadShimLayer() {
    evalWithoutNamespace("Set"_("BOSSVersion"_, 1));

    for(std::string const& it : vector{
            //
            "And",
            "Apply",
            "DateObject",
            "Equal",
            "Evaluate",
            "Extract",
            "Greater",
            "Length",
            "List",
            "Minus",
            "Plus",
            "Rule",
            "Set",
            "SortBy",
            "StringContainsQ",
            "StringJoin",
            "Symbol",
            "Times",
            "UndefinedFunction",
            "UnixTime",
            "Values",
            //
        }) {
      evalWithoutNamespace("Set"_(namespaced(Symbol(it)), Symbol("System`" + it)));
    }
    evalWithoutNamespace("Set"_(namespaced("Date"_), "System`DateObject"_));
    evalWithoutNamespace("Set"_(namespaced("StringContains"_), "System`StringContainsQ"_));

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

    DefineFunction("Schema"_, {"Pattern"_("result"_, "Blank"_("List"_))},
                   "If"_("Greater"_("Length"_("result"_), 0),
                         "Map"_("Composition"_("List"_), "Keys"_("First"_("result"_))), "List"_()));

    loadDDLOperators();

    loadRelationalOperators();
    loadDataLoadingOperators();
    loadSymbolicOperators();
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

  boss::Expression evaluate(Expression const& e,
                            std::string const& namespaceIdentifier = DefaultNamespace) {
    putExpressionOnLink("Return"_(e), namespaceIdentifier);
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

boss::Expression Engine::evaluate(Expression const& e) { return impl.evaluate(e); }
} // namespace boss::engines::wolfram

extern "C" BOSSExpression* evaluate(BOSSExpression* e) {
  static std::mutex m;
  std::lock_guard lock(m);
  static auto engine = boss::engines::wolfram::Engine();
  auto* r = new BOSSExpression{.delegate = engine.evaluate(e->delegate)};
  return r;
};

#endif // WSINTERFACE
