#pragma once

#include <mlir/IR/StandardTypes.h>
#include <mlir/IR/TypeSupport.h>

using namespace mlir;

struct RelationTypeStorage : public TypeStorage {
  RelationTypeStorage(Type relationType) : relationType(relationType) {}

  using KeyTy = Type;

  bool operator==(const KeyTy& key) const { return key == relationType; }

  static RelationTypeStorage* construct(TypeStorageAllocator& allocator, const KeyTy& key) {
    return new(allocator.allocate<RelationTypeStorage>()) RelationTypeStorage(key);
  }

  Type relationType;
};

class RelationType : public Type::TypeBase<RelationType, Type, RelationTypeStorage> {
public:
  using Base::Base;

  static RelationType get(MLIRContext* context, Type type) {
    return Base::get<Type>(context, type);
  }

  static RelationType getChecked(MLIRContext* context, Type relationType) {
    return Base::get<Type>(context, relationType);
  }

  Type relationType() { return getImpl()->relationType; }
};