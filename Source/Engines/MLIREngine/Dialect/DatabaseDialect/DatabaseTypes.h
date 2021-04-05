#pragma once

#include <mlir/IR/StandardTypes.h>
#include <mlir/IR/TypeSupport.h>
#include "Engines/MLIREngine/Runtime/Database.hpp"

struct TupleStreamTypeStorage : public mlir::TypeStorage {
  TupleStreamTypeStorage(runtime::Table* relation) : relation(relation) {}

  using KeyTy = runtime::Table*;

  bool operator==(const KeyTy key) const { return (*key) == (*relation); }

  static llvm::hash_code hashKey(const KeyTy key) { return llvm::hash_value(key->getSchema()->fingerprint()); }

  static TupleStreamTypeStorage* construct(mlir::TypeStorageAllocator& allocator, KeyTy key) {
    return new(allocator.allocate<TupleStreamTypeStorage>()) TupleStreamTypeStorage(key);
  }

  runtime::Table* relation;
};

class TupleStreamType : public mlir::Type::TypeBase<TupleStreamType, mlir::Type, TupleStreamTypeStorage> {
public:
  using Base::Base;

  static TupleStreamType get(mlir::MLIRContext* context, runtime::Table* relation) {
    return Base::get<runtime::Table*>(context, relation);
  }
};