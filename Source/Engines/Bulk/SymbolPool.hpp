#pragma once

#include "../../Expression.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace boss::engines::bulk {

template <typename... AdditionalTypes> class SymbolPool {
private:
  SymbolPool() = default;

public:
  ~SymbolPool() = default;
  static SymbolPool& instance() {
    static SymbolPool instance;
    return instance;
  }

  template <typename T, typename... Args> struct ExtendedArgumentType;
  template <typename... Args0, typename... Args1>
  struct ExtendedArgumentType<std::variant<Args0...>, Args1...> {
    using type = std::variant<Args0..., Args1...>;
  };

  using InternalArgumentType =
      typename ExtendedArgumentType<Expression::ArgumentType, AdditionalTypes...>::type;
  using InternalReturnType =
      typename ExtendedArgumentType<Expression::ReturnType, AdditionalTypes...>::type;

  using SupportedType =
      typename ExtendedArgumentType<Expression::ArgumentType, AdditionalTypes...>::type;

  class SymbolInternal {
  public:
    template <typename T> SymbolInternal(T const& value) : m_value(value) {}
    template <typename T> SymbolInternal(T&& value) : m_value(std::move(value)) {}

    Expression::ReturnType ToReturnType(std::string const& symbolName) const {
      return std::visit(
          [&symbolName](auto&& arg) -> Expression::ReturnType {
            using fromType = std::decay_t<decltype(arg)>;
            if constexpr(std::disjunction_v<std::is_same<fromType, AdditionalTypes>...>) {
              return Expression::Symbol(symbolName);
            } else {
              return arg;
            }
          },
          m_value);
    }

    SupportedType Value() const { return m_value; }

  private:
    SupportedType m_value;
  };

  template <typename T> SymbolInternal& registerSymbol(std::string const& symbol, T const& value) {
    auto& symbolPtr = m_symbolMap[symbol];
    symbolPtr = std::make_shared<SymbolInternal>(value);
    return *symbolPtr.get();
  }

  template <typename T> SymbolInternal& registerSymbol(std::string const& symbol, T&& value) {
    auto& symbolPtr = m_symbolMap[symbol];
    symbolPtr = std::make_shared<SymbolInternal>(std::move(value));
    return *symbolPtr.get();
  }

  Expression::ReturnType evaluateSymbol(std::string const& symbol) {
    auto& symbolPtr = m_symbolMap[symbol];
    return symbolPtr ? symbolPtr.get()->ToReturnType(symbol) : Expression::Symbol(symbol);
  }

private:
  using SymbolPtr = std::shared_ptr<SymbolInternal>;
  using SymbolMapping = std::unordered_map<std::string, SymbolPtr>;

  SymbolMapping m_symbolMap;
};

} // namespace boss::engines::bulk
