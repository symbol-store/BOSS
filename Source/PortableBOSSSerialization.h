#ifndef PORTABLEBOSSSERIALIZATION_H
#define PORTABLEBOSSSERIALIZATION_H
#ifdef __cplusplus
#include <cinttypes>
#include <cstring>
extern "C" {
#else
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#endif
// NOLINTBEGIN(hicpp-use-auto,cppcoreguidelines-pro-type-union-access)

#include <stdlib.h>

//////////////////////////////// Helper Functions ///////////////////////////////

static uint64_t alignTo8Bytes(uint64_t bytes) { return (bytes + (uint64_t)7) & -(uint64_t)8; }

//////////////////////////////// Data Structures ///////////////////////////////

typedef size_t PortableBOSSString;
typedef size_t PortableBOSSExpressionIndex;

union PortableBOSSArgumentValue {
  bool asBool;
  int8_t asChar;
  int16_t asShort;
  int32_t asInt;
  int64_t asLong;
  float asFloat;
  double asDouble;
  PortableBOSSString asString;
  PortableBOSSExpressionIndex asExpression;
};

#ifdef __cplusplus
constexpr uint64_t PortableBOSSArgument_BOOL_SIZE = sizeof(bool);
constexpr uint64_t PortableBOSSArgument_CHAR_SIZE = sizeof(int8_t);
constexpr uint64_t PortableBOSSArgument_SHORT_SIZE = sizeof(int16_t);
constexpr uint64_t PortableBOSSArgument_INT_SIZE = sizeof(int32_t);
constexpr uint64_t PortableBOSSArgument_LONG_SIZE = sizeof(int64_t);
constexpr uint64_t PortableBOSSArgument_FLOAT_SIZE = sizeof(float_t);
constexpr uint64_t PortableBOSSArgument_DOUBLE_SIZE = sizeof(double_t);
constexpr uint64_t PortableBOSSArgument_STRING_SIZE = sizeof(PortableBOSSString);
constexpr uint64_t PortableBOSSArgument_EXPRESSION_SIZE = sizeof(PortableBOSSExpressionIndex);
#else
static uint64_t const PortableBOSSArgument_BOOL_SIZE = sizeof(bool);
static uint64_t const PortableBOSSArgument_CHAR_SIZE = sizeof(int8_t);
static uint64_t const PortableBOSSArgument_SHORT_SIZE = sizeof(int16_t);
static uint64_t const PortableBOSSArgument_INT_SIZE = sizeof(int32_t);
static uint64_t const PortableBOSSArgument_LONG_SIZE = sizeof(int64_t);
static uint64_t const PortableBOSSArgument_FLOAT_SIZE = sizeof(float_t);
static uint64_t const PortableBOSSArgument_DOUBLE_SIZE = sizeof(double_t);
static uint64_t const PortableBOSSArgument_STRING_SIZE = sizeof(PortableBOSSString);
static uint64_t const PortableBOSSArgument_EXPRESSION_SIZE = sizeof(PortableBOSSExpressionIndex);
#endif

enum PortableBOSSArgumentType : uint8_t {
  ARGUMENT_TYPE_BOOL,
  ARGUMENT_TYPE_CHAR,
  ARGUMENT_TYPE_SHORT,
  ARGUMENT_TYPE_INT,
  ARGUMENT_TYPE_LONG,
  ARGUMENT_TYPE_FLOAT,
  ARGUMENT_TYPE_DOUBLE,
  ARGUMENT_TYPE_STRING,
  ARGUMENT_TYPE_SYMBOL,
  ARGUMENT_TYPE_EXPRESSION
};

static uint8_t const PortableBOSSArgumentType_RLE_MINIMUM_SIZE =
    13; // assuming PortableBOSSArgumentType ideally stored in 1 byte only,
        // to store RLE-type, need 1 byte to declare the type and 4 bytes to define the length

static uint8_t const PortableBOSSArgumentType_RLE_BIT =
    0x80; // first bit of PortableBOSSArgumentType to set RLE on/off

static uint8_t const PortableBOSSArgumentType_MASK =
    0x0F; // used to clear the top 4 bits of an argument type

struct PortableBOSSExpression {
  uint64_t symbolNameOffset;
  uint64_t startChildOffset;     // Arg buffer offset
  uint64_t endChildOffset;       // Arg buffer offset
  uint64_t startChildTypeOffset; // Type buffer offset
  uint64_t endChildTypeOffset;   // Type buffer offset
};

/**
 * A single-allocation representation of an expression, including its arguments (i.e., a flattened
 * array of all arguments, another flattened array of argument types and an array of
 * PortableExpressions to encode the structure)
 */
struct PortableBOSSRootExpression {
  uint64_t const argumentCount;      // if used directly for type bytes -- align to 8 bytes
  uint64_t const argumentBytesCount; // if used directly -- align to 8 bytes
  uint64_t const expressionCount;
  uint64_t const argumentDictionaryBytesCount;
  void* const originalAddress;
  /**
   * The index of the last used byte in the arguments buffer relative to the pointer returned by
   * getStringBuffer()
   */
  size_t stringArgumentsFillIndex;

  /**
   * This buffer holds all data associated with the expression in a single untyped array. As the
   * three kinds of data (ArgumentsValues, ArgumentTypes and Expressions) have different sizes,
   * holding them in an array of unions would waste a lot of memory. A union of variable-sized
   * arrays is not supported in ANSI C. So it is held in an untyped buffer which is essentially a
   * concatenation of the three types of buffers that are required. Utility functions exist to
   * extract the different sub-arrays.
   */
  char arguments[];
};

/////////////////////////// Argument-type region format ////////////////////////
//
// The argument-TYPE region used to be, unconditionally, one byte per argument
// (`alignTo8Bytes(argumentCount)` bytes).  That is 34-49% of a compressed Wisent
// file and it is almost entirely constant: measured on SF1 lineitem, 126 runs in
// 66,969,456 bytes.
//
// Format v1 replaces it with one descriptor byte per EXPRESSION NODE plus a small
// overflow area.  It changes PHYSICAL STORAGE ONLY.  `startChildTypeOffset` and
// `endChildTypeOffset` remain LOGICAL indices, so `endChildTypeOffset -
// startChildTypeOffset` is still the element count everywhere.
//
// Which layout a buffer uses is recorded in the otherwise-dead `originalAddress`
// slot of the 48-byte header, which the file writers fill with eight zero bytes.
// The encoding deliberately requires a magic in the TOP 16 BITS, which a
// user-space heap pointer never has: so a buffer whose `originalAddress` still
// holds a real pointer (as every in-memory allocation does) reads as v0, which is
// the correct and conservative answer for an in-memory root.
//
//   metadata == 0                 -> v0 (every .bin written so far)
//   top 16 bits == 0xB055         -> bits[47:40] version, bits[39:0] physical bytes
//   anything else (e.g. pointer)  -> v0
//
// v1 region layout:
//   [0]                                : uint8 residualCount R
//   [1, 1+R)                           : the R type bytes of the uncovered logical
//                                        prefix (arguments owned by no node; in
//                                        practice exactly one, logical index 0)
//   [1+R, 1+R+expressionCount)         : one descriptor byte per expression node
//   [...]                              : overflow bytes for EXPLICIT nodes, in node
//                                        order
//
// Descriptor byte:
//   0x00..0x0F : the whole node is a constant run of that argument type
//   0x10..0x1F : the node is a legacy `setRLEArgumentFlagOrPropagateTypes` artefact
//                for that type -- byte 0 has the RLE bit, bytes 1..4 hold the
//                length, the rest are the plain type.  Reproduced byte-exactly.
//   0xFF       : heterogeneous; the node's bytes are stored verbatim in the
//                overflow area.
// Measured on the eight SF1 tables, those three cases cover 100% of nodes, and
// expand(encode(types)) is byte-identical to the original region.

#define PORTABLEBOSS_FORMAT_MAGIC ((uint64_t)0xB055)
#define PORTABLEBOSS_FORMAT_MAGIC_SHIFT 48
#define PORTABLEBOSS_FORMAT_VERSION_SHIFT 40
#define PORTABLEBOSS_FORMAT_SIZE_MASK (((uint64_t)1 << 40) - 1)

/** Highest argument-type region format this build can WRITE. */
#define PORTABLEBOSS_FORMAT_VERSION_PER_NODE_TYPES 1

#define PORTABLEBOSS_TYPE_DESCRIPTOR_CONST 0x00
#define PORTABLEBOSS_TYPE_DESCRIPTOR_LEGACY_RLE 0x10
#define PORTABLEBOSS_TYPE_DESCRIPTOR_EXPLICIT 0xFF

static uint64_t portableBOSSMakeFormatMetadata(uint8_t version, uint64_t typeRegionBytes) {
  return (PORTABLEBOSS_FORMAT_MAGIC << PORTABLEBOSS_FORMAT_MAGIC_SHIFT) |
         ((uint64_t)version << PORTABLEBOSS_FORMAT_VERSION_SHIFT) |
         (typeRegionBytes & PORTABLEBOSS_FORMAT_SIZE_MASK);
}

static uint64_t portableBOSSRawFormatMetadata(struct PortableBOSSRootExpression const* root) {
  uint64_t metadata;
  memcpy(&metadata, &root->originalAddress, sizeof(metadata));
  return metadata;
}

/** 0 for the original flat-byte-array layout, 1 for the per-node layout. */
static uint8_t portableBOSSFormatVersion(struct PortableBOSSRootExpression const* root) {
  uint64_t const metadata = portableBOSSRawFormatMetadata(root);
  if((metadata >> PORTABLEBOSS_FORMAT_MAGIC_SHIFT) != PORTABLEBOSS_FORMAT_MAGIC) {
    return 0;
  }
  return (uint8_t)((metadata >> PORTABLEBOSS_FORMAT_VERSION_SHIFT) & 0xFF);
}

/**
 * The PHYSICAL, 8-byte-aligned size of the argument-type region.  Every offset
 * computation that used to spell `alignTo8Bytes(argumentCount * sizeof(enum
 * PortableBOSSArgumentType))` must go through this instead.
 */
static uint64_t getArgumentTypesBytesCount(struct PortableBOSSRootExpression const* root) {
  if(portableBOSSFormatVersion(root) == 0) {
    return alignTo8Bytes(root->argumentCount * sizeof(enum PortableBOSSArgumentType));
  }
  return alignTo8Bytes(portableBOSSRawFormatMetadata(root) & PORTABLEBOSS_FORMAT_SIZE_MASK);
}


//////////////////////////////// Part Extraction ///////////////////////////////

struct PortableBOSSRootExpression* getDummySerializedExpression();
static union PortableBOSSArgumentValue*
getExpressionArguments(struct PortableBOSSRootExpression* root) {
  return (union PortableBOSSArgumentValue*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      root->arguments;
}

static enum PortableBOSSArgumentType* getArgumentTypes(struct PortableBOSSRootExpression* root) {
  return (enum PortableBOSSArgumentType*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      &root->arguments[alignTo8Bytes(root->argumentBytesCount)];
}

static struct PortableBOSSExpression*
getExpressionSubexpressions(struct PortableBOSSRootExpression* root) {
  return (struct PortableBOSSExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      &root->arguments[alignTo8Bytes(root->argumentBytesCount) + getArgumentTypesBytesCount(root)];
}

static char* getStringBuffer(struct PortableBOSSRootExpression* root) {
  return (char*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      &root->arguments[alignTo8Bytes(root->argumentBytesCount) + getArgumentTypesBytesCount(root) +
                       root->expressionCount * (sizeof(struct PortableBOSSExpression)) +
                       root->argumentDictionaryBytesCount];
}

///////////////////////// Argument-type region codec //////////////////////////
//
// C++ only: these are the encode/expand helpers used by the Wisent file writer
// and reader.  They are the ONLY places that know the v1 physical layout.

#ifdef __cplusplus
} // extern "C"

#include <vector>

/** Describes how node `i`'s type bytes are stored, or reports that they cannot be. */
struct PortableBOSSTypeDescriptor {
  uint8_t descriptor;
  uint64_t overflowBytes; // non-zero only for PORTABLEBOSS_TYPE_DESCRIPTOR_EXPLICIT
};

inline PortableBOSSTypeDescriptor
portableBOSSDescribeNodeTypes(enum PortableBOSSArgumentType const* types, uint64_t start,
                              uint64_t end) {
  uint64_t const n = end - start;
  PortableBOSSTypeDescriptor out{PORTABLEBOSS_TYPE_DESCRIPTOR_EXPLICIT, n};
  if(n == 0) {
    // An empty node carries no bytes; record it as a constant of type 0 so that it
    // costs one descriptor byte and expands to nothing.
    out.descriptor = (uint8_t)PORTABLEBOSS_TYPE_DESCRIPTOR_CONST;
    out.overflowBytes = 0;
    return out;
  }
  uint8_t const* bytes = (uint8_t const*)types + start;
  uint8_t const first = bytes[0];

  bool constant = true;
  for(uint64_t i = 1; i < n; ++i) {
    if(bytes[i] != first) {
      constant = false;
      break;
    }
  }
  if(constant && first <= 0x0F) {
    out.descriptor = (uint8_t)(PORTABLEBOSS_TYPE_DESCRIPTOR_CONST | first);
    out.overflowBytes = 0;
    return out;
  }
  // Legacy setRLEArgumentFlagOrPropagateTypes artefact: the writer sets the RLE bit on
  // byte 0 and clobbers bytes 1..4 with the little-endian run length, leaving the rest
  // as the plain type.  Recognise it so those (very large) nodes still cost one byte.
  if(n >= PortableBOSSArgumentType_RLE_MINIMUM_SIZE && (first & PortableBOSSArgumentType_RLE_BIT) &&
     n <= 0xFFFFFFFFull) {
    uint8_t const type = (uint8_t)(first & (uint8_t)~PortableBOSSArgumentType_RLE_BIT);
    uint32_t const length = (uint32_t)n;
    bool matches = type <= 0x0F && bytes[1] == (uint8_t)(length & 0xFF) &&
                   bytes[2] == (uint8_t)((length >> 8) & 0xFF) &&
                   bytes[3] == (uint8_t)((length >> 16) & 0xFF) &&
                   bytes[4] == (uint8_t)((length >> 24) & 0xFF);
    if(matches) {
      for(uint64_t i = 5; i < n; ++i) {
        if(bytes[i] != type) {
          matches = false;
          break;
        }
      }
    }
    if(matches) {
      out.descriptor = (uint8_t)(PORTABLEBOSS_TYPE_DESCRIPTOR_LEGACY_RLE | type);
      out.overflowBytes = 0;
      return out;
    }
  }
  return out; // EXPLICIT, n overflow bytes
}

/** Writes node `i`'s `n` type bytes back out from its descriptor. */
inline void portableBOSSExpandNodeTypes(uint8_t descriptor, uint8_t const* overflow, uint64_t n,
                                        uint8_t* out) {
  if(n == 0) {
    return;
  }
  if(descriptor == PORTABLEBOSS_TYPE_DESCRIPTOR_EXPLICIT) {
    memcpy(out, overflow, n);
    return;
  }
  uint8_t const type = (uint8_t)(descriptor & 0x0F);
  memset(out, type, n);
  if((descriptor & 0xF0) == PORTABLEBOSS_TYPE_DESCRIPTOR_LEGACY_RLE) {
    uint32_t const length = (uint32_t)n;
    out[0] = (uint8_t)(type | PortableBOSSArgumentType_RLE_BIT);
    out[1] = (uint8_t)(length & 0xFF);
    out[2] = (uint8_t)((length >> 8) & 0xFF);
    out[3] = (uint8_t)((length >> 16) & 0xFF);
    out[4] = (uint8_t)((length >> 24) & 0xFF);
  }
}

/////////////////////// Random access into a v1 region ////////////////////////
//
// The LAZY (Gather/TableManager) reader must answer "what is the type byte at
// LOGICAL argument-type index i?" WITHOUT materialising the v0 array: its buffer
// is exactly the file, and the v0 array for SF1 lineitem is 67 MB.  It does not
// have to.  The whole v1 descriptor table is ~120 bytes, and the expression table
// (which the lazy reader already fetches in full at init) carries every node's
// [startChildTypeOffset, endChildTypeOffset).  So a single pass over the nodes
// resolves any logical index, or any short window of them, directly.
//
// The two functions below are the ONLY random-access readers of a v1 region, and
// they are required to agree, byte for byte, with portableBOSSExpandToV0 --
// PortableBOSSFormatTests checks exactly that on every logical index of every
// test file.

/** The type byte at `offset` within a node of `n` arguments described by `descriptor`. */
inline uint8_t portableBOSSTypeByteInNode(uint8_t descriptor, uint8_t const* overflow, uint64_t n,
                                          uint64_t offset) {
  if(descriptor == PORTABLEBOSS_TYPE_DESCRIPTOR_EXPLICIT) {
    return overflow[offset];
  }
  uint8_t const type = (uint8_t)(descriptor & 0x0F);
  // The legacy setRLEArgumentFlagOrPropagateTypes artefact: byte 0 carries the RLE bit
  // and bytes 1..4 the little-endian run length.  Only produced for n >= 13
  // (PortableBOSSArgumentType_RLE_MINIMUM_SIZE), so offset < 5 is always inside the node.
  if((descriptor & 0xF0) == PORTABLEBOSS_TYPE_DESCRIPTOR_LEGACY_RLE && offset < 5) {
    if(offset == 0) {
      return (uint8_t)(type | PortableBOSSArgumentType_RLE_BIT);
    }
    uint32_t const length = (uint32_t)n;
    return (uint8_t)((length >> (8 * (offset - 1))) & 0xFF);
  }
  return type;
}

/**
 * Writes the type bytes for logical indices [start, end) of a v1 `root` into `out`,
 * which must hold `end - start` bytes.  One pass over the expression table, so a whole
 * window (e.g. the 13-byte RLE look-back) costs the same as a single index.
 *
 * `root` must be v1 AND must have both its type region and its whole expression table
 * resident -- in the lazy reader, TableManager::init guarantees both.
 *
 * Logical slots that no node covers (other than the recorded prefix) read as 0, which
 * is what the zero-initialised v0 region holds for them.
 */
inline void portableBOSSExpandTypeRangeV1(struct PortableBOSSRootExpression* root, uint64_t start,
                                          uint64_t end, uint8_t* out) {
  if(end <= start) {
    return;
  }
  memset(out, 0, (size_t)(end - start));
  uint8_t const* region = (uint8_t const*)getArgumentTypes(root);
  uint64_t const residual = region[0];
  uint64_t const expressionCount = root->expressionCount;
  uint8_t const* descriptors = region + 1 + residual;
  uint8_t const* overflow = descriptors + expressionCount;
  for(uint64_t i = start; i < end && i < residual; ++i) {
    out[i - start] = region[1 + i];
  }
  struct PortableBOSSExpression const* exprs = getExpressionSubexpressions(root);
  for(uint64_t k = 0; k < expressionCount; ++k) {
    uint64_t const nodeStart = exprs[k].startChildTypeOffset;
    uint64_t const nodeEnd = exprs[k].endChildTypeOffset;
    uint64_t const n = nodeEnd - nodeStart;
    uint8_t const descriptor = descriptors[k];
    uint64_t const lo = nodeStart > start ? nodeStart : start;
    uint64_t const hi = nodeEnd < end ? nodeEnd : end;
    for(uint64_t i = lo; i < hi; ++i) {
      out[i - start] = portableBOSSTypeByteInNode(descriptor, overflow, n, i - nodeStart);
    }
    // The encoder writes overflow in NODE order, so this must accumulate in node order
    // too -- not in index order.  Nodes never overlap (the encoder refuses if they do).
    if(descriptor == PORTABLEBOSS_TYPE_DESCRIPTOR_EXPLICIT) {
      overflow += n;
    }
  }
}

/** The type byte at one logical argument-type index of a v1 `root`. */
inline uint8_t portableBOSSArgumentTypeAtV1(struct PortableBOSSRootExpression* root,
                                            uint64_t index) {
  uint8_t byte = 0;
  portableBOSSExpandTypeRangeV1(root, index, index + 1, &byte);
  return byte;
}

/**
 * Builds the v1 physical type region for `root` (which must be v0).  Returns false --
 * and the caller must then keep the v0 layout -- if the expression table does not
 * cover the logical type space in the shape v1 can describe.  Never lossy: whatever
 * this accepts, portableBOSSExpandToV0 reproduces byte for byte.
 */
inline bool portableBOSSBuildV1TypeRegion(struct PortableBOSSRootExpression* root,
                                          std::vector<char>& region) {
  if(portableBOSSFormatVersion(root) != 0) {
    return false;
  }
  uint64_t const argumentCount = root->argumentCount;
  uint64_t const expressionCount = root->expressionCount;
  enum PortableBOSSArgumentType const* types = getArgumentTypes(root);
  struct PortableBOSSExpression const* exprs = getExpressionSubexpressions(root);

  // Work out which logical type slots no node owns.  In every file produced so far that
  // is exactly slot 0 (the root's own argument).  v1 stores such a prefix verbatim; if
  // the uncovered slots are not a short prefix, refuse rather than lose them.
  std::vector<uint8_t> covered(argumentCount, 0);
  for(uint64_t i = 0; i < expressionCount; ++i) {
    uint64_t const start = exprs[i].startChildTypeOffset;
    uint64_t const end = exprs[i].endChildTypeOffset;
    if(start > end || end > argumentCount) {
      return false;
    }
    for(uint64_t j = start; j < end; ++j) {
      if(covered[j] != 0) {
        return false; // overlapping nodes: v1 cannot describe them
      }
      covered[j] = 1;
    }
  }
  uint64_t residual = 0;
  while(residual < argumentCount && covered[residual] == 0) {
    ++residual;
  }
  if(residual > 0xFF) {
    return false;
  }
  for(uint64_t j = residual; j < argumentCount; ++j) {
    if(covered[j] == 0) {
      return false; // a hole in the middle: refuse
    }
  }

  std::vector<PortableBOSSTypeDescriptor> descriptors(expressionCount);
  uint64_t overflowBytes = 0;
  for(uint64_t i = 0; i < expressionCount; ++i) {
    descriptors[i] = portableBOSSDescribeNodeTypes(types, exprs[i].startChildTypeOffset,
                                                   exprs[i].endChildTypeOffset);
    overflowBytes += descriptors[i].overflowBytes;
  }

  uint64_t const logicalSize = 1 + residual + expressionCount + overflowBytes;
  if(logicalSize > PORTABLEBOSS_FORMAT_SIZE_MASK) {
    return false;
  }
  region.assign((size_t)alignTo8Bytes(logicalSize), 0);
  uint8_t* out = (uint8_t*)region.data();
  out[0] = (uint8_t)residual;
  memcpy(out + 1, (uint8_t const*)types, (size_t)residual);
  uint8_t* descriptorBytes = out + 1 + residual;
  uint8_t* overflow = descriptorBytes + expressionCount;
  for(uint64_t i = 0; i < expressionCount; ++i) {
    descriptorBytes[i] = descriptors[i].descriptor;
    if(descriptors[i].overflowBytes > 0) {
      memcpy(overflow, (uint8_t const*)types + exprs[i].startChildTypeOffset,
             (size_t)descriptors[i].overflowBytes);
      overflow += descriptors[i].overflowBytes;
    }
  }
  return true;
}

/** Rebuilds the whole root buffer with the v1 type region.  `root` must be v0. */
inline bool portableBOSSEncodeToV1(struct PortableBOSSRootExpression* root,
                                   std::vector<char>& encoded) {
  std::vector<char> region;
  if(!portableBOSSBuildV1TypeRegion(root, region)) {
    return false;
  }
  uint64_t const argumentsBytes = alignTo8Bytes(root->argumentBytesCount);
  uint64_t const oldTypeBytes = getArgumentTypesBytesCount(root);
  uint64_t const tailBytes = root->expressionCount * sizeof(struct PortableBOSSExpression) +
                             root->argumentDictionaryBytesCount + root->stringArgumentsFillIndex;
  // The stored size is the region we actually emit (already 8-byte aligned).
  encoded.assign((size_t)(sizeof(struct PortableBOSSRootExpression) + argumentsBytes +
                          region.size() + tailBytes),
                 0);
  memcpy(encoded.data(), root, sizeof(struct PortableBOSSRootExpression));
  auto* out = reinterpret_cast<struct PortableBOSSRootExpression*>(encoded.data());
  *((uint64_t*)&out->originalAddress) =
      portableBOSSMakeFormatMetadata(PORTABLEBOSS_FORMAT_VERSION_PER_NODE_TYPES, region.size());
  memcpy(out->arguments, root->arguments, (size_t)argumentsBytes);
  memcpy(out->arguments + argumentsBytes, region.data(), region.size());
  memcpy(out->arguments + argumentsBytes + region.size(),
         root->arguments + argumentsBytes + oldTypeBytes, (size_t)tailBytes);
  return true;
}

/**
 * Rebuilds the whole root buffer in the v0 layout from a v1 one, so that every existing
 * consumer of getArgumentTypes() keeps working unchanged.  v1 is a storage/transport
 * format; this is the one place it is turned back into the flat array.
 */
inline bool portableBOSSExpandToV0(struct PortableBOSSRootExpression* root,
                                   std::vector<char>& expanded) {
  if(portableBOSSFormatVersion(root) != PORTABLEBOSS_FORMAT_VERSION_PER_NODE_TYPES) {
    return false;
  }
  uint64_t const argumentCount = root->argumentCount;
  uint64_t const expressionCount = root->expressionCount;
  uint64_t const argumentsBytes = alignTo8Bytes(root->argumentBytesCount);
  uint64_t const regionBytes = getArgumentTypesBytesCount(root);
  uint64_t const newTypeBytes =
      alignTo8Bytes(argumentCount * sizeof(enum PortableBOSSArgumentType));
  uint64_t const tailBytes = expressionCount * sizeof(struct PortableBOSSExpression) +
                             root->argumentDictionaryBytesCount + root->stringArgumentsFillIndex;

  uint8_t const* region = (uint8_t const*)root->arguments + argumentsBytes;
  uint64_t const residual = region[0];
  if(1 + residual + expressionCount > regionBytes) {
    return false;
  }
  uint8_t const* descriptorBytes = region + 1 + residual;
  uint8_t const* overflow = descriptorBytes + expressionCount;
  struct PortableBOSSExpression const* exprs = getExpressionSubexpressions(root);

  expanded.assign((size_t)(sizeof(struct PortableBOSSRootExpression) + argumentsBytes +
                           newTypeBytes + tailBytes),
                  0);
  memcpy(expanded.data(), root, sizeof(struct PortableBOSSRootExpression));
  auto* out = reinterpret_cast<struct PortableBOSSRootExpression*>(expanded.data());
  *((uint64_t*)&out->originalAddress) = 0; // v0
  memcpy(out->arguments, root->arguments, (size_t)argumentsBytes);
  memcpy(out->arguments + argumentsBytes + newTypeBytes,
         root->arguments + argumentsBytes + regionBytes, (size_t)tailBytes);

  uint8_t* types = (uint8_t*)(out->arguments + argumentsBytes);
  memcpy(types, region + 1, (size_t)residual);
  for(uint64_t i = 0; i < expressionCount; ++i) {
    uint64_t const start = exprs[i].startChildTypeOffset;
    uint64_t const end = exprs[i].endChildTypeOffset;
    if(start > end || end > argumentCount) {
      return false;
    }
    uint64_t const n = end - start;
    portableBOSSExpandNodeTypes(descriptorBytes[i], overflow, n, types + start);
    if(descriptorBytes[i] == PORTABLEBOSS_TYPE_DESCRIPTOR_EXPLICIT) {
      overflow += n;
    }
  }
  return true;
}

extern "C" {
#endif // __cplusplus

//////////////////////////////   Memory Management /////////////////////////////

static struct PortableBOSSRootExpression*
allocateExpressionTree(uint64_t argumentCount, uint64_t expressionCount, uint64_t stringBytesCount,
                       void* (*allocateFunction)(size_t)) {
  struct PortableBOSSRootExpression* root =
      (struct PortableBOSSRootExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      allocateFunction(                    // NOLINT(hicpp-no-malloc,cppcoreguidelines-no-malloc)
          sizeof(struct PortableBOSSRootExpression) +
          sizeof(union PortableBOSSArgumentValue) * argumentCount +
          alignTo8Bytes(sizeof(enum PortableBOSSArgumentType) * argumentCount) +
          sizeof(struct PortableBOSSExpression) * expressionCount + stringBytesCount);
  *((uint64_t*)&root->argumentCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentCount;
  *((uint64_t*)&root->argumentBytesCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentCount * sizeof(union PortableBOSSArgumentValue);
  *((uint64_t*)&root->expressionCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      expressionCount;
  *((uint64_t*)&root
        ->argumentDictionaryBytesCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      0;
  *((uint64_t*)&root->stringArgumentsFillIndex) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      0;
  *((void**)&root->originalAddress) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      root;
  return root;
}

static struct PortableBOSSRootExpression*
allocateExpressionTree(uint64_t argumentCount, uint64_t argumentBytesCount,
                       uint64_t expressionCount, uint64_t stringBytesCount,
                       void* (*allocateFunction)(size_t)) {
  struct PortableBOSSRootExpression* root =
      (struct PortableBOSSRootExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      allocateFunction(                    // NOLINT(hicpp-no-malloc,cppcoreguidelines-no-malloc)
          sizeof(struct PortableBOSSRootExpression) + alignTo8Bytes(argumentBytesCount) +
          alignTo8Bytes(sizeof(enum PortableBOSSArgumentType) * argumentCount) +
          sizeof(struct PortableBOSSExpression) * expressionCount + stringBytesCount);
  *((uint64_t*)&root->argumentCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentCount;
  *((uint64_t*)&root->argumentBytesCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      argumentBytesCount;
  *((uint64_t*)&root->expressionCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      expressionCount;
  *((uint64_t*)&root
        ->argumentDictionaryBytesCount) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      0;
  *((uint64_t*)&root->stringArgumentsFillIndex) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      0;
  *((void**)&root->originalAddress) = // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      root;
  return root;
}

static void freeExpressionTree(struct PortableBOSSRootExpression* root,
                               void (*freeFunction)(void*)) {
  freeFunction(root); // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
}

static uint64_t* makeArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {

  return (uint64_t*)&getExpressionArguments(root)[argumentOutputI];
};

static bool* makeBoolArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_BOOL = PortableBOSSArgumentType::ARGUMENT_TYPE_BOOL;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_BOOL;
  return &getExpressionArguments(root)[argumentOutputI].asBool;
};

static bool* makeBoolArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                              uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_BOOL = PortableBOSSArgumentType::ARGUMENT_TYPE_BOOL;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_BOOL;
  return &getExpressionArguments(root)[argumentOutputI].asBool;
};

static void makeBoolArgumentType(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_BOOL = PortableBOSSArgumentType::ARGUMENT_TYPE_BOOL;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_BOOL;
};

static int8_t* makeCharArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_CHAR = PortableBOSSArgumentType::ARGUMENT_TYPE_CHAR;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_CHAR;
  return &getExpressionArguments(root)[argumentOutputI].asChar;
};

static int8_t* makeCharArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_CHAR = PortableBOSSArgumentType::ARGUMENT_TYPE_CHAR;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_CHAR;
  return &getExpressionArguments(root)[argumentOutputI].asChar;
};

static void makeCharArgumentType(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_CHAR = PortableBOSSArgumentType::ARGUMENT_TYPE_CHAR;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_CHAR;
};

static int16_t* makeShortArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SHORT = PortableBOSSArgumentType::ARGUMENT_TYPE_SHORT;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_SHORT;
  return &getExpressionArguments(root)[argumentOutputI].asShort;
};

static int16_t* makeShortArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                  uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SHORT = PortableBOSSArgumentType::ARGUMENT_TYPE_SHORT;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_SHORT;
  return &getExpressionArguments(root)[argumentOutputI].asShort;
};

static void makeShortArgumentType(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SHORT = PortableBOSSArgumentType::ARGUMENT_TYPE_SHORT;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_SHORT;
};

static int32_t* makeIntArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_INT = PortableBOSSArgumentType::ARGUMENT_TYPE_INT;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_INT;
  return &getExpressionArguments(root)[argumentOutputI].asInt;
};

static int32_t* makeIntArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_INT = PortableBOSSArgumentType::ARGUMENT_TYPE_INT;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_INT;
  return &getExpressionArguments(root)[argumentOutputI].asInt;
};

static void makeIntArgumentType(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_INT = PortableBOSSArgumentType::ARGUMENT_TYPE_INT;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_INT;
};

static int64_t* makeLongArgument(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_LONG = PortableBOSSArgumentType::ARGUMENT_TYPE_LONG;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_LONG;
  return &getExpressionArguments(root)[argumentOutputI].asLong;
};

static int64_t* makeLongArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                 uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_LONG = PortableBOSSArgumentType::ARGUMENT_TYPE_LONG;
#endif

  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_LONG;
  return &getExpressionArguments(root)[argumentOutputI].asLong;
};

static void makeLongArgumentType(struct PortableBOSSRootExpression* root,
                                 uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_LONG = PortableBOSSArgumentType::ARGUMENT_TYPE_LONG;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_LONG;
};

static float* makeFloatArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_FLOAT = PortableBOSSArgumentType::ARGUMENT_TYPE_FLOAT;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_FLOAT;
  return &getExpressionArguments(root)[argumentOutputI].asFloat;
};

static float* makeFloatArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_FLOAT = PortableBOSSArgumentType::ARGUMENT_TYPE_FLOAT;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_FLOAT;
  return &getExpressionArguments(root)[argumentOutputI].asFloat;
};

static void makeFloatArgumentType(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_FLOAT = PortableBOSSArgumentType::ARGUMENT_TYPE_FLOAT;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_FLOAT;
};

static double* makeDoubleArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_DOUBLE = PortableBOSSArgumentType::ARGUMENT_TYPE_DOUBLE;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_DOUBLE;
  return &getExpressionArguments(root)[argumentOutputI].asDouble;
};

static double* makeDoubleArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                  uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_DOUBLE = PortableBOSSArgumentType::ARGUMENT_TYPE_DOUBLE;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_DOUBLE;
  return &getExpressionArguments(root)[argumentOutputI].asDouble;
};

static void makeDoubleArgumentType(struct PortableBOSSRootExpression* root,
                                   uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_DOUBLE = PortableBOSSArgumentType::ARGUMENT_TYPE_DOUBLE;
#endif

  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_DOUBLE;
};

static size_t* makeStringArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_STRING = PortableBOSSArgumentType::ARGUMENT_TYPE_STRING;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_STRING;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static size_t* makeStringArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                  uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_STRING = PortableBOSSArgumentType::ARGUMENT_TYPE_STRING;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_STRING;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static void makeStringArgumentType(struct PortableBOSSRootExpression* root,
                                   uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_STRING = PortableBOSSArgumentType::ARGUMENT_TYPE_STRING;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_STRING;
};

static size_t* makeSymbolArgument(struct PortableBOSSRootExpression* root,
                                  uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_SYMBOL;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_SYMBOL;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static size_t* makeSymbolArgument(struct PortableBOSSRootExpression* root, uint64_t argumentOutputI,
                                  uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_SYMBOL;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_SYMBOL;
  return &getExpressionArguments(root)[argumentOutputI].asString;
};

static void makeSymbolArgumentType(struct PortableBOSSRootExpression* root,
                                   uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_SYMBOL;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_SYMBOL;
};

static size_t* makeExpressionArgument(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_EXPRESSION;
#endif
  getArgumentTypes(root)[argumentOutputI] = ARGUMENT_TYPE_EXPRESSION;
  return &getExpressionArguments(root)[argumentOutputI].asExpression;
};

static size_t* makeExpressionArgument(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint64_t typeOutputI) {
#ifdef __cplusplus
  auto ARGUMENT_TYPE_SYMBOL = PortableBOSSArgumentType::ARGUMENT_TYPE_EXPRESSION;
#endif
  getArgumentTypes(root)[typeOutputI] = ARGUMENT_TYPE_EXPRESSION;
  return &getExpressionArguments(root)[argumentOutputI].asExpression;
};

static void setRLEArgumentFlagOrPropagateTypes(struct PortableBOSSRootExpression* root,
                                               uint64_t argumentOutputI, uint32_t size) {
  if(size < PortableBOSSArgumentType_RLE_MINIMUM_SIZE) {
    // RLE is not supported, fallback to set the argument types
    enum PortableBOSSArgumentType const type = getArgumentTypes(root)[argumentOutputI];
    for(uint64_t i = argumentOutputI + 1; i < argumentOutputI + size; ++i) {
      getArgumentTypes(root)[i] = type;
    }
    return;
  }
  PortableBOSSArgumentType* argTypes = getArgumentTypes(root);
  (*(uint8_t*)(&argTypes[argumentOutputI])) |=
      PortableBOSSArgumentType_RLE_BIT; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
  (*(uint8_t*)(&argTypes[argumentOutputI + 4])) =
      (size >> 24) & 0xFF; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
  (*(uint8_t*)(&argTypes[argumentOutputI + 3])) =
      (size >> 16) & 0xFF; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
  (*(uint8_t*)(&argTypes[argumentOutputI + 2])) =
      (size >> 8) & 0xFF; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
  (*(uint8_t*)(&argTypes[argumentOutputI + 1])) =
      size & 0xFF; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
}

static int8_t* makeCharArgumentsRun(struct PortableBOSSRootExpression* root,
                                    uint64_t argumentOutputI, uint32_t size) {
  int8_t* value = makeCharArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static int16_t* makeShortArgumentsRun(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint32_t size) {
  int16_t* value = makeShortArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static int32_t* makeIntArgumentsRun(struct PortableBOSSRootExpression* root,
                                    uint64_t argumentOutputI, uint32_t size) {
  int32_t* value = makeIntArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static int64_t* makeLongArgumentsRun(struct PortableBOSSRootExpression* root,
                                     uint64_t argumentOutputI, uint32_t size) {
  int64_t* value = makeLongArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static float* makeFloatArgumentsRun(struct PortableBOSSRootExpression* root,
                                    uint64_t argumentOutputI, uint64_t size) {
  float* value = makeFloatArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static double* makeDoubleArgumentsRun(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint64_t size) {
  double* value = makeDoubleArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static size_t* makeStringArgumentsRun(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint64_t size) {
  size_t* value = makeStringArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static size_t* makeSymbolArgumentsRun(struct PortableBOSSRootExpression* root,
                                      uint64_t argumentOutputI, uint32_t size) {
  size_t* value = makeSymbolArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static size_t* makeExpressionArgumentsRun(struct PortableBOSSRootExpression* root,
                                          uint64_t argumentOutputI, uint64_t size) {
  size_t* value = makeExpressionArgument(root, argumentOutputI);
  setRLEArgumentFlagOrPropagateTypes(root, argumentOutputI, size);
  return value;
}

static struct PortableBOSSExpression* makeExpression(struct PortableBOSSRootExpression* root,
                                                     uint64_t expressionOutputI) {
  return &getExpressionSubexpressions(root)[expressionOutputI];
}

static size_t storeString(struct PortableBOSSRootExpression** root, char const* inputString) {
  size_t const inputStringLength = strlen(inputString);
  char const* result = strncpy(getStringBuffer(*root) + (*root)->stringArgumentsFillIndex,
                               inputString, inputStringLength + 1);
  (*root)->stringArgumentsFillIndex += inputStringLength + 1;
  return result - getStringBuffer(*root);
};

static size_t storeStringReallocation(struct PortableBOSSRootExpression** root,
                                      char const* inputString,
                                      void* (*reallocateFunction)(void*, size_t)) {
  size_t const inputStringLength = strlen(inputString);
  *root = (struct PortableBOSSRootExpression*) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
      reallocateFunction(*root, // NOLINT(hicpp-no-malloc, cppcoreguidelines-no-malloc)
                         ((char*)(getStringBuffer(*root)) -
                          ((char*)*root)) + // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
                             (*root)->stringArgumentsFillIndex +
                             inputStringLength + 1);
  char const* result = strncpy(getStringBuffer(*root) + (*root)->stringArgumentsFillIndex,
                               inputString, inputStringLength + 1);
  (*root)->stringArgumentsFillIndex += inputStringLength + 1;
  return result - getStringBuffer(*root);
};

static char const* viewString(struct PortableBOSSRootExpression* root, size_t inputStringOffset) {
  return getStringBuffer(root) + inputStringOffset;
};

struct PortableBOSSRootExpression* serializeBOSSExpression(struct BOSSExpression* expression);
struct BOSSExpression* deserializeBOSSExpression(struct PortableBOSSRootExpression* root);
struct BOSSExpression* parseURL(char const* url);

#ifdef __cplusplus
}
#endif
// NOLINTEND(hicpp-use-auto,cppcoreguidelines-pro-type-union-access)

#endif /* PORTABLEBOSSSERIALIZATION_H */
