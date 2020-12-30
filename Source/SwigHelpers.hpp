#pragma once
#include "BOSS.hpp"
#include "Expression.hpp"
#include "Utilities.hpp"

using boss::Expression;

enum EngineImplementation {
  BulkEngine,
  WolframEngine,
};

EngineImplementation& currentEngine() {
  static EngineImplementation type = BulkEngine; // default
  return type;
}

void setEngine(EngineImplementation type) { currentEngine() = type; }

boss::engines::bulk::Engine& bulkEngine() {
  static auto instance = boss::engines::bulk::Engine();
  return instance;
}

#ifdef WSINTERFACE
boss::engines::wolfram::Engine& wolframEngine() {
  static auto instance = boss::engines::wolfram::Engine();
  return instance;
}
#endif // WSINTERFACE

Expression evaluate(Expression const& arg) {
  switch(currentEngine()) {
  case BulkEngine:
    return bulkEngine().evaluate(arg);
#ifdef WSINTERFACE
  case WolframEngine:
    return wolframEngine().evaluate(arg);
#endif // WSINTERFACE
  }
  return arg; // do nothing
}

Expression Symbol(std::string const& symbolName) { return boss::Symbol(symbolName); }
