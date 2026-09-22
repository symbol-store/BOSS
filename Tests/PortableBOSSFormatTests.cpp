// Standalone tests for the Wisent argument-type region format (v0 flat / v1 per-node RLE).
//
// Deliberately depends on nothing but PortableBOSSSerialization.h and the standard
// library, so it can be built and run in seconds without the CMake build:
//
//   clang++-17 -std=c++17 -O1 -I Source Tests/PortableBOSSFormatTests.cpp -o /tmp/fmttest && /tmp/fmttest
//
// It can additionally be pointed at real .bin files, in which case it asserts that
// expand(encode(types)) is byte-identical to the file's own type region:
//
//   /tmp/fmttest <file.bin> [more.bin ...]

#include <cmath> // for float_t/double_t, which PortableBOSSSerialization.h uses but does not include
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "PortableBOSSSerialization.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                                \
  do {                                                                                             \
    ++checks;                                                                                      \
    if(!(cond)) {                                                                                  \
      ++failures;                                                                                  \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                         \
    }                                                                                              \
  } while(0)

#define CHECK_EQ(a, b)                                                                             \
  do {                                                                                             \
    ++checks;                                                                                      \
    auto const va_ = (uint64_t)(a);                                                                \
    auto const vb_ = (uint64_t)(b);                                                                \
    if(va_ != vb_) {                                                                               \
      ++failures;                                                                                  \
      std::fprintf(stderr, "FAIL %s:%d: %s (%llu) != %s (%llu)\n", __FILE__, __LINE__, #a,         \
                   (unsigned long long)va_, #b, (unsigned long long)vb_);                          \
    }                                                                                              \
  } while(0)

//////////////////////////////////////////////////////////////////////////////
// Helpers: build a root in the v0 (flat type array) layout.
//////////////////////////////////////////////////////////////////////////////

struct Node {
  uint64_t startType, endType;
  std::vector<uint8_t> types; // types[startType..endType)
};

// Builds a v0 root: header, argument values (zeroed), flat type array, expression table.
static std::vector<char> buildV0(std::vector<uint8_t> const& flatTypes,
                                 std::vector<Node> const& nodes, uint64_t stringBytes = 8) {
  uint64_t const argumentCount = flatTypes.size();
  uint64_t const argumentBytesCount = argumentCount * sizeof(PortableBOSSArgumentValue);
  uint64_t const expressionCount = nodes.size();
  uint64_t const typeBytes = alignTo8Bytes(argumentCount * sizeof(PortableBOSSArgumentType));
  uint64_t const total = sizeof(PortableBOSSRootExpression) + alignTo8Bytes(argumentBytesCount) +
                         typeBytes + expressionCount * sizeof(PortableBOSSExpression) + stringBytes;
  std::vector<char> buf(total, 0);
  auto* root = reinterpret_cast<PortableBOSSRootExpression*>(buf.data());
  *((uint64_t*)&root->argumentCount) = argumentCount;
  *((uint64_t*)&root->argumentBytesCount) = argumentBytesCount;
  *((uint64_t*)&root->expressionCount) = expressionCount;
  *((uint64_t*)&root->argumentDictionaryBytesCount) = 0;
  *((uint64_t*)&root->originalAddress) = 0; // v0
  root->stringArgumentsFillIndex = stringBytes;
  std::memcpy(getArgumentTypes(root), flatTypes.data(), flatTypes.size());
  auto* exprs = getExpressionSubexpressions(root);
  for(uint64_t i = 0; i < expressionCount; ++i) {
    exprs[i].symbolNameOffset = 0;
    exprs[i].startChildOffset = nodes[i].startType;
    exprs[i].endChildOffset = nodes[i].endType;
    exprs[i].startChildTypeOffset = nodes[i].startType;
    exprs[i].endChildTypeOffset = nodes[i].endType;
  }
  return buf;
}

// Mirrors setRLEArgumentFlagOrPropagateTypes' on-disk artefact for a run of `n` of type `t`.
static std::vector<uint8_t> legacyRleArtefact(uint8_t t, uint32_t n) {
  std::vector<uint8_t> v(n, t);
  if(n >= PortableBOSSArgumentType_RLE_MINIMUM_SIZE) {
    v[0] = (uint8_t)(t | PortableBOSSArgumentType_RLE_BIT);
    v[1] = (uint8_t)(n & 0xFF);
    v[2] = (uint8_t)((n >> 8) & 0xFF);
    v[3] = (uint8_t)((n >> 16) & 0xFF);
    v[4] = (uint8_t)((n >> 24) & 0xFF);
  }
  return v;
}

//////////////////////////////////////////////////////////////////////////////

static void testVersionDispatch() {
  std::vector<uint8_t> types(40, ARGUMENT_TYPE_LONG);
  std::vector<Node> nodes{{0, 40, {}}};
  auto buf = buildV0(types, nodes);
  auto* root = reinterpret_cast<PortableBOSSRootExpression*>(buf.data());

  // A zeroed metadata slot is v0 -- this is what every existing .bin on disk has.
  CHECK_EQ(portableBOSSFormatVersion(root), 0);
  CHECK_EQ(getArgumentTypesBytesCount(root), alignTo8Bytes(40));

  // A stale heap pointer left in the slot by an in-memory allocation must ALSO read as v0,
  // because user-space pointers have zero in the top 16 bits. This is what stops an
  // unpatched writer's `originalAddress = root` from being mistaken for a new-format file.
  *((void**)&root->originalAddress) = (void*)buf.data();
  CHECK_EQ(portableBOSSFormatVersion(root), 0);
  CHECK_EQ(getArgumentTypesBytesCount(root), alignTo8Bytes(40));

  // An explicitly stamped v1 header reports v1 and the stored physical size.
  *((uint64_t*)&root->originalAddress) = portableBOSSMakeFormatMetadata(1, 123);
  CHECK_EQ(portableBOSSFormatVersion(root), 1);
  CHECK_EQ(getArgumentTypesBytesCount(root), alignTo8Bytes(123));

  // Garbage in the top bits that is not our magic degrades to v0 rather than misreading.
  *((uint64_t*)&root->originalAddress) = 0x1234ull << 48;
  CHECK_EQ(portableBOSSFormatVersion(root), 0);
}

static void testRegionOffsetsV0() {
  std::vector<uint8_t> types(37, ARGUMENT_TYPE_INT);
  std::vector<Node> nodes{{0, 37, {}}};
  auto buf = buildV0(types, nodes);
  auto* root = reinterpret_cast<PortableBOSSRootExpression*>(buf.data());
  char const* base = root->arguments;
  CHECK_EQ((char const*)getArgumentTypes(root) - base, alignTo8Bytes(root->argumentBytesCount));
  CHECK_EQ((char const*)getExpressionSubexpressions(root) - base,
           alignTo8Bytes(root->argumentBytesCount) + alignTo8Bytes(37));
  CHECK_EQ((char const*)getStringBuffer(root) - base,
           alignTo8Bytes(root->argumentBytesCount) + alignTo8Bytes(37) +
               1 * sizeof(PortableBOSSExpression));
}

// The invariant that must not break: start/endChildTypeOffset stay LOGICAL indices, so
// numChildren == end - start regardless of how the region is physically stored.
static void testLogicalIndexInvariant() {
  std::vector<uint8_t> flat;
  std::vector<Node> nodes;
  flat.push_back(ARGUMENT_TYPE_EXPRESSION); // residual slot 0, owned by no node
  uint64_t start = flat.size();
  auto run = legacyRleArtefact(ARGUMENT_TYPE_BOOL, 5000);
  flat.insert(flat.end(), run.begin(), run.end());
  nodes.push_back({start, (uint64_t)flat.size(), {}});
  start = flat.size();
  for(uint8_t t : {ARGUMENT_TYPE_INT, ARGUMENT_TYPE_EXPRESSION, ARGUMENT_TYPE_SHORT})
    flat.push_back(t);
  nodes.push_back({start, (uint64_t)flat.size(), {}});

  auto v0 = buildV0(flat, nodes);
  auto* r0 = reinterpret_cast<PortableBOSSRootExpression*>(v0.data());

  std::vector<char> v1;
  CHECK(portableBOSSEncodeToV1(r0, v1));
  auto* r1 = reinterpret_cast<PortableBOSSRootExpression*>(v1.data());

  CHECK_EQ(portableBOSSFormatVersion(r1), 1);
  CHECK_EQ(r1->argumentCount, r0->argumentCount);
  CHECK_EQ(r1->expressionCount, r0->expressionCount);
  auto* e0 = getExpressionSubexpressions(r0);
  auto* e1 = getExpressionSubexpressions(r1);
  for(uint64_t i = 0; i < r0->expressionCount; ++i) {
    CHECK_EQ(e1[i].startChildTypeOffset, e0[i].startChildTypeOffset);
    CHECK_EQ(e1[i].endChildTypeOffset, e0[i].endChildTypeOffset);
    CHECK_EQ(e1[i].endChildTypeOffset - e1[i].startChildTypeOffset,
             e0[i].endChildTypeOffset - e0[i].startChildTypeOffset);
  }
  // and the v1 type region really is smaller
  CHECK(getArgumentTypesBytesCount(r1) < getArgumentTypesBytesCount(r0));
}

static void testRoundTripSynthetic() {
  struct Case {
    char const* name;
    std::vector<uint8_t> flat;
    std::vector<Node> nodes;
  };
  std::vector<Case> cases;

  { // constant node
    std::vector<uint8_t> f(1, ARGUMENT_TYPE_EXPRESSION);
    std::vector<uint8_t> c(100, ARGUMENT_TYPE_DOUBLE);
    f.insert(f.end(), c.begin(), c.end());
    cases.push_back({"constant", f, {{1, (uint64_t)f.size(), {}}}});
  }
  { // legacy RLE artefact node
    std::vector<uint8_t> f(1, ARGUMENT_TYPE_EXPRESSION);
    auto c = legacyRleArtefact(ARGUMENT_TYPE_LONG, 1000000);
    f.insert(f.end(), c.begin(), c.end());
    cases.push_back({"legacy-rle", f, {{1, (uint64_t)f.size(), {}}}});
  }
  { // genuinely heterogeneous node (like FOREncodedList / ScaledFixedDecimal)
    std::vector<uint8_t> f{ARGUMENT_TYPE_EXPRESSION, ARGUMENT_TYPE_EXPRESSION,
                           ARGUMENT_TYPE_EXPRESSION, ARGUMENT_TYPE_EXPRESSION,
                           ARGUMENT_TYPE_EXPRESSION, ARGUMENT_TYPE_INT,
                           ARGUMENT_TYPE_LONG};
    cases.push_back({"heterogeneous", f, {{1, (uint64_t)f.size(), {}}}});
  }
  { // a run just under the legacy RLE minimum -- must stay a plain constant node
    std::vector<uint8_t> f(1, ARGUMENT_TYPE_EXPRESSION);
    std::vector<uint8_t> c(PortableBOSSArgumentType_RLE_MINIMUM_SIZE - 1, ARGUMENT_TYPE_CHAR);
    f.insert(f.end(), c.begin(), c.end());
    cases.push_back({"short-run", f, {{1, (uint64_t)f.size(), {}}}});
  }
  { // mixture of all of the above
    std::vector<uint8_t> f(1, ARGUMENT_TYPE_EXPRESSION);
    std::vector<Node> n;
    auto add = [&](std::vector<uint8_t> const& seg) {
      uint64_t s = f.size();
      f.insert(f.end(), seg.begin(), seg.end());
      n.push_back({s, (uint64_t)f.size(), {}});
    };
    add(std::vector<uint8_t>(64, ARGUMENT_TYPE_STRING));
    add(legacyRleArtefact(ARGUMENT_TYPE_BOOL, 250000));
    add({ARGUMENT_TYPE_INT, ARGUMENT_TYPE_SYMBOL});
    add(legacyRleArtefact(ARGUMENT_TYPE_INT, 77));
    add(std::vector<uint8_t>(1, ARGUMENT_TYPE_FLOAT));
    cases.push_back({"mixture", f, n});
  }

  for(auto const& c : cases) {
    auto v0 = buildV0(c.flat, c.nodes);
    auto* r0 = reinterpret_cast<PortableBOSSRootExpression*>(v0.data());
    std::vector<char> v1;
    if(!portableBOSSEncodeToV1(r0, v1)) {
      ++checks;
      ++failures;
      std::fprintf(stderr, "FAIL encode refused for case '%s'\n", c.name);
      continue;
    }
    auto* r1 = reinterpret_cast<PortableBOSSRootExpression*>(v1.data());
    CHECK_EQ(portableBOSSFormatVersion(r1), 1);

    std::vector<char> back;
    ++checks;
    if(!portableBOSSExpandToV0(r1, back)) {
      ++failures;
      std::fprintf(stderr, "FAIL expand refused for case '%s'\n", c.name);
      continue;
    }
    auto* r2 = reinterpret_cast<PortableBOSSRootExpression*>(back.data());
    CHECK_EQ(portableBOSSFormatVersion(r2), 0);
    CHECK_EQ(r2->argumentCount, r0->argumentCount);
    CHECK_EQ(getArgumentTypesBytesCount(r2), getArgumentTypesBytesCount(r0));
    ++checks;
    if(std::memcmp(getArgumentTypes(r2), getArgumentTypes(r0), r0->argumentCount) != 0) {
      ++failures;
      std::fprintf(stderr, "FAIL type bytes differ after round trip for case '%s'\n", c.name);
    }
    // every other region must survive too
    ++checks;
    if(std::memcmp(getExpressionSubexpressions(r2), getExpressionSubexpressions(r0),
                   r0->expressionCount * sizeof(PortableBOSSExpression)) != 0) {
      ++failures;
      std::fprintf(stderr, "FAIL expression table differs for case '%s'\n", c.name);
    }
    std::printf("  %-14s v0 type region %10llu B -> v1 %6llu B\n", c.name,
                (unsigned long long)getArgumentTypesBytesCount(r0),
                (unsigned long long)getArgumentTypesBytesCount(r1));
  }
}

// A node whose children cannot be described by any descriptor must still round trip
// (it falls into the EXPLICIT overflow), and an uncovered logical prefix must survive.
static void testResidualAndOverflow() {
  std::vector<uint8_t> f{ARGUMENT_TYPE_SYMBOL, ARGUMENT_TYPE_BOOL, ARGUMENT_TYPE_DOUBLE};
  std::vector<Node> n{{2, 3, {}}};
  auto v0 = buildV0(f, n);
  auto* r0 = reinterpret_cast<PortableBOSSRootExpression*>(v0.data());
  std::vector<char> v1;
  CHECK(portableBOSSEncodeToV1(r0, v1));
  auto* r1 = reinterpret_cast<PortableBOSSRootExpression*>(v1.data());
  std::vector<char> back;
  CHECK(portableBOSSExpandToV0(r1, back));
  auto* r2 = reinterpret_cast<PortableBOSSRootExpression*>(back.data());
  CHECK_EQ(std::memcmp(getArgumentTypes(r2), getArgumentTypes(r0), 3), 0);
}

//////////////////////////////////////////////////////////////////////////////
// Optional: run the round trip against real .bin files.
//////////////////////////////////////////////////////////////////////////////

static void testRealFile(char const* path) {
  std::ifstream in(path, std::ios::binary);
  if(!in) {
    std::fprintf(stderr, "  (skipping unreadable %s)\n", path);
    return;
  }
  in.seekg(0, std::ios::end);
  auto size = (size_t)in.tellg();
  in.seekg(0, std::ios::beg);
  std::vector<char> buf(size);
  in.read(buf.data(), (std::streamsize)size);
  auto* r0 = reinterpret_cast<PortableBOSSRootExpression*>(buf.data());
  CHECK_EQ(portableBOSSFormatVersion(r0), 0); // every existing file is v0

  std::vector<char> v1;
  ++checks;
  if(!portableBOSSEncodeToV1(r0, v1)) {
    ++failures;
    std::fprintf(stderr, "FAIL encode refused for %s\n", path);
    return;
  }
  auto* r1 = reinterpret_cast<PortableBOSSRootExpression*>(v1.data());
  std::vector<char> back;
  CHECK(portableBOSSExpandToV0(r1, back));
  auto* r2 = reinterpret_cast<PortableBOSSRootExpression*>(back.data());
  ++checks;
  if(std::memcmp(getArgumentTypes(r2), getArgumentTypes(r0), r0->argumentCount) != 0) {
    ++failures;
    std::fprintf(stderr, "FAIL type region not byte-identical after round trip: %s\n", path);
  }
  ++checks;
  if(std::memcmp(getExpressionSubexpressions(r2), getExpressionSubexpressions(r0),
                 r0->expressionCount * sizeof(PortableBOSSExpression)) != 0) {
    ++failures;
    std::fprintf(stderr, "FAIL expression table not identical: %s\n", path);
  }
  auto const oldT = getArgumentTypesBytesCount(r0);
  auto const newT = getArgumentTypesBytesCount(r1);
  std::printf("  %-52s type region %12llu B -> %6llu B  (file %llu B, %.1f%% of it was types)\n",
              path, (unsigned long long)oldT, (unsigned long long)newT,
              (unsigned long long)size, 100.0 * (double)oldT / (double)size);
}

int main(int argc, char** argv) {
  std::printf("PortableBOSS argument-type format tests\n");
  testVersionDispatch();
  testRegionOffsetsV0();
  testLogicalIndexInvariant();
  testRoundTripSynthetic();
  testResidualAndOverflow();
  for(int i = 1; i < argc; ++i)
    testRealFile(argv[i]);
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
