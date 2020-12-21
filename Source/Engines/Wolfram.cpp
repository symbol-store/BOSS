#ifdef WSINTERFACE
#include "Wolfram.hpp"
#include "../Utilities.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#define STRINGIFY(x) #x        // NOLINT
#define STRING(x) STRINGIFY(x) // NOLINT

namespace boss::engines::wolfram {
using boss::utilities::overload;
using std::string;
using std::to_string;
using std::vector;
using boss::utilities::operator""_;

struct EngineImplementation {
  constexpr static char const* const DefaultNamespace = "BOSS`";
  WSENV environment = {};
  WSLINK link = {};

  void putExpressionOnLink(Expression const& expression, std::string namespaceIdentifier) {
    WSPutFunction(link, (namespaceIdentifier + expression.getHead()).c_str(),
                  expression.getArguments().size());
    for(auto const& argument : expression.getArguments()) {
      std::visit(overload([&](int a) { WSPutInteger(link, a); },
                          [&](char const* a) { WSPutString(link, a); },
                          [&](Expression::Symbol const& a) {
                            WSPutSymbol(link, (namespaceIdentifier + a.getName()).c_str());
                          },
                          [&](std::string const& a) { WSPutString(link, a.c_str()); },
                          [&](Expression const& expression) {
                            putExpressionOnLink(expression, namespaceIdentifier);
                          }),
                 argument);
    }
  }

  Expression::ReturnType readExpressionFromLink() {
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
      WSGetFunction(link, &resultHead, &numberOfArguments);
      auto resultArguments = vector<Expression::ArgumentType>();
      for(auto i = 0U; i < numberOfArguments; i++) {
        resultArguments.push_back(readExpressionFromLink());
      }
      auto result = Expression(resultHead, resultArguments);
      WSReleaseSymbol(link, resultHead);
      return result;
    }
    if(resultType == WSTKSYM) {
      char const* result = nullptr;
      WSGetSymbol(link, &result);
      auto resultingSymbol = Expression::Symbol(result);
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

  static Expression::Symbol namespaced(Expression::Symbol const& name) {
    return Expression::Symbol(DefaultNamespace + name.getName());
  }
  static Expression namespaced(Expression const& name) {
    return Expression(DefaultNamespace + name.getHead(), name.getArguments());
  }

  void loadShimLayer() {
    auto Set = "Set"_;
    auto SetDelayed = "SetDelayed"_;
    auto Function = "Function"_;
    auto List = "List"_;
    auto eval = [this](Expression const& expression) { return evaluate(expression, ""); };
    for(std::string const& it :
        vector{"Plus", "StringJoin", "Greater", "Symbol", "UndefinedFunction", "Evaluate", "Set",
               "List", "Extract", "Function", "StringContainsQ"}) {
      eval(Set(namespaced(Expression::Symbol(it)), Expression::Symbol("System`" + it)));
    }
    eval(SetDelayed(namespaced("CreateTable"_("Pattern"_("relation"_, "Blank"_()),
                                              "Pattern"_("attributes"_, "BlankSequence"_()))),
                    Set("relation"_, List())));
    eval("SetAttributes"_(namespaced("CreateTable"_), "HoldFirst"_));

    eval(SetDelayed(namespaced("InsertInto"_("Pattern"_("relation"_, "Blank"_()),
                                             "Pattern"_("tuple"_, "BlankSequence"_()))),
                    "AppendTo"_("relation"_, "List"_("tuple"_))));
    eval("SetAttributes"_(namespaced("InsertInto"_), "HoldFirst"_));

    eval(SetDelayed(namespaced("Project"_("Pattern"_("input"_, "Blank"_()),
                                          "Pattern"_("projection"_, "Blank"_()))),
                    "Map"_("projection"_, "relation"_)));
    eval(SetDelayed(namespaced("Select"_("Pattern"_("input"_, "Blank"_()),
                                          "Pattern"_("predicate"_, "Blank"_()))),
                    "Select"_("input"_, "predicate"_)));
    eval(SetDelayed(namespaced("GroupBy"_("Pattern"_("input"_, "Blank"_()),
                                          "Pattern"_("groupFunction"_, "Blank"_()),
                                          "Pattern"_("aggregateFunction"_, "Blank"_()))),
                    "Length"_("input"_)));
    eval("Set"_("BOSSVersion"_, 1));
  };

  EngineImplementation() {
    environment = WSInitialize(nullptr);
    if(environment == nullptr) {
      throw std::runtime_error("could not initialize wstp environment");
    }
    auto error = 0;
    link = WSOpenString(
        environment, "-linkmode launch -linkname " STRING(MATHEMATICA_KERNEL_EXECUTABLE) " -wstp",
        &error);
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

  Expression::ReturnType evaluate(Expression const& e,
                                  std::string const& namespaceIdentifier = DefaultNamespace) {
    putExpressionOnLink(e, namespaceIdentifier);
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

Expression::ReturnType Engine::evaluate(Expression const& e) { return impl.evaluate(e); }
} // namespace boss::engines::wolfram

#endif // WSINTERFACE
