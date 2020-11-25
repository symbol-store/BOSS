#pragma once

#include "Block.hpp"
#include "MaterializedView.hpp"
#include "Tuple.hpp"

#include "SymbolicData/SymbolicData.hpp"

#include "Utils/UniqueString.hpp"

#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace boss::engines::bulk {

template <class... SupportedTypes> class TableType {
public:
  template <class CustomTupleAllocator, class CustomTupleVectorAllocator> friend class Table;

  typedef typename TupleType<Data<SupportedTypes>...>::TupleValue CustomTupleValue;

  template <class CustomAllocator>
  class CustomTuple : public TupleType<Data<SupportedTypes>...>::template Tuple<CustomAllocator> {};

  template <class CustomTupleAllocator = std::allocator<CustomTupleValue>,
            class CustomTupleVectorAllocator = std::allocator<CustomTuple<CustomTupleAllocator>>>
  class Table : public MaterializedView<Table<CustomTupleAllocator, CustomTupleVectorAllocator>> {
    template <class> friend class MaterializedView;

  public:
    typedef CustomTupleValue TupleValueImpl;
    typedef CustomTuple<CustomTupleAllocator> TupleImpl;
    typedef std::vector<TupleImpl, CustomTupleVectorAllocator> TupleVectorImpl;
    typedef CustomTupleAllocator TupleAllocator;
    typedef CustomTupleVectorAllocator TupleVectorAllocator;

    typedef MaterializedView<Table<CustomTupleAllocator, CustomTupleVectorAllocator>>
        TableMaterializedView;

    Table(std::string const& name) : TableMaterializedView(*this, true), m_name(name) {}
    Table(std::string const&& name) : TableMaterializedView(*this, true), m_name(std::move(name)) {}

    // non-copyable
    Table(Table const&) = delete;
    Table(Table&&) = delete;
    Table() = delete;

    virtual ~Table() {}

    std::string const& name() const { return m_name; }

    template <typename T>
    typename std::enable_if<
        std::bool_constant<(std::is_same_v<T, SupportedTypes> || ...)>::value>::type
    addColumn(std::string const& name) {
      m_columns.emplace_back(name, typeid(T));
      m_offsets.push_back(sizeof(T));

      this->m_columnIndexMapping.push_back(this->m_columnIndexMapping.size());
    }

    template <class OtherTupleAllocator>
    void preAllocateRows(CustomTuple<OtherTupleAllocator> const& tuple, size_t numRows);

    template <class OtherTupleAllocator>
    bool addPreAllocatedRow(CustomTuple<OtherTupleAllocator> const& tuple) {
      return addRow(tuple, false);
    }

    template <class OtherTupleAllocator>
    bool addRow(CustomTuple<OtherTupleAllocator> const& tuple, bool allocateSpace = true);

    template <class OtherTupleAllocator, class OtherTupleVectorAllocator>
    bool
    addRows(std::vector<CustomTuple<OtherTupleAllocator>, OtherTupleVectorAllocator> const& tuples);

    size_t columnCount() const { return m_columns.size(); }
    std::string const& columnName(size_t index) const { return m_columns[index].m_name; }
    int columnIndex(std::string const& columnName) const override;

    std::type_info const& columnType(size_t index) const { return m_columns[index].m_info; }

    CustomTupleValue createTupleValue(size_t columnIndex, std::string const& strData) {
      size_t functionIndex = m_columns[columnIndex].m_converterIndex;
      auto& function = s_convertTupleFunctions[functionIndex];
      return function(strData);
    }

    CustomTupleValue createMissingValue(size_t columnIndex) {
      size_t functionIndex = m_columns[columnIndex].m_converterIndex;
      auto& function = s_createMissingFunctions[functionIndex];
      return function();
    }

  private:
    std::string m_name;

    struct ColumnInfo {
      ColumnInfo(std::string const& name, std::type_info const& info)
          : m_name(name), m_info(info), m_converterIndex(getConverterIndex(std::type_index(info))) {
      }

      std::string m_name;
      std::type_info const& m_info;
      int m_converterIndex;
    };

    std::vector<ColumnInfo> m_columns;
    std::vector<size_t> m_offsets;
    std::vector<char> m_converters;

    template <typename ToType> static ToType const& convert(char const* rawData) {
      return *reinterpret_cast<ToType const*>(rawData);
    }

    template <typename ToType>
    static typename Data<ToType>::SymbolicType& convertSymbolic(char const* rawData) {
      auto* symbolPtr =
          *reinterpret_cast<typename Data<ToType>::SymbolicType const* const*>(rawData);
      return *const_cast<typename Data<ToType>::SymbolicType*>(symbolPtr);
    }

    template <typename ToType> static ToType convertSymbolicToValue(char const* rawData) {
      return static_cast<ToType>(
          **reinterpret_cast<typename Data<ToType>::SymbolicType const* const*>(rawData));
    }

    typedef std::tuple<Data<SupportedTypes>& (*)(char const*)...> ConversionFunctionTypes;

    static constexpr size_t s_converterCount = std::tuple_size<ConversionFunctionTypes>::value;

    template <typename ToType>
    static CustomTupleValue convertFromString(std::string const& strData) {
      if constexpr(std::is_same<std::string, ToType>::value) {
        return CustomTupleValue(Data(strData));
      } else if constexpr(std::is_same<char const*, ToType>::value) {
        char const* uniqueValue = UniqueString::MakeUnique(strData.c_str());
        return CustomTupleValue(Data(uniqueValue));
      } else {
        // check if the string look like is convertible
        // if not, just return a missing value
        if((strData[0] < '0' || strData[0] > '9') && strData[0] != '.') {
          return MissingData<ToType>();
        } else {
          ToType value = static_cast<ToType>(std::stof(strData));
          return CustomTupleValue(Data(value));
        }
      }
    }

    static constexpr std::array<CustomTupleValue (*)(std::string const&), s_converterCount>
        s_convertTupleFunctions = {convertFromString<SupportedTypes>...};

    template <typename ToType> static CustomTupleValue createMissing() {
      return MissingData<ToType>();
    }

    static constexpr std::array<CustomTupleValue (*)(void), s_converterCount>
        s_createMissingFunctions = {createMissing<SupportedTypes>...};

    template <typename T, typename Predicate>
    static bool predicateFunc(char const* rawData, bool symbolic, Predicate const& predicate) {
      if(symbolic) {
        T value = convertSymbolicToValue<T>(rawData);
        return predicate(value);
      } else {
        return predicate(convert<T>(rawData));
      }
    }

    template <typename Predicate>
    static constexpr std::array<bool (*)(char const*, bool, Predicate const&), s_converterCount>
        s_predicateFunctions = {predicateFunc<SupportedTypes, Predicate>...};

    template <typename T>
    static bool evaluateFunc(char* dest, char const* rawData, TableMaterializedView const* view,
                             size_t rowIndex) {
      T& nonSymbolicValue = *reinterpret_cast<T*>(dest);
      auto& symbolicData = convertSymbolic<T>(rawData);
      if(symbolicData.Evaluate(view, rowIndex)) {
        nonSymbolicValue = static_cast<T>(symbolicData);
        return true;
      } else {
        return false;
      }
    }

    static constexpr std::array<bool (*)(char*, char const*, TableMaterializedView const*, size_t),
                                s_converterCount>
        s_evaluateFunctions = {evaluateFunc<SupportedTypes>...};

    template <typename T> static std::multimap<T, char const*>& sortMap() {
      static std::multimap<T, char const*> map;
      return map;
    }

    template <typename T> static void sortFunc(char const* rawData, bool symbolic) {
      if(symbolic) {
        T value = convertSymbolicToValue<T>(rawData);
        sortMap<T>().insert({value, rawData});
      } else {
        T value = convert<T>(rawData);
        sortMap<T>().insert({value, rawData});
      }
    }

    template <typename T> static void sortExtractFunc(char* dest, size_t offset, size_t rowSize) {
      for(auto& it : sortMap<T>()) {
        memcpy(dest, it.second - offset, rowSize);
        dest += rowSize;
      }
      sortMap<T>().clear();
    }

    static constexpr std::array<void (*)(char const*, bool), s_converterCount> s_sortFunctions = {
        sortFunc<SupportedTypes>...};
    static constexpr std::array<void (*)(char*, size_t, size_t), s_converterCount>
        s_sortExtractFunctions = {sortExtractFunc<SupportedTypes>...};

    template <typename T, typename Aggregate>
    static void aggregateFunc(char* dest, char const* rawData, bool symbolic,
                              Aggregate const& aggregate) {
      T& aggregateValue = *reinterpret_cast<T*>(dest);

      if(symbolic) {
        T value = convertSymbolicToValue<T>(rawData);
        aggregateValue = aggregate(aggregateValue, value);
      } else {
        aggregateValue = aggregate(aggregateValue, convert<T>(rawData));
      }
    }

    template <typename Aggregate>
    static constexpr std::array<void (*)(char*, char const*, bool, Aggregate const&),
                                s_converterCount>
        s_aggregateFunctions = {aggregateFunc<SupportedTypes, Aggregate>...};

    template <typename T> static void printFunc(char const* rawData, bool symbolic) {
      symbolic ? std::cout << convertSymbolic<T>(rawData) : std::cout << convert<T>(rawData);
    }

    typedef std::array<void (*)(char const*, bool), s_converterCount> PrintFunctions;
    static constexpr PrintFunctions s_printFunctions = {printFunc<SupportedTypes>...};

    template <typename T> static std::string toStringFunc(char const* rawData, bool symbolic) {
      if constexpr(std::is_same<std::string, T>::value || std::is_same<char const*, T>::value) {
        return symbolic ? convertSymbolicToValue<T>(rawData) : convert<T>(rawData);
      } else {
        return symbolic ? std::to_string(convertSymbolicToValue<T>(rawData))
                        : std::to_string(convert<T>(rawData));
      }
    }

    typedef std::array<std::string (*)(char const*, bool), s_converterCount> toStringFunctions;
    static constexpr toStringFunctions s_toStringFunctions = {toStringFunc<SupportedTypes>...};

    template <typename T, size_t OutputCharSize>
    static void copyFunc(char*& dest, char const* rawData, bool symbolic) {
      if constexpr(std::is_convertible<T, char const*>::value) {
        // specialization for char arrays: do a deep copy
        char const* src =
            symbolic ? convertSymbolicToValue<char const*>(rawData) : convert<char const*>(rawData);
        memcpy(dest, src, std::min(strlen(src), OutputCharSize));
        dest += OutputCharSize;
      } else {
        *reinterpret_cast<T*>(dest) =
            symbolic ? convertSymbolicToValue<T>(rawData) : convert<T>(rawData);
        dest += sizeof(T);
      }
    }

    template <size_t OutputCharSize = 1>
    static constexpr std::array<void (*)(char*& dest, char const*, bool), s_converterCount>
        s_copyFunctions = {copyFunc<SupportedTypes, OutputCharSize>...};

    static int getConverterIndex(std::type_index index) {
      static int tupleIndex = 0; // this index will increment in the same order
                                 // as the std::tuple will be initialised
      static std::unordered_map<std::type_index, int> s_converters = {{
          std::type_index(typeid(SupportedTypes)),
          tupleIndex++,
      }...};

      return s_converters[index];
    }
  };
};

template <class... SupportedTypes>
template <class CustomTupleAllocator, class CustomTupleVectorAllocator>
template <class OtherTupleAllocator>
inline void TableType<SupportedTypes...>::Table<CustomTupleAllocator, CustomTupleVectorAllocator>::
    preAllocateRows(CustomTuple<OtherTupleAllocator> const& tuple, size_t numRows) {
  // count the size we need
  std::vector<bool> symbolic(columnCount(), false);
  for(size_t index = 0; index < tuple.size(); ++index) {
    auto const& tupleValue = tuple[index];

    bool isSymbolic = false;
    tupleValue.visit([&isSymbolic](auto const& arg) { isSymbolic = arg.isSymbolic(); });

    symbolic[index] = isSymbolic;
  }

  for(size_t index = tuple.size(); index < columnCount(); ++index) {
    symbolic[index] = true;
  }

  Block* block = Block::FindOrCreateBlock(this->m_rootBlock, m_offsets, symbolic);
  if(block != nullptr) {
    block->extendSize(block->rowSize() * numRows);
  }
}

template <class... SupportedTypes>
template <class CustomTupleAllocator, class CustomTupleVectorAllocator>
template <class OtherTupleAllocator>
inline bool
TableType<SupportedTypes...>::Table<CustomTupleAllocator, CustomTupleVectorAllocator>::addRow(
    CustomTuple<OtherTupleAllocator> const& tuple, bool allocateSpace) {
  std::vector<bool> symbolic(columnCount(), false);
  for(size_t index = 0; index < tuple.size(); ++index) {
    auto const& tupleValue = tuple[index];

    bool isSymbolic = false;
    tupleValue.visit([&isSymbolic](auto const& arg) { isSymbolic = arg.isSymbolic(); });

    symbolic[index] = isSymbolic;
  }

  for(size_t index = tuple.size(); index < columnCount(); ++index) {
    symbolic[index] = true;
  }

  Block* block = Block::FindOrCreateBlock(this->m_rootBlock, m_offsets, symbolic);

  // add to that block
  if(block != nullptr) {
    if(allocateSpace) {
      block->extendSize(block->rowSize());
    }

    for(size_t index = 0; index < tuple.size(); ++index) {
      auto const& tupleValue = tuple[index];

      if(symbolic[index]) {
        tupleValue.visit([&block](auto const& arg) { block->addTupleSymbol(arg.getSymbolic()); });
      } else {
        tupleValue.visit([&block](auto const& arg) { block->addTupleValue(*arg.getValue()); });
      }
    }

    for(size_t index = tuple.size(); index < columnCount(); ++index) {
      createMissingValue(index).visit(
          [&block](auto const& arg) { block->addTupleSymbol(arg.getSymbolic()); });
    }
  }

  return true;
}

template <class... SupportedTypes>
template <class CustomTupleAllocator, class CustomTupleVectorAllocator>
template <class OtherTupleAllocator, class OtherTupleVectorAllocator>
inline bool
TableType<SupportedTypes...>::Table<CustomTupleAllocator, CustomTupleVectorAllocator>::addRows(
    std::vector<CustomTuple<OtherTupleAllocator>, OtherTupleVectorAllocator> const& tuples) {
  // count the size we need
  std::unordered_map<Block*, int> numTuplesPerBlock;
  for(auto const& tuple : tuples) {
    std::vector<bool> symbolic(columnCount(), false);
    for(size_t index = 0; index < tuple.size(); ++index) {
      auto const& tupleValue = tuple[index];

      bool isSymbolic = false;
      tupleValue.visit([&isSymbolic](auto const& arg) { isSymbolic = arg.isSymbolic(); });

      symbolic[index] = isSymbolic;
    }

    for(size_t index = tuple.size(); index < columnCount(); ++index) {
      symbolic[index] = true;
    }

    Block* block = Block::FindOrCreateBlock(this->m_rootBlock, m_offsets, symbolic);
    if(block != nullptr) {
      ++numTuplesPerBlock[block];
    }
  };

  // reserve space in each block
  for(auto const& [block, numTuples] : numTuplesPerBlock) {
    block->extendSize(block->rowSize() * numTuples);
  }

  for(auto const& tuple : tuples) {
    if(!addRow(tuple, false)) {
      return false;
    }
  }

  return true;
}

template <class... SupportedTypes>
template <class CustomTupleAllocator, class CustomTupleVectorAllocator>
inline int
TableType<SupportedTypes...>::Table<CustomTupleAllocator, CustomTupleVectorAllocator>::columnIndex(
    std::string const& columnName) const {
  for(size_t index = 0; index < m_columns.size(); ++index) {
    if(m_columns[index].m_name == columnName) {
      return index;
    }
  }

  return -1; // not found
}

} // namespace boss::engines::bulk
