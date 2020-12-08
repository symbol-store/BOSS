#pragma once
#include "BOSS.hpp"

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

Expression::ReturnType evaluate(Expression::ArgumentType const& arg) {
  switch(currentEngine()) {
  case BulkEngine: {
    return bulkEngine().evaluate(arg);
  } break;

  case WolframEngine: {
#ifdef WSINTERFACE
    return wolframEngine().evaluate(arg);
#endif // WSINTERFACE
  } break;
  }

  return arg; // do nothing
}

bool loadDatabaseSchema(std::string const& filepath) {
  switch(currentEngine()) {
  case BulkEngine: {
    return bulkEngine().loadDatabaseSchema(filepath);
  } break;
  }

  return false;
}

bool loadTableData(std::string const& tableName, std::string const& filepath) {
  switch(currentEngine()) {
  case BulkEngine: {
    return bulkEngine().loadTableData(tableName, filepath);
  } break;
  }

  return false;
}
