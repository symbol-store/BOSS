#pragma once

#include "Utils/UniqueString.hpp"

#include <vector>

namespace boss::engines::bulk {

template <typename T> struct TreeNode {

  TreeNode() : left(nullptr), right(nullptr) {}
  TreeNode(TreeNode const& other) : left(other.left), right(other.right) {}
  TreeNode(TreeNode&& other) : left(std::move(other.left)), right(std::move(other.right)) {}

  virtual ~TreeNode() {}

  T* left;
  T* right;

  bool isLeaf() const { return left == right; }
};

class Block;
class BlockContainer : public TreeNode<BlockContainer> {
public:
  virtual ~BlockContainer() {}

  virtual Block* leafBlock() { return nullptr; }
  virtual Block const* leafBlock() const { return nullptr; }
};

class Block : public BlockContainer {
public:
  Block* leafBlock() override { return this; }
  const Block* leafBlock() const override { return this; }

  Block(std::vector<size_t> const& offsets, std::vector<bool> const& symbolic);

  virtual ~Block() {}

  // non-copyable
  Block(Block const&) = delete;
  Block(Block&&) = delete;
  Block() = delete;

  char* begin() { return reinterpret_cast<char*>(&m_buffer[0]); }
  char* end() { return reinterpret_cast<char*>(&m_buffer[m_buffer.size() - 1]) + 1; }

  char const* cbegin() const { return reinterpret_cast<char const*>(&m_buffer[0]); }
  char const* cend() const {
    return reinterpret_cast<char const*>(&m_buffer[m_buffer.size() - 1]) + 1;
  }

  std::vector<size_t> const& offsets() const { return m_offsets; }
  std::vector<bool> const& symbolic() const { return m_symbolic; }

  size_t rowSize() const { return m_rowSize; }
  size_t bufferSize() const { return m_buffer.size(); }

  void extendSize(size_t size) { m_buffer.reserve(m_buffer.capacity() + size); }

  template <typename Value> bool addTupleValue(Value const& value);

  template <typename Symbol> bool addTupleSymbol(Symbol const* pointer);

  static Block* FindOrCreateBlock(BlockContainer& rootBlock, std::vector<size_t> const& offsets,
                                  std::vector<bool> const& symbolic);

  // query operators
  template <typename Evaluate>
  void evaluate(BlockContainer& outputRoot, int columnIndex, size_t realOffset, size_t& rowIndex,
                Evaluate&& evaluate) const;
  template <typename Predicate>
  void select(BlockContainer& outputRoot, int columnIndex, Predicate&& predicate) const;
  template <typename Sort, typename Extract>
  void sort(BlockContainer& outputRoot, int columnIndex, Sort&& sortFunc,
            Extract&& extractFunc) const;
  void project(BlockContainer& outputRoot, std::vector<size_t> const& columnIndexes) const;
  template <typename Aggregate>
  void aggregate(BlockContainer& outputRoot, int columnIndex, Aggregate&& aggregate) const;
  void hash(BlockContainer& outputRoot, int columnIndex) const;

private:
  std::vector<size_t> m_offsets;
  std::vector<bool> m_symbolic;
  std::vector<char> m_buffer;

  size_t m_rowSize;

  bool addTupleData(char const* data, size_t size);
};

inline Block ::Block(std::vector<size_t> const& offsets, std::vector<bool> const& symbolic)
    : m_symbolic(symbolic), m_rowSize(0) {
  m_offsets.reserve(offsets.size());
  for(size_t index = 0; index < offsets.size(); ++index) {
    if(!symbolic[index]) {
      m_offsets.push_back(offsets[index]);
    } else {
      m_offsets.push_back(sizeof(void*));
    }
    m_rowSize += m_offsets.back();
  }
}

inline Block* Block ::FindOrCreateBlock(BlockContainer& rootBlock,
                                        std::vector<size_t> const& offsets,
                                        std::vector<bool> const& symbolic) {
  // find in which block to add it
  // traverse the binary tree based on which values are symbolic values
  size_t tupleSize = 0;
  BlockContainer* currentBlock = &rootBlock;
  for(size_t index = 0; index < symbolic.size(); ++index) {

    bool isSymbolic = symbolic[index];

    BlockContainer*& nextBlock = isSymbolic ? currentBlock->left : currentBlock->right;

    if(isSymbolic) {
      tupleSize += sizeof(void*);
    } else {
      tupleSize += offsets[index];
    }

    if(nextBlock == nullptr) {
      bool isLeaf(index == symbolic.size() - 1);
      nextBlock = isLeaf ? new Block(offsets, symbolic) : new BlockContainer();
    }

    currentBlock = nextBlock;
  }

  return currentBlock->leafBlock();
}

template <typename Value> inline bool Block ::addTupleValue(Value const& value) {
  char const* rawData = reinterpret_cast<char const*>(&value);
  return addTupleData(rawData, sizeof(Value));
}

// specialization for char arrays:
// this isn't a datatype that can be safely shallow-copied to the raw buffer
// need to keep them in memory to do so
// get the chance to keep them unique in memory too
template <> inline bool Block ::addTupleValue(char* const& value) {
  char const* uniqueValue = UniqueString::MakeUnique(value);
  char const* rawData = reinterpret_cast<char const*>(&uniqueValue);
  return addTupleData(rawData, sizeof(value));
}

template <typename Symbol> inline bool Block ::addTupleSymbol(Symbol const* pointer) {
  char const* rawData = reinterpret_cast<char const*>(&pointer);
  return addTupleData(rawData, sizeof(Symbol*));
}

inline bool Block ::addTupleData(char const* data, size_t size) {
  size_t previousSize = m_buffer.size();
  size_t newSize = previousSize + size;
  m_buffer.resize(newSize);

  memcpy(&m_buffer[previousSize], data, size);

  return true;
}

template <typename Evaluate>
inline void Block ::evaluate(BlockContainer& outputRoot, int columnIndex, size_t realOffset,
                             size_t& rowIndex, Evaluate&& evaluateFunc) const {
  size_t rowOffset = 0;
  for(int index = 0; index < columnIndex; ++index) {
    rowOffset += m_offsets[index];
  }

  std::vector<bool> nonSymbolic(m_symbolic.begin(), m_symbolic.end());
  nonSymbolic[columnIndex] = false;

  bool wasSymbolic = m_symbolic[columnIndex];

  std::vector<size_t> nonSymbolicOffsets(m_offsets.begin(), m_offsets.end());
  nonSymbolicOffsets[columnIndex] = realOffset;

  size_t nonSymbolicRowSize = m_rowSize + realOffset - m_offsets[columnIndex];

  // find which output block to write data
  Block* symbolicBlock = Block::FindOrCreateBlock(outputRoot, m_offsets, m_symbolic);
  Block* nonSymbolicBlock = Block::FindOrCreateBlock(outputRoot, nonSymbolicOffsets, nonSymbolic);

  if(symbolicBlock != nullptr && nonSymbolicBlock != nullptr) {

    char const* position = cbegin() + rowOffset;

    char nonSymbolicValue[realOffset];

    while(position < cend()) {
      if(wasSymbolic && evaluateFunc(&nonSymbolicValue[0], position, rowIndex)) {
        // become non-symbolic, move to the non-symbolic block
        nonSymbolicBlock->m_buffer.resize(nonSymbolicBlock->m_buffer.size() + nonSymbolicRowSize);
        char* dst =
            &nonSymbolicBlock->m_buffer[nonSymbolicBlock->m_buffer.size() - nonSymbolicRowSize];
        memcpy(dst, position - rowOffset, rowOffset);
        dst += rowOffset;
        memcpy(dst, nonSymbolicValue, realOffset);
        dst += realOffset;
        char const* src = position + m_offsets[columnIndex];
        memcpy(dst, src, nonSymbolicRowSize - rowOffset - realOffset);
      } else {
        // nothing changed, just copy raw data to the output block
        char const* src = position - rowOffset;
        symbolicBlock->m_buffer.resize(symbolicBlock->m_buffer.size() + m_rowSize);
        char* dst = &symbolicBlock->m_buffer[symbolicBlock->m_buffer.size() - m_rowSize];
        memcpy(dst, src, m_rowSize);
      }

      position += m_rowSize; // go to the next row, same offset
      rowIndex++;
    }
  }
}

template <typename Predicate>
inline void Block ::select(BlockContainer& outputRoot, int columnIndex,
                           Predicate&& predicate) const {
  size_t rowOffset = 0;
  for(int index = 0; index < columnIndex; ++index) {
    rowOffset += m_offsets[index];
  }

  // find which output block to write data
  Block* outputBlock = Block::FindOrCreateBlock(outputRoot, m_offsets, m_symbolic);

  if(outputBlock != nullptr) {
    bool symbolic = m_symbolic[columnIndex];

    char const* position = cbegin() + rowOffset;

    while(position < cend()) {
      if(predicate(position, symbolic) == true) {
        // just copy raw data to the output block
        char const* src = position - rowOffset;
        outputBlock->m_buffer.resize(outputBlock->m_buffer.size() + m_rowSize);
        char* dst = &outputBlock->m_buffer[outputBlock->m_buffer.size() - m_rowSize];
        memcpy(dst, src, m_rowSize);
      }

      position += m_rowSize; // go to the next row, same offset
    }
  }
}

template <typename Sort, typename Extract>
inline void Block ::sort(BlockContainer& outputRoot, int columnIndex, Sort&& sortFunc,
                         Extract&& extractFunc) const {
  size_t rowOffset = 0;
  for(int index = 0; index < columnIndex; ++index) {
    rowOffset += m_offsets[index];
  }

  // find which output block to write data
  Block* outputBlock = Block::FindOrCreateBlock(outputRoot, m_offsets, m_symbolic);

  if(outputBlock != nullptr) {
    bool symbolic = m_symbolic[columnIndex];

    char const* position = cbegin() + rowOffset;

    // first save the sort data
    while(position < cend()) {
      sortFunc(position, symbolic);
      position += m_rowSize; // go to the next row, same offset
    }

    // and then write all in order
    outputBlock->m_buffer.resize(outputBlock->m_buffer.size() + m_buffer.size());
    char* dst = &outputBlock->m_buffer[outputBlock->m_buffer.size() - m_buffer.size()];
    extractFunc(dst, rowOffset, m_rowSize);
  }
}

template <typename Aggregate>
inline void Block ::aggregate(BlockContainer& outputRoot, int columnIndex,
                              Aggregate&& aggregate) const {
  std::vector<size_t> newOffsets = {m_offsets[columnIndex]};
  std::vector<bool> newSymbolic = {m_symbolic[columnIndex]};

  size_t outputRowSize = newOffsets[0];

  size_t rowOffset = 0;
  for(int index = 0; index < columnIndex; ++index) {
    rowOffset += m_offsets[index];
  }

  // find which output block to write data
  Block* outputBlock = Block::FindOrCreateBlock(outputRoot, newOffsets, newSymbolic);

  if(outputBlock != nullptr) {
    bool symbolic = newSymbolic[0];

    char const* position = cbegin() + rowOffset;

    // reserve space for only one value
    outputBlock->m_buffer.resize(outputBlock->m_buffer.size() + outputRowSize);
    char* dst = &outputBlock->m_buffer[outputBlock->m_buffer.size() - outputRowSize];
    memset(dst, 0, outputRowSize);

    while(position < cend()) {
      // aggregate each row value to this destination value
      aggregate(dst, position, symbolic);

      position += m_rowSize; // go to the next row, same offset
    }
  }
}

inline void Block ::project(BlockContainer& outputRoot,
                            std::vector<size_t> const& columnIndexes) const {
  std::vector<size_t> newOffsets;
  newOffsets.reserve(columnIndexes.size());
  std::vector<bool> newSymbolic;
  newSymbolic.reserve(columnIndexes.size());

  std::vector<size_t> projOffsets;
  projOffsets.reserve(columnIndexes.size() + 1);
  projOffsets.push_back(0);

  size_t outputRowSize = 0;
  size_t outputIndex = 0;
  for(size_t index = 0; index < m_offsets.size(); ++index) {
    if(outputIndex < columnIndexes.size() && index == columnIndexes[outputIndex]) {
      outputIndex++;
      projOffsets.push_back(0);
      newOffsets.push_back(m_offsets[index]);
      newSymbolic.push_back(m_symbolic[index]);
      outputRowSize += m_offsets[index];
    }
    projOffsets[outputIndex] += m_offsets[index];
  }

  // find which output block to write data
  Block* outputBlock = Block::FindOrCreateBlock(outputRoot, newOffsets, newSymbolic);

  if(outputBlock != nullptr) {

    char const* src = cbegin();

    while(src < cend()) {
      outputBlock->m_buffer.resize(outputBlock->m_buffer.size() + outputRowSize);
      char* dst = &outputBlock->m_buffer[outputBlock->m_buffer.size() - outputRowSize];

      for(size_t index = 0; index < newOffsets.size(); ++index) {
        size_t sizeToCopy = newOffsets[index];
        src += projOffsets[index];

        // just copy raw data to the output block
        memcpy(dst, src, sizeToCopy);

        dst += sizeToCopy;
      }

      src += projOffsets.back(); // go to the beginning of next row
    }
  }
}

inline void Block ::hash(BlockContainer& outputRoot, int columnIndex) const {
  // TODO
}

} // namespace boss::engines::bulk
