#include "Bulk.hpp"

#include "Bulk/Batch/Batch.hpp"
#include "Bulk/Batch/ExpressionBatch.hpp"
#include "Bulk/Batch/RLEBatch.hpp"
#include "Bulk/Batch/SymbolBatch.hpp"
#include "Bulk/Batch/ValueBatch.hpp"

namespace boss::engines::bulk {

Expression Engine::evaluate(Expression const& e) {
  m_dispatcher.insert(e);
  auto output = m_dispatcher.evaluate();
  if(output.empty()) {
    return e;
  }

  auto *outputBatch = output[0].get();

  m_dispatcher.clear();

  // TODO: templatise it
  if(outputBatch->typeId() == UniqueId::forType<SymbolBatch>()) {
    return (Symbol)*static_cast<SymbolBatch*>(outputBatch)->begin();
  } else if(outputBatch->typeId() == UniqueId::forType<ValueBatch<bool>>()) {
    return (bool)*static_cast<ValueBatch<bool>*>(outputBatch)->begin();
  } else if(outputBatch->typeId() == UniqueId::forType<RLEBatch<bool>>()) {
    return (bool)*static_cast<RLEBatch<bool>*>(outputBatch)->begin();
  } else if(outputBatch->typeId() == UniqueId::forType<ValueBatch<int>>()) {
    return *static_cast<ValueBatch<int>*>(outputBatch)->begin();
  } else if(outputBatch->typeId() == UniqueId::forType<RLEBatch<int>>()) {
    return *static_cast<RLEBatch<int>*>(outputBatch)->begin();
  } else if(outputBatch->typeId() == UniqueId::forType<ValueBatch<float>>()) {
    return *static_cast<ValueBatch<float>*>(outputBatch)->begin();
  } else if(outputBatch->typeId() == UniqueId::forType<RLEBatch<float>>()) {
    return *static_cast<RLEBatch<float>*>(outputBatch)->begin();
  } else if(outputBatch->typeId() == UniqueId::forType<ValueBatch<std::string>>()) {
    return *static_cast<ValueBatch<std::string>*>(outputBatch)->begin();
  } else if(outputBatch->typeId() == UniqueId::forType<RLEBatch<std::string>>()) {
    return *static_cast<RLEBatch<std::string>*>(outputBatch)->begin();
  } else {
    return e;
  }
}

} // namespace boss::engines::bulk
