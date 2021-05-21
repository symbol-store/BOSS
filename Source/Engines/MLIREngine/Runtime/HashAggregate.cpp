#include "HashAggregate.hpp"
#include <variant>

boss::Expression runtime::aggregate::HashAggregate::getAggregate(size_t hash) {
  auto it = aggregates.find(hash);
  if (it == aggregates.end()) {
    aggregates[hash] = initialValue;
    return initialValue;
  }
  return it->second;
}

void runtime::aggregate::HashAggregate::updateAggregate(size_t hash, boss::Expression e) {
  aggregates[hash] = std::move(e);
}

boss::mlir::types::RuntimeTypes runtime::aggregate::HashAggregate::getAggregateType() {
  if (std::holds_alternative<size_t>(initialValue)) {
    return boss::mlir::types::RuntimeTypes::INT64;
  }
  if (std::holds_alternative<int>(initialValue)) {
    return boss::mlir::types::RuntimeTypes::INT;
  }
  if (std::holds_alternative<std::string>(initialValue)) {
    return boss::mlir::types::RuntimeTypes::STRING;
  }
  if (std::holds_alternative<boss::ComplexExpression>(initialValue)) {
    return boss::mlir::types::RuntimeTypes::SYMBOL;
  }
  if (std::holds_alternative<bool>(initialValue)) {
    return boss::mlir::types::RuntimeTypes::BOOLEAN;
  }
  return boss::mlir::types::RuntimeTypes::ERROR;
}

int runtime::aggregate::groupByGet_Int(size_t hash, runtime::aggregate::HashAggregate* aggregate) {
  return std::get<int>(aggregate->getAggregate(hash));
}

void runtime::aggregate::groupByInsert_Int(int value, size_t hash,
                                           runtime::aggregate::HashAggregate* aggregate) {
  aggregate->updateAggregate(hash, value);
}
