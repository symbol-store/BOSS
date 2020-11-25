#pragma once

#include <iostream>
#include <memory>
#include <variant>

namespace boss::engines::bulk {

constexpr bool AUTO_EVALUATION = false;
constexpr bool DEBUG_INTERPOLATION = false;

template <typename T> class Symbolic {
public:
  Symbolic(T value, bool missing = false) : m_value(value), m_missingValue(missing) {}
  operator T() const {
    if constexpr(AUTO_EVALUATION) {
      Evaluate();
    }
    return m_value;
  }

  virtual bool Evaluate(void const* view, size_t rowIndex) = 0;

  friend std::ostream& operator<<(std::ostream& os, Symbolic const& symbolic) {
    if(symbolic.m_missingValue) {
      os << "[MISS]";
    } else {
      os << "symb(" << symbolic.m_value << ")";
    }
    return os;
  }

protected:
  T m_value;
  bool m_missingValue;
};

template <> inline Symbolic<char const*>::operator char const *() const {
  return m_value ? m_value : "MISSING";
}

template <typename T> class MissingData : public Symbolic<T> {
public:
  MissingData() : Symbolic<T>(T(0), true) {}

  bool Evaluate(void const* view, size_t rowIndex) override { return false; }
};

template <typename T, typename S = Symbolic<T>, typename SPtr = std::shared_ptr<S>> class Data {
public:
  typedef T ValueType;
  typedef S SymbolicType;

  Data(T const& value) : m_data(value) {}

  template <typename DerivedFromS,
            std::enable_if_t<std::is_base_of<S, DerivedFromS>::value, int> = 0>
  Data(DerivedFromS const& symbol) : m_data(SPtr(new DerivedFromS(symbol))) {}

  T* getValue() { return std::get_if<T>(&m_data); }
  S* getSymbolic() { return std::get_if<SPtr>(&m_data)->get(); }

  bool isValue() const { return std::holds_alternative<T>(m_data); }
  bool isSymbolic() const { return std::holds_alternative<SPtr>(m_data); }

  T const* getValue() const { return std::get_if<T>(&m_data); }
  S const* getSymbolic() const { return std::get_if<SPtr>(&m_data)->get(); }

private:
  std::variant<T, SPtr> m_data;
};

} // namespace boss::engines::bulk
