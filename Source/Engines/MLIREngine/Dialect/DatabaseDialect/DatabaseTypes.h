#pragma once

#include <array>
#include <map>
#include <mlir/IR/Builders.h>
#include <mlir/IR/StandardTypes.h>
#include <mlir/IR/TypeSupport.h>
#include <mlir/Support/LLVM.h>
#include <mlir/Transforms/DialectConversion.h>
#include <utility>

struct TupleStreamTypeStorage : public mlir::TypeStorage {
  using KeyTy = std::map<std::string, ::mlir::Type>;

  explicit TupleStreamTypeStorage(KeyTy fields) : fields(std::move(fields)) {}

  bool operator==(const KeyTy& key) const { return fields == key; }

  static llvm::hash_code hashKey(const KeyTy& key) {
    return llvm::hash_combine_range(key.begin(), key.end());
  }

  static TupleStreamTypeStorage* construct(mlir::TypeStorageAllocator& allocator,
                                           KeyTy const& key) {
    return new(allocator.allocate<TupleStreamTypeStorage>()) TupleStreamTypeStorage(key);
  }

  std::map<std::string, ::mlir::Type> fields;
};

struct TupleStreamType
    : public mlir::Type::TypeBase<TupleStreamType, mlir::Type, TupleStreamTypeStorage> {
  using Base::Base;

  static TupleStreamType get(::mlir::MLIRContext* context,
                             std::map<std::string, ::mlir::Type> const& fields) {
    return Base::get<TupleStreamTypeStorage::KeyTy>(context, fields);
  }

  TupleStreamTypeStorage::KeyTy getFields() const { return getImpl()->fields; }
};

template <class ChildType> struct TupleStreamContainerStorage : public mlir::TypeStorage {
  using ChildTypes = std::vector<ChildType>;

  ChildTypes childTypes;

  using KeyTy = ChildTypes;

  TupleStreamContainerStorage(mlir::ArrayRef<ChildType> childTypes) : childTypes(childTypes) {}

  bool operator==(const KeyTy& key) const { return key == childTypes; }

  static llvm::hash_code hashKey(const KeyTy& key) {
    return llvm::hash_combine_range(key.begin(), key.end());
  }

  static TupleStreamContainerStorage* construct(mlir::TypeStorageAllocator& allocator,
                                                KeyTy const& key) {
    return new(allocator.allocate<TupleStreamContainerStorage>()) TupleStreamContainerStorage(key);
  }
};

class GenericTupleStreamUnionType
    : public mlir::Type::TypeBase<GenericTupleStreamUnionType, mlir::Type,
                                  TupleStreamContainerStorage<::mlir::Type>> {
public:
  using Base::Base;

  static GenericTupleStreamUnionType
  get(mlir::MLIRContext* context,
      TupleStreamContainerStorage<::mlir::Type>::ChildTypes const& funcTypes) {
    return Base::get<TupleStreamContainerStorage<::mlir::Type>::KeyTy>(context, funcTypes);
  }

  [[nodiscard]] TupleStreamContainerStorage<::mlir::Type>::ChildTypes const& getChildren() {
    return getImpl()->childTypes;
  }
};

class TupleStreamUnionType
    : public mlir::Type::TypeBase<TupleStreamUnionType, mlir::Type,
                                  TupleStreamContainerStorage<TupleStreamType>> {
public:
  using Base::Base;

  static TupleStreamUnionType
  get(mlir::MLIRContext* context,
      TupleStreamContainerStorage<TupleStreamType>::ChildTypes const& streamTypes) {
    return Base::get<TupleStreamContainerStorage<TupleStreamType>::KeyTy>(context, streamTypes);
  }

  [[nodiscard]] TupleStreamContainerStorage<TupleStreamType>::ChildTypes const& getTupleStreams() {
    return getImpl()->childTypes;
  }

  size_t getNumChildStreams() { return getImpl()->childTypes.size(); }
};

class RelationType : public mlir::Type::TypeBase<RelationType, mlir::Type, mlir::TypeStorage> {
  using Base::Base;
};