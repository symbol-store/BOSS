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
using std::string_literals::operator""s;
using std::endl;
#ifdef NDEBUG
struct NOOPConsole {
  template <typename T> NOOPConsole const& operator<<(T /*unused*/) const { return *this; }
  NOOPConsole const& operator<<(std::ostream& (*/*pf*/)(std::ostream&)) const { return *this; };

} const console;
#else
std::ostream& console = std::cout; // NOLINT
#endif // NDEBUG

struct EngineImplementation {
  constexpr static char const* const DefaultNamespace = "BOSS`";
  WSENV environment = {};
  WSLINK link = {};

  void putExpressionOnLink(Expression const& expression, std::string namespaceIdentifier) {
    std::visit(overload(
                   [&](int a) {
                     console << a;
                     WSPutInteger(link, a);
                   },
                   [&](char const* a) {
                     console << a;
                     WSPutString(link, a);
                   },
                   [&](Symbol const& a) {
                     console << (namespaceIdentifier + a.getName());
                     WSPutSymbol(link, (namespaceIdentifier + a.getName()).c_str());
                   },
                   [&](std::string const& a) {
                     console << "\"" << a << "\"";
                     WSPutString(link, a.c_str());
                   },
                   [&](ComplexExpression const& expression) {
                     console << (namespaceIdentifier + expression.getHead().getName()) << "[";
                     WSPutFunction(link,
                                   (namespaceIdentifier + expression.getHead().getName()).c_str(),
                                   expression.getArguments().size());
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
      WSGetFunction(link, &resultHead, &numberOfArguments);
      auto resultArguments = vector<Expression>();
      for(auto i = 0U; i < numberOfArguments; i++) {
        resultArguments.push_back(readExpressionFromLink());
      }
      auto result = ComplexExpression(Symbol(resultHead), resultArguments);
      WSReleaseSymbol(link, resultHead);
      return result;
    }
    if(resultType == WSTKSYM) {
      char const* result = nullptr;
      WSGetSymbol(link, &result);
      auto resultingSymbol = Symbol(result);
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

  static Symbol namespaced(Symbol const& name) { return Symbol(DefaultNamespace + name.getName()); }
  static ComplexExpression namespaced(ComplexExpression const& name) {
    return ComplexExpression(Symbol(DefaultNamespace + name.getHead().getName()),
                             name.getArguments());
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
      eval(Set(namespaced(Symbol(it)), Symbol("System`" + it)));
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

  Expression evaluate(Expression const& e,
                      std::string const& namespaceIdentifier = DefaultNamespace) {
    putExpressionOnLink(e, namespaceIdentifier);
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
