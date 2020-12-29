#pragma once

#include "../BatchFactory.hpp"
#include "../SymbolPool.hpp"
#include "RLEBatch.hpp"

#include "../../../Expression.hpp"

#include <optional>

namespace boss::engines::bulk {

class SymbolWrapper {
public:
  SymbolWrapper() = default;
  SymbolWrapper(Symbol const& symbol) : m_symbol(symbol) {}
  SymbolWrapper(SymbolWrapper const& other) : m_symbol(other.m_symbol) {}
  SymbolWrapper(SymbolWrapper&& other) : m_symbol(std::move(other.m_symbol)) {}

  SymbolWrapper const& operator=(SymbolWrapper const& other) {
    m_symbol.reset();
    if(other.m_symbol) {
      m_symbol.emplace(*other.m_symbol);
    }
    return *this;
  }

  operator bool() { return m_symbol.has_value(); }
  operator Symbol() { return *m_symbol; }
  std::string const& getName() { return m_symbol->getName(); }

private:
  std::optional<Symbol> m_symbol;
};

class SymbolBatch : public RLEBatch<SymbolWrapper> {
public:
  using ValueType = Symbol; // still pretend to be a Symbol
  static constexpr UniqueId::type UniqueId = UniqueId::forType<SymbolBatch>();

  SymbolBatch() = default;
  SymbolBatch(SymbolBatch const& other) : RLEBatch<SymbolWrapper>(other.m_value) {}
  SymbolBatch(Symbol const& symbol) : RLEBatch<SymbolWrapper>(symbol) {}
  SymbolBatch(Symbol&& symbol) : RLEBatch<SymbolWrapper>(std::move(symbol)) {}

  Batch* clone() override { return new SymbolBatch(*this); }

  UniqueId::type typeId() const override { return UniqueId; }
  UniqueId::type evaluatedTypeId() const override { return UniqueId; } // TODO
  UniqueId::type elementTypeId() const override { return UniqueId::forType<Symbol>(); }

  using RLE = std::bool_constant<true>;
  bool isRLE() const override { return RLE::value; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<Symbol>(val);
  }

  Batch* evaluate(BatchFactory const& factory) override {
    if(!m_value) {
      return this->clone();
    }    
    auto returnValue = SymbolPool<>::instance().evaluateSymbol(m_value.getName());
    return std::visit(
        [this, &factory](auto&& arg) -> Batch* {
          using Type = std::decay_t<decltype(arg)>;
          if constexpr(std::is_same_v<Type, Symbol>) {
            if(arg.getName() == m_value.getName()) {
              return this->clone();
            } else {
              return new SymbolBatch(arg);
            }
          } else if constexpr(std::is_same_v<Type, ComplexExpression>) {
            auto* batch = factory.createBatch(arg);
            batch->insert(arg);
            return batch;
          } else {
            return new RLEBatch<Type>(arg);
          }
        },
        returnValue);
  }
};

} // namespace boss::engines::bulk
