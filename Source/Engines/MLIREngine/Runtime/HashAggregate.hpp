#pragma once
#include <unordered_map>
#include "Expression.hpp"
#include "Engines/MLIREngine/Types/Types.hpp"

namespace runtime::aggregate {

class HashAggregate {
public:

  explicit HashAggregate(boss::Expression initialValue): initialValue(initialValue) {}

  boss::Expression getAggregate(size_t hash);
  void updateAggregate(size_t hash, boss::Expression e);

  boss::mlir::types::RuntimeTypes getAggregateType();

  std::unordered_map<size_t, boss::Expression>::const_iterator begin() { return aggregates.begin(); }
  std::unordered_map<size_t, boss::Expression>::const_iterator end() { return aggregates.end(); }

private:
  std::unordered_map<size_t, boss::Expression> aggregates;

  boss::Expression initialValue;
};


extern "C" int groupByGet_Int(size_t hash, HashAggregate* aggregate);
extern "C" void groupByInsert_Int(int value, size_t hash, HashAggregate* aggregate);

}