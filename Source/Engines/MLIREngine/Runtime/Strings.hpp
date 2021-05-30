#pragma once
#include <string>

namespace boss::mlir::runtime::string {
struct RuntimeString {
  char* data;
  size_t length;
};

extern "C" RuntimeString* allocateRuntimeString(size_t length);
extern "C" RuntimeString* allocateRuntimeStringReference(char* data, size_t length);

}
