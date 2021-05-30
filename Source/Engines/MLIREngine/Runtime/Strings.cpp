#include "Strings.hpp"

boss::mlir::runtime::string::RuntimeString* boss::mlir::runtime::string::allocateRuntimeString(size_t length) {
  auto* result = static_cast<RuntimeString*>(malloc(sizeof(RuntimeString)));
  result->length = length;
  result->data = static_cast<char*>(malloc(length));
  return result;
}

boss::mlir::runtime::string::RuntimeString*
boss::mlir::runtime::string::allocateRuntimeStringReference(char* data, size_t length) {
  auto* result = static_cast<RuntimeString*>(malloc(sizeof(RuntimeString)));
  result->data = data;
  result->length = length;
  return result;
}

bool boss::mlir::runtime::string::runtimeStringCompare(
    boss::mlir::runtime::string::RuntimeString* lhs,
    boss::mlir::runtime::string::RuntimeString* rhs) {
  if (lhs->length != rhs->length) {
    return false;
  }
  return memcmp(lhs->data, rhs->data, lhs->length) == 0;
}
