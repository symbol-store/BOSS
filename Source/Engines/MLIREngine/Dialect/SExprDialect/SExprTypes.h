#pragma once
#include <mlir/IR/Location.h>
#include <mlir/IR/StandardTypes.h>
#include <mlir/IR/TypeSupport.h>
#include <optional>
#include <utility>

namespace sexprtype {
enum class SymbolOrValue { SYMBOL, VALUE, UNKNOWN };
}; // namespace sexprtype

class StringType : public mlir::Type::TypeBase<StringType, mlir::Type, mlir::TypeStorage> {
public:
  using Base::Base;
};

struct SymbolOrValueTypeStorage : public ::mlir::TypeStorage {
  SymbolOrValueTypeStorage(sexprtype::SymbolOrValue isSymbol, llvm::Optional<::mlir::Type> baseType)
      : isSymbol(isSymbol), baseType(baseType) {}

  using KeyTy = std::pair<sexprtype::SymbolOrValue, llvm::Optional<::mlir::Type>>;

  bool operator==(const KeyTy& key) const { return key == KeyTy(isSymbol, baseType); }

  static llvm::hash_code hashKey(const KeyTy& key) {
    return llvm::hash_combine(key.first, key.second.getValueOr(nullptr));
  }

  static KeyTy getKey(sexprtype::SymbolOrValue isSymbol, ::mlir::Type baseType) {
    return KeyTy(isSymbol, baseType);
  }

  static SymbolOrValueTypeStorage* construct(::mlir::TypeStorageAllocator& allocator, const KeyTy& key) {
    return new(allocator.allocate<SymbolOrValueTypeStorage>())
        SymbolOrValueTypeStorage(key.first, key.second);
  }

  sexprtype::SymbolOrValue isSymbol;
  llvm::Optional<::mlir::Type> baseType;
};

class SymbolOrValueType : public ::mlir::Type::TypeBase<SymbolOrValueType, ::mlir::Type, SymbolOrValueTypeStorage> {
public:
  using Base::Base;

  static SymbolOrValueType get(::mlir::MLIRContext* context, sexprtype::SymbolOrValue sOrV,
                               llvm::Optional<Type> type) {
    return Base::get<sexprtype::SymbolOrValue, llvm::Optional<Type>>(context, sOrV, type);
  }

  static SymbolOrValueType getChecked(::mlir::MLIRContext* context, sexprtype::SymbolOrValue sOrV,
                                      llvm::Optional<Type> type) {
    return Base::get<sexprtype::SymbolOrValue, llvm::Optional<Type>>(context, sOrV, type);
  }

  static ::mlir::LogicalResult verifyConstructionInvariants(::mlir::Location location,             // NOLINT
                                                    sexprtype::SymbolOrValue sOrV, // NOLINT
                                                    llvm::Optional<Type> type) {   // NOLINT
    return ::mlir::success();
  }

  sexprtype::SymbolOrValue isSymbolic() { return getImpl()->isSymbol; }

  bool hasType() { return getImpl()->baseType.hasValue(); }

  Type getBaseType() { return getImpl()->baseType.getValue(); }

  llvm::Optional<Type> getBaseTypeChecked() { return getImpl()->baseType; }
};
