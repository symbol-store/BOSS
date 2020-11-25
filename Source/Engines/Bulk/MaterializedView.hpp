#pragma once

#include "Block.hpp"

#include "Utils/UniqueString.hpp"

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

namespace boss::engines::bulk {

template <class TableType> class MaterializedView {
public:
  MaterializedView(TableType const& table, bool allocated = false)
      : m_tables({&table}), m_allocated(allocated) {}

  MaterializedView(MaterializedView const& other)
      : m_rootBlock(other.m_rootBlock), m_columnIndexMapping(other.m_columnIndexMapping),
        m_tables(other.m_tables), m_allocated(false) {}

  MaterializedView(MaterializedView&& other)
      : m_rootBlock(std::move(other.m_rootBlock)),
        m_columnIndexMapping(std::move(other.m_columnIndexMapping)),
        m_tables(std::move(other.m_tables)), m_allocated(false) {}

  MaterializedView(MaterializedView const& other, TableType const& table)
      : m_rootBlock(other.m_rootBlock), m_columnIndexMapping(other.m_columnIndexMapping),
        m_tables(other.m_tables), m_allocated(false) {
    m_tables.push_back(&table);
  }

  virtual ~MaterializedView();

  void clear() {
    m_allocated = true;
    m_rootBlock.left = m_rootBlock.right = nullptr;
  }

  void print(bool skipSymbolic = false) const;

  size_t numRows() const;

  template <size_t OutputCharSize> void copy(char* dest, bool skipSymbolic = false) const;

  virtual int columnIndex(std::string const& column) const;
  TableType const* getTableAndColumnIndexes(std::string const& column, int& tableColumnIndex,
                                            int& rawIndex) const;

  template <typename T> T get(size_t columnIndex, size_t rowIndex, bool skipSymbolic = false) const;

  std::string toString(size_t columnIndex, size_t rowIndex, bool skipSymbolic = false) const;

  // implement a specialised class to support hash/probe
  class HashTable {
  public:
    virtual ~HashTable() {}

    // support probe only
    MaterializedView<TableType> probe(TableType const& table, std::string const& column) const;
  };

  enum class PredicateOp {
    EQUALS,
    DIFFERENT,
    LESS,
    GREATER,
    LESS_OR_EQUALS,
    GREATER_OR_EQUALS,
  };

  enum class AggregateOp {
    COUNT,
    SUM,
    MIN,
    MAX,
  };

  // symbol operators
  MaterializedView symbolicFilter(std::string const& column, bool symbolic) const;
  MaterializedView& evaluate(TableType const* fromTable = nullptr, size_t fromRealIndex = 0);
  MaterializedView& evaluate(std::string const& column);
  MaterializedView evaluated(TableType const* fromTable = nullptr, size_t fromRealIndex = 0) const;
  MaterializedView evaluated(std::string const& column) const;

  // query operators
  template <typename T>
  MaterializedView select(std::string const& column, T const& value, PredicateOp op) const;
  MaterializedView sort(std::string const& column) const;
  MaterializedView aggregate(std::string const& column, AggregateOp op) const;
  MaterializedView project(std::vector<std::string> columns) const;
  MaterializedView::HashTable hash(std::string const& column) const;

protected:
  BlockContainer m_rootBlock;
  std::vector<int> m_columnIndexMapping;

  void deleteBlocks();
  void swapBlocks(MaterializedView& other);

  // generic visitor, to traverse all the blocks
  template <class Visitor> void forEachBlock(Visitor&& vis) const;

  void printHeader() const;

  void printBlock(Block const* block) const;

  template <size_t OutputCharSize> void copyBlock(char*& dest, Block const* block) const;

private:
  std::vector<TableType const*> m_tables;
  bool m_allocated;
};

template <class TableType> inline MaterializedView<TableType>::~MaterializedView<TableType>() {
  deleteBlocks();
}

template <class TableType> inline void MaterializedView<TableType>::deleteBlocks() {
  if(m_allocated) {
    std::vector<BlockContainer const*> blocksToDelete;
    forEachBlock([this, &blocksToDelete](BlockContainer const* block) {
      if(block != &m_rootBlock) {
        blocksToDelete.push_back(block);
      }
    });

    for(BlockContainer const* block : blocksToDelete) {
      delete block;
    }
  }

  clear();
}

template <class TableType>
inline void MaterializedView<TableType>::swapBlocks(MaterializedView<TableType>& other) {
  BlockContainer* otherLeft = other.m_rootBlock.left;
  BlockContainer* otherRight = other.m_rootBlock.right;
  bool otherAllocated = other.m_allocated;

  other.m_rootBlock.left = m_rootBlock.left;
  other.m_rootBlock.right = m_rootBlock.right;
  other.m_allocated = m_allocated;

  m_rootBlock.left = otherLeft;
  m_rootBlock.right = otherRight;
  m_allocated = otherAllocated;
}

template <class TableType>
TableType const* MaterializedView<TableType>::getTableAndColumnIndexes(std::string const& column,
                                                                       int& tableColumnIndex,
                                                                       int& rawIndex) const {
  // split the column name if in the form "[TABLE_NAME].[COLUMN.NAME]"
  size_t dotIndex = column.find('.');
  std::string const& columnName =
      dotIndex < column.size() - 1 ? column.substr(dotIndex + 1) : column;
  std::string const& tableName =
      dotIndex < column.size() - 1 ? column.substr(0, dotIndex) : std::string();

  // find the table containing this column
  int tableOffset = 0;
  for(auto* table : m_tables) {
    if(!tableName.empty() && tableName != table->name()) {
      tableOffset += table->columnCount();
      continue;
    }

    tableColumnIndex = table->columnIndex(columnName);
    if(tableColumnIndex < 0) {
      tableOffset += table->columnCount();
      continue;
    }

    rawIndex = m_columnIndexMapping[tableOffset + tableColumnIndex];
    if(rawIndex < 0) {
      tableOffset += table->columnCount();
      continue;
    }

    return table;
  }

  return nullptr;
}

template <class TableType>
int MaterializedView<TableType>::columnIndex(std::string const& column) const {
  int unused, rawIndex;
  if(getTableAndColumnIndexes(column, unused, rawIndex)) {
    return rawIndex;
  }

  return -1;
}

template <class TableType>
template <typename T>
T MaterializedView<TableType>::get(size_t columnIndex, size_t rowIndex, bool skipSymbolic) const {
  T value;

  auto findColRowValue = [&, this](Block const* block, size_t columnIndex) {
    char const* position = block->cbegin();

    // find the right row
    while(position < block->cend() && rowIndex > 0) {
      position += block->rowSize();
      rowIndex--;
    }

    // find the right column

    while(position < block->cend()) {
      for(TableType const* table : m_tables) {
        auto const& columns = table->m_columns;
        for(size_t tableIndex = 0; tableIndex < columns.size(); ++tableIndex) {
          int index = m_columnIndexMapping[tableIndex];
          if(index < 0) {
            continue;
          }

          if(columnIndex == 0) {
            size_t functionIndex = table->m_columns[tableIndex].m_converterIndex;
            auto& function = table->template s_copyFunctions<>[functionIndex];
            char* rawValue = reinterpret_cast<char*>(&value);
            function(rawValue, position, block->symbolic()[index]);
            return;
          }

          size_t offset = block->offsets()[index];
          position += offset;

          columnIndex--;
        }
      }
    }
  };

  if(!skipSymbolic) {
    // find the value in all the block
    forEachBlock([&, this](BlockContainer const* block) {
      if(block->leafBlock()) {
        findColRowValue(block->leafBlock(), columnIndex);
      }
    });
  } else {
    // find the block having no symbolic data
    BlockContainer const* currentBlock = &this->m_rootBlock;
    while(!currentBlock->isLeaf()) {
      currentBlock = currentBlock->right;
    }

    // find the value only in this one
    if(currentBlock->leafBlock()) {
      findColRowValue(currentBlock->leafBlock(), columnIndex);
    }
  }

  return value;
}

template <class TableType>
std::string MaterializedView<TableType>::toString(size_t columnIndex, size_t rowIndex,
                                                  bool skipSymbolic) const {
  std::string strValue;

  auto findColRowValue = [&, this](Block const* block, size_t columnIndex) {
    char const* position = block->cbegin();

    // find the right row
    while(position < block->cend() && rowIndex > 0) {
      position += block->rowSize();
      rowIndex--;
    }

    // find the right column

    while(position < block->cend()) {
      for(TableType const* table : m_tables) {
        auto const& columns = table->m_columns;
        for(size_t tableIndex = 0; tableIndex < columns.size(); ++tableIndex) {
          int index = m_columnIndexMapping[tableIndex];
          if(index < 0) {
            continue;
          }

          if(columnIndex == 0) {
            size_t functionIndex = columns[tableIndex].m_converterIndex;
            auto& function = table->s_toStringFunctions[functionIndex];
            strValue = function(position, block->symbolic()[index]);
            return;
          }

          size_t offset = block->offsets()[index];
          position += offset;

          columnIndex--;
        }
      }
    }
  };

  if(!skipSymbolic) {
    // find the value in all the block
    forEachBlock([&, this](BlockContainer const* block) {
      if(block->leafBlock()) {
        findColRowValue(block->leafBlock(), columnIndex);
      }
    });
  } else {
    // find the block having no symbolic data
    BlockContainer const* currentBlock = &this->m_rootBlock;
    while(!currentBlock->isLeaf()) {
      currentBlock = currentBlock->right;
    }

    // find the value only in this one
    if(currentBlock->leafBlock()) {
      findColRowValue(currentBlock->leafBlock(), columnIndex);
    }
  }

  return strValue;
}

template <class TableType>
template <class Visitor>
inline void MaterializedView<TableType>::forEachBlock(Visitor&& vis) const {
  // using morris algorithm
  // no recursion, no temporary container
  // tree is modified during traversal, and then put back to normal

  BlockContainer const* currentBlock = &this->m_rootBlock;

  std::vector<BlockContainer const*> blocksToVisit;

  while(currentBlock != nullptr) {
    if(currentBlock->left == nullptr) {
      blocksToVisit.push_back(currentBlock);
      currentBlock = currentBlock->right;
    } else {
      // find the inorder predecessor of current
      BlockContainer const* preBlock = currentBlock->left;
      while(preBlock->right != nullptr && preBlock->right != currentBlock) {
        preBlock = preBlock->right;
      }

      // make current as right child of its inorder predecessor
      if(preBlock->right == nullptr) {
        const_cast<BlockContainer*>(preBlock)->right = const_cast<BlockContainer*>(currentBlock);
        currentBlock = currentBlock->left;
      }
      // MAGIC OF RESTORING the Tree happens here:
      // Revert the changes made in if part to restore the original
      // tree i.e., fix the right child of predecessor
      else {
        const_cast<BlockContainer*>(preBlock)->right = nullptr;
        blocksToVisit.push_back(currentBlock);
        currentBlock = currentBlock->right;
      }
    }
  }

  for(BlockContainer const* block : blocksToVisit) {
    vis(block);
  }
}

template <class TableType>
inline void MaterializedView<TableType>::printBlock(Block const* block) const {
  char const* position = block->cbegin();

  auto printTableRows = [this, &position, &block](TableType const* table) {
    auto const& columns = table->m_columns;
    for(size_t tableIndex = 0; tableIndex < columns.size(); ++tableIndex) {
      size_t functionIndex = columns[tableIndex].m_converterIndex;
      auto& function = table->s_printFunctions[functionIndex];

      int index = m_columnIndexMapping[tableIndex];
      if(index < 0) {
        continue;
      }

      function(position, block->symbolic()[index]);
      std::cout << '\t';

      size_t offset = block->offsets()[index];
      position += offset;
    }
  };

  while(position < block->cend()) {
    std::for_each(m_tables.begin(), m_tables.end(), printTableRows);
    std::cout << std::endl;
  }
}

template <class TableType>
template <size_t OutputCharSize>
inline void MaterializedView<TableType>::copyBlock(char*& dest, Block const* block) const {
  char const* position = block->cbegin();

  auto copyTableRows = [this, &position, &block, &dest](TableType const* table) {
    auto const& columns = table->m_columns;
    for(size_t tableIndex = 0; tableIndex < columns.size(); ++tableIndex) {
      size_t functionIndex = columns[tableIndex].m_converterIndex;
      auto& function = table->template s_copyFunctions<OutputCharSize>[functionIndex];

      int index = m_columnIndexMapping[tableIndex];
      if(index < 0) {
        continue;
      }

      function(dest, position, block->symbolic()[index]);

      size_t offset = block->offsets()[index];
      position += offset;
    }
  };

  while(position < block->cend()) {
    std::for_each(m_tables.begin(), m_tables.end(), copyTableRows);
  }
}

template <class TableType> inline void MaterializedView<TableType>::printHeader() const {
  int tableOffset = 0;
  for(TableType const* table : m_tables) {
    for(size_t index = 0; index < table->columnCount(); ++index) {
      int rawIndex = m_columnIndexMapping[tableOffset + index];
      if(rawIndex < 0) {
        continue;
      }

      std::cout << table->m_columns[index].m_name << '\t';
    }

    tableOffset += table->columnCount();
  }
  std::cout << std::endl;

  tableOffset = 0;
  for(TableType const* table : m_tables) {
    for(size_t index = 0; index < table->columnCount(); ++index) {
      int rawIndex = m_columnIndexMapping[tableOffset + index];
      if(rawIndex < 0) {
        continue;
      }

      std::cout << '(' << table->m_columns[index].m_info.name() << ')' << '\t';
    }

    tableOffset += table->columnCount();
  }
  std::cout << std::endl;
}

template <class TableType> inline void MaterializedView<TableType>::print(bool skipSymbolic) const {
  bool first = true;
  for(TableType const* table : m_tables) {
    if(first) {
      first = false;
      std::cout << table->name();
    } else {
      std::cout << ',' << table->name();
    }
  }

  if(skipSymbolic) {
    std::cout << "(skipped symbolic data)";
  }
  std::cout << ':' << std::endl;

  printHeader();

  if(!skipSymbolic) {
    // print all the blocks
    forEachBlock([this](BlockContainer const* block) {
      if(block->leafBlock()) {
        printBlock(block->leafBlock());
      }
    });
  } else {
    // find the block having no symbolic data
    BlockContainer const* currentBlock = &this->m_rootBlock;
    while(!currentBlock->isLeaf()) {
      currentBlock = currentBlock->right;
    }

    // print only this one
    if(currentBlock->leafBlock()) {
      printBlock(currentBlock->leafBlock());
    }
  }
}

template <class TableType>
template <size_t OutputCharSize>
inline void MaterializedView<TableType>::copy(char* dest, bool skipSymbolic) const {
  if(!skipSymbolic) {
    // copy all the blocks
    forEachBlock([this, &dest](BlockContainer const* block) {
      if(block->leafBlock()) {
        copyBlock<OutputCharSize>(dest, block->leafBlock());
      }
    });
  } else {
    // find the block having no symbolic data
    BlockContainer const* currentBlock = &this->m_rootBlock;
    while(!currentBlock->isLeaf()) {
      currentBlock = currentBlock->right;
    }

    // copy only this one
    if(currentBlock->leafBlock()) {
      copyBlock<OutputCharSize>(dest, currentBlock->leafBlock());
    }
  }
}

template <class TableType> inline size_t MaterializedView<TableType>::numRows() const {
  size_t totalRows = 0;
  forEachBlock([&totalRows](BlockContainer const* block) {
    if(block->leafBlock()) {
      size_t rowSize = block->leafBlock()->rowSize();
      size_t bufferSize = block->leafBlock()->bufferSize();
      size_t BlockNumRows = bufferSize / rowSize;
      totalRows += BlockNumRows;
    }
  });

  return totalRows;
}

template <class TableType>
inline MaterializedView<TableType>&
MaterializedView<TableType>::evaluate(TableType const* fromTable, size_t fromRealIndex) {
  MaterializedView<TableType> output = evaluated(fromTable, fromRealIndex);
  swapBlocks(output);
  return *this;
}

template <class TableType>
inline MaterializedView<TableType>&
MaterializedView<TableType>::evaluate(std::string const& column) {
  MaterializedView<TableType> output = evaluated(column);
  swapBlocks(output);
  return *this;
}

template <class TableType>
inline MaterializedView<TableType>
MaterializedView<TableType>::evaluated(TableType const* fromTable, size_t fromRealIndex) const {
  for(size_t tableIndex = 0; tableIndex < m_tables.size(); ++tableIndex) {
    TableType const* table = m_tables[tableIndex];
    if(fromTable == nullptr || table == fromTable) {
      if(fromRealIndex < table->m_columns.size()) {
        MaterializedView<TableType> output = evaluated(table->columnName(fromRealIndex));
        fromRealIndex++;
        if(fromRealIndex < table->m_columns.size()) {
          // go to next column
          return output.evaluated(table, fromRealIndex);
        } else if(tableIndex + 1 < m_tables.size()) {
          // go to next table
          return output.evaluated(m_tables[tableIndex + 1], fromRealIndex);
        } else {
          // finished
          return output;
        }
      }
    }
  }

  // should not come here
  MaterializedView output(*this);
  output.clear();
  return output;
}

template <class TableType>
inline MaterializedView<TableType>
MaterializedView<TableType>::evaluated(std::string const& column) const {
  MaterializedView output(*this);
  output.clear();

  int rawIndex, columnIndex;
  auto* table = getTableAndColumnIndexes(column, columnIndex, rawIndex);
  if(table != nullptr) {
    size_t functionIndex = table->m_columns[columnIndex].m_converterIndex;
    auto& function = table->s_evaluateFunctions[functionIndex];

    auto evaluateFunc = [&function, this](char* dest, char const* rawData, size_t rowIndex) {
      return function(dest, rawData, this, rowIndex);
    };

    // apply to every block
    // TODO: could do only to symbolic ones, and copy other blocks?
    // but needs proper block copy method
    size_t rowIndex = 0;
    forEachBlock([&, this](BlockContainer const* block) {
      if(block->leafBlock()) {
        block->leafBlock()->evaluate(output.m_rootBlock, rawIndex, table->m_offsets[columnIndex],
                                     rowIndex, evaluateFunc);
      }
    });
  }

  return output;
}

template <class TableType>
inline MaterializedView<TableType>
MaterializedView<TableType>::symbolicFilter(std::string const& column, bool symbolic) const {
  MaterializedView output(*this);
  output.clear();

  int rawIndex = columnIndex(column);
  if(rawIndex >= 0) {
    // apply to every block
    forEachBlock([&output, &rawIndex, &symbolic](BlockContainer const* block) {
      if(block->leafBlock() && block->leafBlock()->symbolic()[rawIndex] == symbolic) {
        block->leafBlock()->select(output.m_rootBlock, rawIndex,
                                   [](char const*, bool) { return true; });
      }
    });
  }

  return output;
}

template <class TableType>
template <typename T>
inline MaterializedView<TableType> MaterializedView<TableType>::select(std::string const& column,
                                                                       T const& value,
                                                                       PredicateOp op) const {
  MaterializedView output(*this);
  output.clear();

  int rawIndex, columnIndex;
  auto* table = getTableAndColumnIndexes(column, columnIndex, rawIndex);
  if(table != nullptr) {
    auto predicateOp = [&value, &op](auto& rowValue) {
      if constexpr(std::is_same<decltype(rowValue), decltype(value)>::value) {
        if(op == PredicateOp::EQUALS) {
          return rowValue == value;
        } else if(op == PredicateOp::DIFFERENT) {
          return rowValue != value;
        } else if(op == PredicateOp::GREATER) {
          return rowValue > value;
        } else if(op == PredicateOp::GREATER_OR_EQUALS) {
          return rowValue >= value;
        } else if(op == PredicateOp::LESS) {
          return rowValue < value;
        } else if(op == PredicateOp::LESS_OR_EQUALS) {
          return rowValue <= value;
        }
      }
      return false;
    };

    size_t functionIndex = table->m_columns[columnIndex].m_converterIndex;
    auto& function = table->template s_predicateFunctions<decltype(predicateOp)>[functionIndex];

    auto predicate = [&predicateOp, &function](char const* rawData, bool symbolic) {
      return function(rawData, symbolic, predicateOp);
    };

    // apply to every block
    forEachBlock([&output, &rawIndex, &predicate](BlockContainer const* block) {
      if(block->leafBlock()) {
        block->leafBlock()->select(output.m_rootBlock, rawIndex, predicate);
      }
    });
  }

  return output;
}

template <class TableType>
inline MaterializedView<TableType>
MaterializedView<TableType>::sort(std::string const& column) const {
  MaterializedView output(*this);
  output.clear();

  int rawIndex, columnIndex;
  auto* table = getTableAndColumnIndexes(column, columnIndex, rawIndex);
  if(table != nullptr) {
    size_t functionIndex = table->m_columns[columnIndex].m_converterIndex;
    auto& sortFunction = table->s_sortFunctions[functionIndex];
    auto& extractFunction = table->s_sortExtractFunctions[functionIndex];

    // apply to every block
    forEachBlock(
        [&output, &rawIndex, &sortFunction, &extractFunction](BlockContainer const* block) {
          if(block->leafBlock()) {
            block->leafBlock()->sort(output.m_rootBlock, rawIndex, sortFunction, extractFunction);
          }
        });
  }

  return output;
}

template <class TableType>
inline MaterializedView<TableType> MaterializedView<TableType>::aggregate(std::string const& column,
                                                                          AggregateOp op) const {
  MaterializedView output(*this);
  output.clear();

  int rawIndex, columnIndex;
  auto* table = getTableAndColumnIndexes(column, columnIndex, rawIndex);
  if(table != nullptr) {
    bool initialised = false;

    auto aggregateOp = [&op, &initialised](auto& aggregateValue, auto& rowValue) {
      if constexpr(std::is_convertible<std::decay_t<decltype(rowValue)>, char const*>::value) {
        // special case for chars, convert in/out of string
        std::string aggregateStr(aggregateValue ? (std::decay_t<decltype(rowValue)>)aggregateValue
                                                : rowValue);
        if(op == AggregateOp::COUNT) {
          aggregateStr = std::to_string((int)(atoi(aggregateStr.c_str()) + 1));
        } else {
          std::string rowValueStr(rowValue);

          if(op == AggregateOp::SUM) {
            aggregateStr += rowValueStr;
          } else if(op == AggregateOp::MIN) {
            aggregateStr = aggregateStr < rowValueStr ? aggregateStr : rowValueStr;
          } else if(op == AggregateOp::MAX) {
            aggregateStr = aggregateStr > rowValueStr ? aggregateStr : rowValueStr;
          }
        }

        return UniqueString::MakeUnique(aggregateStr.c_str());
      } else if constexpr(std::is_same<std::decay_t<decltype(rowValue)>,
                                       std::decay_t<decltype(aggregateValue)>>::value) {
        if(op == AggregateOp::SUM) {
          if(!initialised) {
            initialised = true;
            return rowValue;
          }
          return aggregateValue + rowValue;
        } else if(op == AggregateOp::MIN) {
          if(!initialised) {
            initialised = true;
            return rowValue;
          }
          return aggregateValue < rowValue ? aggregateValue : rowValue;
        } else if(op == AggregateOp::MAX) {
          if(!initialised) {
            initialised = true;
            return rowValue;
          }
          return aggregateValue > rowValue ? aggregateValue : rowValue;
        } else if(op == AggregateOp::COUNT) {
          return aggregateValue + 1;
        }
      }

      return aggregateValue;
    };

    size_t functionIndex = table->m_columns[columnIndex].m_converterIndex;
    auto& function = table->template s_aggregateFunctions<decltype(aggregateOp)>[functionIndex];

    auto aggregate = [&aggregateOp, &function](char* dest, char const* rawData, bool symbolic) {
      function(dest, rawData, symbolic, aggregateOp);
    };

    // apply to every block
    forEachBlock([&output, &rawIndex, &aggregate](BlockContainer const* block) {
      if(block->leafBlock()) {
        block->leafBlock()->aggregate(output.m_rootBlock, rawIndex, aggregate);
      }
    });

    // add custom mapping with table's column ids since some columns are missing
    std::fill(output.m_columnIndexMapping.begin(), output.m_columnIndexMapping.end(), -1);
    output.m_columnIndexMapping[columnIndex] = 0;
  }

  return output;
}

template <class TableType>
inline MaterializedView<TableType>
MaterializedView<TableType>::project(std::vector<std::string> columns) const {
  MaterializedView output(*this);
  output.clear();

  std::vector<size_t> columnIndexes;
  columnIndexes.reserve(columns.size());

  // add custom mapping with table's column ids since some columns are missing
  int finalColumnCount = 0;
  std::fill(output.m_columnIndexMapping.begin(), output.m_columnIndexMapping.end(), -1);

  for(std::string const& column : columns) {
    int rawColumnIndex = columnIndex(column);
    if(rawColumnIndex >= 0) {
      columnIndexes.push_back(rawColumnIndex);

      for(size_t key = 0; key < m_columnIndexMapping.size(); ++key) {
        if(m_columnIndexMapping[key] == rawColumnIndex) {
          output.m_columnIndexMapping[key] = finalColumnCount++;
          break;
        }
      }
    }
  }

  // apply projection to every block
  forEachBlock([&output, &columnIndexes](BlockContainer const* block) {
    if(block->leafBlock()) {
      block->leafBlock()->project(output.m_rootBlock, columnIndexes);
    }
  });

  return output;
}

template <class TableType>
inline typename MaterializedView<TableType>::HashTable
MaterializedView<TableType>::hash(std::string const& column) const {
  return HashTable(); // TODO
}

template <class TableType>
inline MaterializedView<TableType>
MaterializedView<TableType>::HashTable ::probe(TableType const& table,
                                               std::string const& column) const {
  return MaterializedView<TableType>(*this, table); // TODO
}

} // namespace boss::engines::bulk
