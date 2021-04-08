#pragma once

#include <mlir/IR/StandardTypes.h>
#include <mlir/IR/TypeSupport.h>
#include <mlir/Support/LLVM.h>
#include "Engines/MLIREngine/Runtime/Database.hpp"
#include <utility>
#include <array>

struct TupleStreamTypeStorage : public mlir::TypeStorage {
  using NameToType = std::pair<std::string, mlir::Type>;
  using KeyTy = std::vector<std::pair<std::string, mlir::Type>>;

  TupleStreamTypeStorage(mlir::ArrayRef<NameToType> tupleType) : tupleType(tupleType) {}

  bool operator==(const KeyTy& key) const { return key == tupleType; }

  static llvm::hash_code hashKey(const KeyTy& key) {
    return llvm::hash_combine_range(key.begin(), key.end());
  }

  static TupleStreamTypeStorage* construct(mlir::TypeStorageAllocator& allocator, KeyTy const& key) {
    return new(allocator.allocate<TupleStreamTypeStorage>()) TupleStreamTypeStorage(key);
  }

  KeyTy tupleType;
};

class TupleStreamType : public mlir::Type::TypeBase<TupleStreamType, mlir::Type, TupleStreamTypeStorage> {
public:
  using Base::Base;

  static TupleStreamType get(mlir::MLIRContext* context, TupleStreamTypeStorage::KeyTy const& tupleType) {
    return Base::get<TupleStreamTypeStorage::KeyTy>(context, tupleType);
  }

  TupleStreamTypeStorage::KeyTy const& getTupleTypes() { return getImpl()->tupleType; }
};

class RelationType : public mlir::Type::TypeBase<RelationType, mlir::Type, mlir::TypeStorage> {
  using Base::Base;
};