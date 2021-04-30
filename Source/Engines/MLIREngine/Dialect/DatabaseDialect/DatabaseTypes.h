#pragma once

#include "Engines/MLIREngine/Runtime/Database.hpp"
#include <array>
#include <mlir/IR/Builders.h>
#include <mlir/IR/StandardTypes.h>
#include <mlir/IR/TypeSupport.h>
#include <mlir/Support/LLVM.h>
#include <mlir/Transforms/DialectConversion.h>
#include <utility>

struct TupleStreamTypeStorage : public mlir::TypeStorage {
  using KeyTy = std::map<std::string, ::mlir::Type>;

  explicit TupleStreamTypeStorage(KeyTy fields) : fields(std::move(fields)) {}

  bool operator==(const KeyTy& key) const {
    return fields == key;
  }

  static llvm::hash_code hashKey(const KeyTy& key) {
    return llvm::hash_combine_range(key.begin(), key.end());
  }

  static TupleStreamTypeStorage* construct(mlir::TypeStorageAllocator& allocator,
                                                KeyTy const& key) {
    return new(allocator.allocate<TupleStreamTypeStorage>())
        TupleStreamTypeStorage(key);
  }

  std::map<std::string, ::mlir::Type> fields;
};


struct TupleStreamType : public mlir::Type::TypeBase<TupleStreamType, mlir::Type, TupleStreamTypeStorage> {
  using Base::Base;

  static TupleStreamType get(::mlir::MLIRContext* context, std::map<std::string, ::mlir::Type> const& fields) {
    return Base::get<TupleStreamTypeStorage::KeyTy>(context, fields);
  }

  TupleStreamTypeStorage::KeyTy getFields() const { return getImpl()->fields; }
};

struct TupleStreamUnionTypeStorage : public mlir::TypeStorage {
  using ChildTupleStreams = std::vector<TupleStreamType>;

  ChildTupleStreams streamTypes;
  std::vector<mlir::Block*> insertionPoints;

  using KeyTy = std::pair<ChildTupleStreams, std::vector<mlir::Block*>>;

  TupleStreamUnionTypeStorage(mlir::ArrayRef<TupleStreamType> streamTypes,
                         std::vector<mlir::Block*> insertionPoints)
      : streamTypes(streamTypes), insertionPoints(std::move(insertionPoints)) {}

  bool operator==(const KeyTy& key) const {
    return key.first == streamTypes && key.second == insertionPoints;
  }

  static llvm::hash_code hashKey(const KeyTy& key) {
    auto hashTupleStreams = llvm::hash_combine_range(key.first.begin(), key.first.end());
    auto hashInsertionPoints = llvm::hash_combine_range(key.second.begin(), key.second.end());
    return llvm::hash_combine(hashTupleStreams, hashInsertionPoints);
  }

  static TupleStreamUnionTypeStorage* construct(mlir::TypeStorageAllocator& allocator,
                                           KeyTy const& key) {
    return new(allocator.allocate<TupleStreamUnionTypeStorage>())
        TupleStreamUnionTypeStorage(key.first, key.second);
  }
};

class TupleStreamUnionType
    : public mlir::Type::TypeBase<TupleStreamUnionType, mlir::Type, TupleStreamUnionTypeStorage> {
public:
  using Base::Base;

  static TupleStreamUnionType get(mlir::MLIRContext* context,
                                  TupleStreamUnionTypeStorage::ChildTupleStreams const& streamTypes,
                             std::vector<mlir::Block*> const& insertionPoints) {
    return Base::get<TupleStreamUnionTypeStorage::KeyTy>(context,
                                                    std::make_pair(streamTypes, insertionPoints));
  }

  [[nodiscard]] TupleStreamUnionTypeStorage::ChildTupleStreams const& getTupleStreams() {
    return getImpl()->streamTypes;
  }

  [[nodiscard]] std::vector<::mlir::Block*> const& getBlocks() {
    return getImpl()->insertionPoints;
  }

  size_t getNumChildStreams() {
    return getImpl()->streamTypes.size();
  }

};

class RelationType : public mlir::Type::TypeBase<RelationType, mlir::Type, mlir::TypeStorage> {
  using Base::Base;
};