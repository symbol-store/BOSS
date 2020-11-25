#pragma once

#include "SymbolicData/SymbolicData.hpp"

#include <tuple>
#include <variant>
#include <vector>

namespace boss::engines::bulk {

template <class... SupportedTypes> class TupleData {
public:
  template <
      typename T,
      std::enable_if_t<std::is_convertible<T, std::variant<SupportedTypes...>>::value, int> = 0>
  TupleData(T const& value) : m_value(value) {}

  template <
      typename U,
      std::enable_if_t<!std::is_convertible<U, std::variant<SupportedTypes...>>::value, int> = 0>
  TupleData(U const& value) : m_value(Data(value)) {}

  TupleData(char* rawData, int& offset) {
    std::visit([this](auto&& arg) { set(arg); }, m_value);
  }

  friend std::ostream& operator<<(std::ostream& os, TupleData const& tupleData) {
    std::visit([&os](auto&& arg) { os << arg; }, tupleData.m_value);
    return os;
  }

  template <class Visitor> void visit(Visitor&& vis) const { std::visit(vis, m_value); }

private:
  std::variant<SupportedTypes...> m_value;

  template <typename T> void set(char* rawData, int& offset);
};

template <class... SupportedTypes>
template <typename T>
inline void TupleData<SupportedTypes...>::set(char* rawData, int& offset) {
  m_value = *reinterpret_cast<T*>(&rawData[offset]);
  offset += sizeof(T);
}

template <class... SupportedTypes> class TupleType {
public:
  typedef TupleData<SupportedTypes...> TupleValue;

  template <class CustomAllocator = std::allocator<TupleValue>> class Tuple {
  public:
    Tuple() {}
    Tuple(Tuple const& other) : m_values(other.m_values) {}
    Tuple(Tuple&& other) : m_values(std::move(other.m_values)) {}

    auto begin() { return m_values.begin(); }
    auto end() { return m_values.end(); }
    auto cbegin() const { return m_values.begin(); }
    auto cend() const { return m_values.end(); }
    auto begin() const { return m_values.begin(); }
    auto end() const { return m_values.end(); }

    auto const& operator[](size_t index) const { return m_values[index]; }
    auto& operator[](size_t index) { return m_values[index]; }

    size_t size() const { return m_values.size(); }

    void reserve(size_t columnCount) { m_values.reserve(columnCount); }

    void add(TupleValue const& tuple) { m_values.push_back(tuple); }
    void add(TupleValue&& tuple) { m_values.push_back(std::move(tuple)); }

    template <typename T> void addMissing() { m_values.push_back(MissingData<T>()); }

    template <typename T> void setMissing(size_t index) {
      m_values[index] = TupleValue(MissingData<T>());
    }

    template <typename Any> void set(size_t index, Any value) {
      m_values[index] = TupleValue(value);
    }

    friend std::ostream& operator<<(std::ostream& os, Tuple const& tuple) {
      bool first = true;
      for(TupleValue const& tupleValue : tuple.m_values) {
        if(!first) {
          os << ", ";
        }
        first = false;

        os << tupleValue;
      }
      return os;
    }

  private:
    std::vector<TupleValue, CustomAllocator> m_values;
  }; // Tuple

}; // TupleType

} // namespace boss::engines::bulk
