#pragma once

#include "Engines/MLIREngine/Runtime/Database.hpp"
#include <array>
#include <mlir/IR/StandardTypes.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mlir/IR/TypeSupport.h>
#include <mlir/Support/LLVM.h>
#include <mlir/IR/Builders.h>
#include <utility>

struct TupleStreamTypeStorage : public mlir::TypeStorage {
  using NameToType = std::pair<std::string, mlir::Type>;
  using TupleHeader = std::vector<std::pair<std::string, mlir::Type>>;
  using KeyTy = std::pair<TupleHeader, ::mlir::Block*>;

  TupleStreamTypeStorage(mlir::ArrayRef<NameToType> tupleType, ::mlir::Block* loopBlock) : tupleType(tupleType), loopBlock(loopBlock) {}

  bool operator==(const KeyTy& key) const { return (key.first == tupleType) && (key.second == loopBlock); }

  static llvm::hash_code hashKey(const KeyTy& key) {
    return llvm::hash_combine(llvm::hash_combine_range(key.first.begin(), key.first.end()), llvm::hash_value(key.second));
  }

  static TupleStreamTypeStorage* construct(mlir::TypeStorageAllocator& allocator,
                                           KeyTy const& key) {
    return new(allocator.allocate<TupleStreamTypeStorage>()) TupleStreamTypeStorage(key.first, key.second);
  }

  TupleHeader tupleType;
  ::mlir::Block* loopBlock;
};

class TupleStreamType
    : public mlir::Type::TypeBase<TupleStreamType, mlir::Type, TupleStreamTypeStorage> {
public:
  using Base::Base;

  static TupleStreamType get(mlir::MLIRContext* context,
                             TupleStreamTypeStorage::TupleHeader const& tupleType, ::mlir::Block* block) {
    return Base::get<TupleStreamTypeStorage::KeyTy>(context, std::make_pair(tupleType, block));
  }

  TupleStreamTypeStorage::TupleHeader const& getTupleTypes() { return getImpl()->tupleType; }

  TupleStreamTypeStorage::TupleHeader getConcreteTupleTypes() {
    TupleStreamTypeStorage::TupleHeader result;
    std::copy_if(getImpl()->tupleType.begin(), getImpl()->tupleType.end(),
                 std::back_inserter(result),
                 [](auto const& pair) { return pair.first.find("symbol") == std::string::npos; });
    return result;
  }

  ::mlir::Block* getLoopBody() { return getImpl()->loopBlock; }
};

class RelationType : public mlir::Type::TypeBase<RelationType, mlir::Type, mlir::TypeStorage> {
  using Base::Base;
};