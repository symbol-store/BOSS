#pragma once
#include "BOSS.hpp"
#include "Expression.hpp"
#include "Serialization/JsonSchemaLoader.hpp"
#include "Serialization/TableDataLoader.hpp"

#ifndef SWIG
using boss::Expression;
#endif

enum EngineImplementation {
  NoEngine,
  BulkEngine,
  WolframEngine,
};

EngineImplementation& currentEngine() {
#ifdef WSINTERFACE
  static EngineImplementation type = WolframEngine; // default
#else
  static EngineImplementation type = NoEngine; // default
#endif // WSINTERFACE
  return type;
}

void setEngine(EngineImplementation type) { currentEngine() = type; }

#ifdef WSINTERFACE
boss::engines::wolfram::Engine& wolframEngine() {
  static auto instance = boss::engines::wolfram::Engine();
  return instance;
}
#endif // WSINTERFACE

Expression evaluate(Expression const& arg) {
  switch(currentEngine()) {
  case BulkEngine:
    break;
#ifdef WSINTERFACE
  case WolframEngine:
    return wolframEngine().evaluate(arg);
#endif // WSINTERFACE
  case NoEngine:
    break;
  }
  return arg; // do nothing
}

bool loadDatabaseSchema(std::string const& filepath) {
  boss::serialization::JsonSchemaLoader schemaLoader(filepath);
  switch(currentEngine()) {
  case BulkEngine:
    break;
#ifdef WSINTERFACE
  case WolframEngine:
    return schemaLoader.loadTables(wolframEngine());
#endif // WSINTERFACE
  case NoEngine:
    break;
  }
  return false;
}

bool loadTableData(std::string const& tableName, std::string const& filepath) {
  using boss::serialization::TableDataLoader;
  switch(currentEngine()) {
  case BulkEngine:
    break;
#ifdef WSINTERFACE
  case WolframEngine:
    return TableDataLoader::load(wolframEngine(), boss::Symbol(tableName), filepath);
#endif // WSINTERFACE
  case NoEngine:
    break;
  }
  return false;
}
