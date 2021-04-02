#pragma once
#include <mlir/IR/Location.h>
#include <mlir/IR/StandardTypes.h>
#include <mlir/IR/TypeSupport.h>
#include <optional>
#include <utility>

using namespace mlir;

namespace sexprtype {
enum class SymbolOrValue { SYMBOL, VALUE, UNKNOWN };
enum class ReturnTypes { STRING, INT, BOOLEAN, SYMBOL, RELATION, UNKNOWN };
}; // namespace sexprtype

struct StringTypeStorage : public TypeStorage {
  StringTypeStorage(size_t length) : length(length) {}

  using KeyTy = size_t;

  bool operator==(const KeyTy& key) const { return key == length; }

  static llvm::hash_code hashKey(const KeyTy& key) { return key; }

  static KeyTy getKey(size_t length) { return length; }

  static StringTypeStorage* construct(TypeStorageAllocator& allocator, const KeyTy& key) {
    return new(allocator.allocate<StringTypeStorage>()) StringTypeStorage(key);
  }

  size_t length;
};

class StringType : public Type::TypeBase<StringType, Type, StringTypeStorage> {
public:
  using Base::Base;

  static StringType get(MLIRContext* context, size_t length) {
    return Base::get<size_t>(context, length);
  }

  size_t getLength() { return getImpl()->length; }
};

struct SymbolOrValueTypeStorage : public TypeStorage {
  SymbolOrValueTypeStorage(sexprtype::SymbolOrValue isSymbol, llvm::Optional<Type> baseType)
      : isSymbol(isSymbol), baseType(baseType) {}

  using KeyTy = std::pair<sexprtype::SymbolOrValue, llvm::Optional<Type>>;

  bool operator==(const KeyTy& key) const { return key == KeyTy(isSymbol, baseType); }

  static llvm::hash_code hashKey(const KeyTy& key) {
    return llvm::hash_combine(key.first, key.second.getValueOr(nullptr));
  }

  static KeyTy getKey(sexprtype::SymbolOrValue isSymbol, Type baseType) {
    return KeyTy(isSymbol, baseType);
  }

  static SymbolOrValueTypeStorage* construct(TypeStorageAllocator& allocator, const KeyTy& key) {
    return new(allocator.allocate<SymbolOrValueTypeStorage>())
        SymbolOrValueTypeStorage(key.first, key.second);
  }

  sexprtype::SymbolOrValue isSymbol;
  llvm::Optional<Type> baseType;
};

class SymbolOrValueType : public Type::TypeBase<SymbolOrValueType, Type, SymbolOrValueTypeStorage> {
public:
  using Base::Base;

  static SymbolOrValueType get(MLIRContext* context, sexprtype::SymbolOrValue sOrV,
                               llvm::Optional<Type> type) {
    return Base::get<sexprtype::SymbolOrValue, llvm::Optional<Type>>(context, sOrV, type);
  }

  static SymbolOrValueType getChecked(MLIRContext* context, sexprtype::SymbolOrValue sOrV,
                                      llvm::Optional<Type> type) {
    return Base::get<sexprtype::SymbolOrValue, llvm::Optional<Type>>(context, sOrV, type);
  }

  static LogicalResult verifyConstructionInvariants(Location location,             // NOLINT
                                                    sexprtype::SymbolOrValue sOrV, // NOLINT
                                                    llvm::Optional<Type> type) {   // NOLINT
    return success();
  }

  sexprtype::SymbolOrValue isSymbolic() { return getImpl()->isSymbol; }

  bool hasType() { return getImpl()->baseType.hasValue(); }

  Type getBaseType() { return getImpl()->baseType.getValue(); }

  llvm::Optional<Type> getBaseTypeChecked() { return getImpl()->baseType; }
};
