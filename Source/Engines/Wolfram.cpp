#ifdef WSINTERFACE
#include "Wolfram.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#define STRINGIFY(x) #x
#define STRING(x) STRINGIFY(x)

namespace boss::engines::wolfram {
using namespace std;

template <class... Fs> struct overload : Fs... {
  template <class... Ts> explicit overload(Ts&&... ts) : Fs{std::forward<Ts>(ts)}... {}
  using Fs::operator()...;
};

template <class... Ts> overload(Ts&&...) -> overload<std::remove_reference_t<Ts>...>;

struct EngineImplementation {

  static void putExpressionOnLink(Engine& e, Expression const& expression) {
    WSPutFunction(e.link, expression.getHead().c_str(), expression.getArguments().size());
    for(const auto& argument : expression.getArguments()) {
      std::visit(
          overload([&](int a) { WSPutInteger(e.link, a); },
                   [&](char const* a) { WSPutString(e.link, a); },
                   [&](Expression::Symbol const& a) { WSPutSymbol(e.link, a.getName().c_str()); },
                   [&](std::string const& a) { WSPutString(e.link, a.c_str()); },
                   [&](Expression const& expression) { putExpressionOnLink(e, expression); }),
          argument);
    }
  }

  static Expression::ReturnType readExpressionFromLink(Engine& e) {
    auto resultType = WSGetType(e.link);
    if(resultType == WSTKSTR) {
      char const* result = nullptr;
      WSGetString(e.link, &result);
      return std::string(result);
    } else if(resultType == WSTKINT) {
      int result = 0;
      WSGetInteger(e.link, &result);
      return result;
    } else if(resultType == WSTKFUNC) {
      auto const* resultHead = "";
      auto numberOfArguments = 0;
      WSGetFunction(e.link, &resultHead, &numberOfArguments);
      auto resultArguments = vector<Expression::ArgumentType>();
      for(auto i = 0U; i < numberOfArguments; i++) {
        resultArguments.push_back(readExpressionFromLink(e));
      }

      return Expression(resultHead, resultArguments);
    } else if(resultType == WSTKSYM) {
      char const* result = nullptr;
      WSGetSymbol(e.link, &result);
      if(std::string("True") == result) {
        return true;
      }
      if(std::string("False") == result) {
        return false;
      }
      return Expression::Symbol(result);
    }
    throw std::logic_error("unsupported return type: " + std::to_string(resultType));
  }
};

Engine::Engine() {
  environment = WSInitialize(nullptr);
  if(environment == nullptr) {
    throw std::runtime_error("could not initialize wstp environment");
  }
  auto error = 0;
  link = WSOpenString(environment,
                      "-linkmode launch -linkname " STRING(MATHEMATICA_KERNEL_EXECUTABLE) " -wstp",
                      &error);
  if(error != 0) {
    throw std::runtime_error("could not open wstp link -- error code: " + to_string(error));
  }
}
Engine::~Engine() {
  WSClose(link);
  WSDeinitialize(environment);
}

Expression::ReturnType Engine::evaluate(Expression const& e) {
  EngineImplementation::putExpressionOnLink(*this, e);

  WSEndPacket(link);
  int pkt = 0;
  while((pkt = WSNextPacket(link)) && (pkt != RETURNPKT)) {
    WSNewPacket(link);
  }
  return EngineImplementation::readExpressionFromLink(*this);
}
} // namespace boss::engines::wolfram

#endif // WSINTERFACE
