#pragma once

#include "mlir/IR/Builders.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/StandardTypes.h"
#include "DatabaseTypes.h"
#include "Engines/MLIREngine/Types/Types.hpp"
#include <mlir/Interfaces/SideEffectInterfaces.h>

#define GET_OP_CLASSES
#include "DatabaseOps.h.inc"
