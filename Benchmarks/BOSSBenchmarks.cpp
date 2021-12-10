#include "../Source/BootstrapEngine.hpp"
#include "DuckDB.hpp"
#include "ITTNotifySupport.hpp"
#include "MonetDB.hpp"

#include <arrow/builder.h>
#include <benchmark/benchmark.h>

#include <filesystem>
#include <iostream>

using namespace std;

static auto const vtune = VTuneAPIInterface{"BOSS"};

#ifdef NDEBUG
bool constexpr VERBOSE_QUERY_OUTPUT = false;
#else
bool constexpr VERBOSE_QUERY_OUTPUT = true;
#endif

void generateStrings(std::vector<string>& lhs, std::vector<string>& rhs, size_t size) {
  auto alphabet = string{"abcdefghijklmnopqrstuvwxyz"};

  lhs.reserve(size);
  rhs.reserve(size);

  auto lhsOffset = 0;
  auto rhsOffset = 0;
  for(int i = 0; i < size; ++i) {
    static auto const baseWordSize = 6;
    static auto const maxAdditionalSize = 5;
    static auto const leftToRightOffset = 4;
    auto lhsWordLength = baseWordSize + (i % maxAdditionalSize);
    auto rhsWordLength = baseWordSize - ((i + leftToRightOffset) % maxAdditionalSize);
    if(lhsOffset + lhsWordLength >= alphabet.length()) {
      lhsOffset = 0;
    }
    if(rhsOffset + rhsWordLength >= alphabet.length()) {
      rhsOffset = 0;
    }
    lhs.emplace_back(alphabet, lhsOffset, lhsWordLength);
    rhs.emplace_back(alphabet, rhsOffset, rhsWordLength);
    lhsOffset += lhsWordLength;
    rhsOffset += rhsWordLength;
  }
}

static void Addition_Vectors(benchmark::State& state) {
  auto size = state.range(0);
  auto lhs = std::vector<int>(size);
  auto rhs = std::vector<int>(size);

  vtune.startSampling("Addition - Vectors");
  for(auto _ : state) { // NOLINT
    auto output = std::vector<int>(size);
    for(int i = 0; i < size; ++i) {
      output[i] = lhs[i] + rhs[i];
    }
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void Comparison_Vectors(benchmark::State& state) {
  auto size = state.range(0);
  auto lhs = std::vector<int>(size);
  auto rhs = std::vector<int>(size);

  vtune.startSampling("Comparison - Vectors");
  for(auto _ : state) { // NOLINT
    auto output = std::vector<bool>(size);
    for(int i = 0; i < size; ++i) {
      output[i] = lhs[i] < rhs[i];
    }
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void Logic_Vectors(benchmark::State& state) {
  auto size = state.range(0);
  auto lhs = std::vector<bool>(size);
  auto rhs = std::vector<bool>(size);

  vtune.startSampling("Logic - Vectors");
  for(auto _ : state) { // NOLINT
    auto output = std::vector<bool>(size);
    for(int i = 0; i < size; ++i) {
      output[i] = lhs[i] && rhs[i];
    }
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

int functionPlus(int lhs, int rhs) { return lhs + rhs; }
bool functionGreater(int lhs, int rhs) { return lhs > rhs; }

static void Compound_Vectors(benchmark::State& state) {
  auto size = state.range(0);
  auto ints1 = std::vector<int>(size);
  auto ints2 = std::vector<int>(size);
  auto ints3 = std::vector<int>(size);

  std::function<int(int, int)> callPlus = functionPlus;
  std::function<bool(int, int)> callGreater = functionGreater;

  vtune.startSampling("Compound - Vectors");
  for(auto _ : state) { // NOLINT
    auto output = std::vector<bool>(size);
    for(int i = 0; i < size; ++i) {
      output[i] = callGreater(callPlus(ints1[i], ints2[i]), ints3[i]) &&
                  callGreater(callPlus(ints3[i], ints2[i]), ints1[i]);
    }
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void StringContains_Vectors(benchmark::State& state) {
  auto size = state.range(0);
  auto lhs = std::vector<string>();
  auto rhs = std::vector<string>();
  generateStrings(lhs, rhs, size);

  vtune.startSampling("StringContains - Vectors");
  for(auto _ : state) { // NOLINT
    auto output = std::vector<bool>(size);
    for(int i = 0; i < size; ++i) {
      output[i] = lhs[i].find(rhs[i]) != string::npos;
    }
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void StringJoin_Vectors(benchmark::State& state) {
  auto size = state.range(0);
  auto lhs = std::vector<string>();
  auto rhs = std::vector<string>();
  generateStrings(lhs, rhs, size);

  vtune.startSampling("StringJoin - Vectors");
  for(auto _ : state) { // NOLINT
    auto output = std::vector<string>(size);
    for(int i = 0; i < size; ++i) {
      output[i] = lhs[i] + rhs[i];
    }
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void SelectString_Vectors(benchmark::State& state) {
  std::string toFind = "found";

  auto size = state.range(0);
  auto selectivityFraction = state.range(1); // 1 true for X values

  auto column1 = std::vector<string>();
  auto column2 = std::vector<string>();
  generateStrings(column1, column2, size);

  for(auto i = 0UL; i < size; i += selectivityFraction) {
    column1[i] = toFind;
  }

  vtune.startSampling("SelectString - Vectors");
  for(auto _ : state) { // NOLINT
    std::vector<std::pair<std::string, std::string>> output;
    output.reserve(size / selectivityFraction);
    for(int i = 0; i < size; ++i) {
      if(column1[i] == toFind) {
        output.emplace_back(column1[i], column2[i]);
      }
    }
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void SelectInt_Vectors(benchmark::State& state) {
  static int const toFind = 100;

  auto size = state.range(0);
  auto selectivityFraction = state.range(1); // 1 true for X values

  auto column1 = std::vector<int>(size, 0);
  auto column2 = std::vector<int>(size, 0);

  for(auto i = 0UL; i < size; i += selectivityFraction) {
    column1[i] = toFind;
  }

  vtune.startSampling("SelectInt - Vectors");
  for(auto _ : state) { // NOLINT
    std::vector<std::pair<int, int>> output;
    output.reserve(size / selectivityFraction);
    for(int i = 0; i < size; ++i) {
      if(column1[i] >= toFind) {
        output.emplace_back(column1[i], column2[i]);
      }
    }
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void SelectBool_Vectors(benchmark::State& state) {
  auto size = state.range(0);
  auto selectivityFraction = state.range(1); // 1 true for X values

  auto column1 = std::vector<bool>(size, false);
  auto column2 = std::vector<bool>(size, false);

  for(auto i = 0UL; i < size; i += selectivityFraction) {
    column1[i] = true;
  }

  vtune.startSampling("SelectBool - Vectors");
  for(auto _ : state) { // NOLINT
    std::vector<std::pair<bool, bool>> output;
    output.reserve(size / selectivityFraction);
    for(int i = 0; i < size; ++i) {
      if(column1[i]) {
        output.emplace_back(column1[i], column2[i]);
      }
    }
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void Addition_BOSS(benchmark::State& state, std::string const& engineLibrary) {
  auto engine = boss::BootstrapEngine();
  auto eval = [&](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  auto size = state.range(0);
  auto lhs = std::vector<int>(size);
  auto rhs = std::vector<int>(size);

  auto lhsArrayPtr =
      std::shared_ptr<arrow::Array>(new arrow::Int32Array(size, arrow::Buffer::Wrap(lhs)));
  auto rhsArrayPtr =
      std::shared_ptr<arrow::Array>(new arrow::Int32Array(size, arrow::Buffer::Wrap(rhs)));

  auto lhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(lhsArrayPtr);
  auto rhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(rhsArrayPtr);

  auto expr = "Extract"_("Plus"_(lhsArrayExpr, rhsArrayExpr), 1);

  vtune.startSampling("Addition - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval(expr);
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void Comparison_BOSS(benchmark::State& state, std::string const& engineLibrary) {
  auto engine = boss::BootstrapEngine();
  auto eval = [&](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  auto size = state.range(0);
  auto lhs = std::vector<int>(size);
  auto rhs = std::vector<int>(size);

  auto lhsArrayPtr =
      std::shared_ptr<arrow::Array>(new arrow::Int32Array(size, arrow::Buffer::Wrap(lhs)));
  auto rhsArrayPtr =
      std::shared_ptr<arrow::Array>(new arrow::Int32Array(size, arrow::Buffer::Wrap(rhs)));

  auto lhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(lhsArrayPtr);
  auto rhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(rhsArrayPtr);

  auto expr = "Extract"_("Greater"_(lhsArrayExpr, rhsArrayExpr), 1);

  vtune.startSampling("Comparison - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval(expr);
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void Logic_BOSS(benchmark::State& state, std::string const& engineLibrary) {
  auto engine = boss::BootstrapEngine();
  auto eval = [&](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  auto size = state.range(0);
  auto lhs = std::vector<bool>(size);
  auto rhs = std::vector<bool>(size);

  auto lhsBuilder = arrow::BooleanBuilder();
  auto rhsBuilder = arrow::BooleanBuilder();

  auto status = lhsBuilder.AppendValues(lhs);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = rhsBuilder.AppendValues(rhs);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  std::shared_ptr<arrow::Array> lhsArrayPtr;
  std::shared_ptr<arrow::Array> rhsArrayPtr;

  status = lhsBuilder.Finish(&lhsArrayPtr);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = rhsBuilder.Finish(&rhsArrayPtr);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  auto lhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(lhsArrayPtr);
  auto rhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(rhsArrayPtr);

  auto expr = "Extract"_("And"_(lhsArrayExpr, rhsArrayExpr), 1);

  vtune.startSampling("Logic - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval(expr);
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void Compound_BOSS(benchmark::State& state, std::string const& engineLibrary) {
  auto engine = boss::BootstrapEngine();
  auto eval = [&](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  auto size = state.range(0);
  auto ints1 = std::vector<int>(size);
  auto ints2 = std::vector<int>(size);
  auto ints3 = std::vector<int>(size);

  auto arrayPtr1 =
      std::shared_ptr<arrow::Array>(new arrow::Int32Array(size, arrow::Buffer::Wrap(ints1)));
  auto arrayPtr2 =
      std::shared_ptr<arrow::Array>(new arrow::Int32Array(size, arrow::Buffer::Wrap(ints2)));
  auto arrayPtr3 =
      std::shared_ptr<arrow::Array>(new arrow::Int32Array(size, arrow::Buffer::Wrap(ints3)));

  auto arrayExpr1 = boss::utilities::nasty::arrowArrayToExpression(arrayPtr1);
  auto arrayExpr2 = boss::utilities::nasty::arrowArrayToExpression(arrayPtr2);
  auto arrayExpr3 = boss::utilities::nasty::arrowArrayToExpression(arrayPtr3);

  auto expr = "Extract"_("And"_("Greater"_("Plus"_(arrayExpr1, arrayExpr2), arrayExpr3),
                                "Greater"_("Plus"_(arrayExpr3, arrayExpr2), arrayExpr1)),
                         1);

  vtune.startSampling("Compound - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval(expr);
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void StringContains_BOSS(benchmark::State& state, std::string const& engineLibrary) {
  auto engine = boss::BootstrapEngine();
  auto eval = [&](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  auto size = state.range(0);
  auto lhs = std::vector<string>();
  auto rhs = std::vector<string>();
  generateStrings(lhs, rhs, size);

  auto lhsBuilder = arrow::StringBuilder();
  auto rhsBuilder = arrow::StringBuilder();

  auto status = lhsBuilder.AppendValues(lhs);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = rhsBuilder.AppendValues(rhs);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  auto lhsArrayPtr = std::shared_ptr<arrow::Array>();
  auto rhsArrayPtr = std::shared_ptr<arrow::Array>();

  status = lhsBuilder.Finish(&lhsArrayPtr);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = rhsBuilder.Finish(&rhsArrayPtr);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  auto lhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(lhsArrayPtr);
  auto rhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(rhsArrayPtr);

  auto expr = "Extract"_("StringContainsQ"_(lhsArrayExpr, rhsArrayExpr), 1);

  vtune.startSampling("StringContains - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval(expr);
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void StringJoin_BOSS(benchmark::State& state, std::string const& engineLibrary) {
  auto engine = boss::BootstrapEngine();
  auto eval = [&](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  auto size = state.range(0);
  auto lhs = std::vector<string>();
  auto rhs = std::vector<string>();
  generateStrings(lhs, rhs, size);

  auto lhsBuilder = arrow::StringBuilder();
  auto rhsBuilder = arrow::StringBuilder();

  auto status = lhsBuilder.AppendValues(lhs);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = rhsBuilder.AppendValues(rhs);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  auto lhsArrayPtr = std::shared_ptr<arrow::Array>();
  auto rhsArrayPtr = std::shared_ptr<arrow::Array>();

  status = lhsBuilder.Finish(&lhsArrayPtr);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = rhsBuilder.Finish(&rhsArrayPtr);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  auto lhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(lhsArrayPtr);
  auto rhsArrayExpr = boss::utilities::nasty::arrowArrayToExpression(rhsArrayPtr);

  auto expr = "Extract"_("StringJoin"_(lhsArrayExpr, rhsArrayExpr), 1);

  vtune.startSampling("StringJoin - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval(expr);
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void SelectString_BOSS(benchmark::State& state, std::string const& engineLibrary) {
  auto engine = boss::BootstrapEngine();
  auto eval = [&](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  std::string toFind = "found";

  auto size = state.range(0);
  auto selectivityFraction = state.range(1); // 1 true for X values

  auto column1 = std::vector<string>();
  auto column2 = std::vector<string>();
  generateStrings(column1, column2, size);

  for(auto i = 0UL; i < size; i += selectivityFraction) {
    column1[i] = toFind;
  }

  auto columnBuilder1 = arrow::StringBuilder();
  auto columnBuilder2 = arrow::StringBuilder();

  auto status = columnBuilder1.AppendValues(column1);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = columnBuilder2.AppendValues(column2);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  auto arrayPtr1 = std::shared_ptr<arrow::Array>();
  auto arrayPtr2 = std::shared_ptr<arrow::Array>();

  status = columnBuilder1.Finish(&arrayPtr1);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = columnBuilder2.Finish(&arrayPtr2);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  auto columnExpr1 = boss::utilities::nasty::arrowArrayToExpression(arrayPtr1);
  auto columnExpr2 = boss::utilities::nasty::arrowArrayToExpression(arrayPtr2);

  eval("CreateTable"_("Customer"_, "FirstName"_, "LastName"_));
  eval("AttachColumns"_("Customer"_, columnExpr1, columnExpr2));

  auto expr = "Extract"_(
      "Column"_("Select"_("Customer"_, "Function"_("tuple"_, "StringContainsQ"_(
                                                                 toFind, "Column"_("tuple"_, 1)))),
                1),
      1);

  vtune.startSampling("SelectString - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval(expr);
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void SelectInt_BOSS(benchmark::State& state, std::string const& engineLibrary) {
  auto engine = boss::BootstrapEngine();
  auto eval = [&](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  static int const toFind = 100;

  auto size = state.range(0);
  auto selectivityFraction = state.range(1); // 1 true for X values

  auto column1 = std::vector<int>(size, 0);
  auto column2 = std::vector<int>(size, 0);

  for(auto i = 0UL; i < size; i += selectivityFraction) {
    column1[i] = toFind;
  }

  eval("CreateTable"_("Quantities"_, "A"_, "B"_));

  auto arrayPtr1 =
      std::shared_ptr<arrow::Array>(new arrow::Int32Array(size, arrow::Buffer::Wrap(column1)));
  auto arrayPtr2 =
      std::shared_ptr<arrow::Array>(new arrow::Int32Array(size, arrow::Buffer::Wrap(column2)));

  auto columnExpr1 = boss::utilities::nasty::arrowArrayToExpression(arrayPtr1);
  auto columnExpr2 = boss::utilities::nasty::arrowArrayToExpression(arrayPtr2);

  eval("AttachColumns"_("Quantities"_, columnExpr1, columnExpr2));

  auto expr = "Extract"_(
      "Column"_("Select"_("Quantities"_,
                          "Function"_("tuple"_, "Greater"_("Column"_("tuple"_, 1), toFind - 1))),
                1),
      1);

  vtune.startSampling("SelectInt - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval(expr);
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void SelectBool_BOSS(benchmark::State& state, std::string const& engineLibrary) {
  auto engine = boss::BootstrapEngine();
  auto eval = [&](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  auto size = state.range(0);
  auto selectivityFraction = state.range(1); // 1 true for X values

  auto column1 = std::vector<bool>(size, false);
  auto column2 = std::vector<bool>(size, false);

  for(auto i = 0UL; i < size; i += selectivityFraction) {
    column1[i] = true;
  }

  eval("CreateTable"_("Booleans"_, "A"_, "B"_));

  auto columnBuilder1 = arrow::BooleanBuilder();
  auto columnBuilder2 = arrow::BooleanBuilder();

  auto status = columnBuilder1.AppendValues(column1.begin(), column1.end());
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = columnBuilder2.AppendValues(column2.begin(), column2.end());
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  auto arrayPtr1 = std::shared_ptr<arrow::Array>();
  auto arrayPtr2 = std::shared_ptr<arrow::Array>();

  status = columnBuilder1.Finish(&arrayPtr1);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  status = columnBuilder2.Finish(&arrayPtr2);
  if(!status.ok()) {
    throw std::runtime_error(status.ToString());
  }

  auto columnExpr1 = boss::utilities::nasty::arrowArrayToExpression(arrayPtr1);
  auto columnExpr2 = boss::utilities::nasty::arrowArrayToExpression(arrayPtr2);

  eval("AttachColumns"_("Booleans"_, columnExpr1, columnExpr2));

  auto expr = "Extract"_(
      "Column"_("Select"_("Booleans"_, "Function"_("tuple"_, "Column"_("tuple"_, 1))), 1), 1);

  vtune.startSampling("SelectBool - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval(expr);
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

template <typename Vector1, typename Vector2>
Vector1 mergeVectors(Vector1 const& vec1, Vector2 const& vec2) {
  auto output = vec1;
  output.insert(output.end(), vec2.begin(), vec2.end());
  return output;
}

template <typename Vector1, typename... Vectors>
Vector1 mergeVectors(Vector1 const& vec1, Vectors const&... vecs) {
  return mergeVectors(vec1, mergeVectors(vecs...));
}

static auto& persistentEnginePtr() {
  static std::unique_ptr<boss::BootstrapEngine> engine;
  return engine;
}

static auto& initEngine_TPCH(std::string const& engineLibrary, size_t size, bool lineitemOnly) {
  static auto lastEngineLibrary = engineLibrary;
  static auto lastSize = size;
  static auto lastLineitemOnly = lineitemOnly;
  if(engineLibrary != lastEngineLibrary || size != lastSize || lastLineitemOnly != lineitemOnly ||
     !persistentEnginePtr()) {
    lastSize = size;
    lastLineitemOnly = lineitemOnly;
    lastEngineLibrary = engineLibrary;
    persistentEnginePtr().reset();
    persistentEnginePtr() = std::make_unique<boss::BootstrapEngine>();

    auto& engine = *persistentEnginePtr();
    auto eval = [&engine, &engineLibrary](boss::ComplexExpression const& expression) mutable {
      return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
    };

    if(!lineitemOnly) {
      eval("CreateTable"_("REGION"_, "R_REGIONKEY"_, "R_NAME"_, "R_COMMENT"_));
      eval("CreateTable"_("NATION"_, "N_NATIONKEY"_, "N_NAME"_, "N_REGIONKEY"_, "N_COMMENT"_));
      eval("CreateTable"_("PART"_, "P_PARTKEY"_, "P_NAME"_, "P_MFGR"_, "P_BRAND"_, "P_TYPE"_,
                          "P_SIZE"_, "P_CONTAINER"_, "P_RETAILPRICE"_, "P_COMMENT"_));
      eval("CreateTable"_("SUPPLIER"_, "S_SUPPKEY"_, "S_NAME"_, "S_ADDRESS"_, "S_NATIONKEY"_,
                          "S_PHONE"_, "S_ACCTBAL"_, "S_COMMENT"_));
      eval("CreateTable"_("PARTSUPP"_, "PS_PARTKEY"_, "PS_SUPPKEY"_, "PS_AVAILQTY"_,
                          "PS_SUPPLYCOST"_, "PS_COMMENT"_));
      eval("CreateTable"_("CUSTOMER"_, "C_CUSTKEY"_, "C_NAME"_, "C_ADDRESS"_, "C_NATIONKEY"_,
                          "C_PHONE"_, "C_ACCTBAL"_, "C_MKTSEGMENT"_, "C_COMMENT"_));
      eval("CreateTable"_("ORDERS"_, "O_ORDERKEY"_, "O_CUSTKEY"_, "O_ORDERSTATUS"_, "O_TOTALPRICE"_,
                          "O_ORDERDATE"_, "O_ORDERPRIORITY"_, "O_CLERK"_, "O_SHIPPRIORITY"_,
                          "O_COMMENT"_));
    }
    eval("CreateTable"_("LINEITEM"_, "L_ORDERKEY"_, "L_PARTKEY"_, "L_SUPPKEY"_, "L_LINENUMBER"_,
                        "L_QUANTITY"_, "L_EXTENDEDPRICE"_, "L_DISCOUNT"_, "L_TAX"_, "L_RETURNFLAG"_,
                        "L_LINESTATUS"_, "L_SHIPDATE"_, "L_COMMITDATE"_, "L_RECEIPTDATE"_,
                        "L_SHIPINSTRUCT"_, "L_SHIPMODE"_, "L_COMMENT"_));
    auto filenames = lineitemOnly
                         ? std::vector<std::string>{"lineitem"}
                         : std::vector<std::string>{"region",   "nation",   "part",   "supplier",
                                                    "partsupp", "customer", "orders", "lineitem"};
    auto tables = lineitemOnly
                      ? std::vector<boss::Symbol>{"LINEITEM"_}
                      : std::vector<boss::Symbol>{"REGION"_,   "NATION"_,   "PART"_,   "SUPPLIER"_,
                                                  "PARTSUPP"_, "CUSTOMER"_, "ORDERS"_, "LINEITEM"_};
    for(int i = 0; i < tables.size(); ++i) {
      std::filesystem::path path =
          "../data/tpch_" + std::to_string(size) + "MB/" + filenames[i] + ".tbl";
      auto absPath = path.is_absolute() ? path.string() : std::filesystem::absolute(path).string();
      auto it = std::find(absPath.begin(), absPath.end(), '\\');
      while(it != absPath.end()) {
        it = absPath.insert(it, '\\');
        it = std::find(it + 2, absPath.end(), '\\');
      }
      eval("Load"_(tables[i], absPath));
    }
  }
  lastSize = size;
  return *persistentEnginePtr();
};

enum DB_ENGINE { MONETDB, DUCKDB, BOSS_ENGINES_START };
static auto const DBEngineNames = std::vector<string>{"MonetDB", "DuckDB", "BOSS"};

enum TPCH_QUERIES { TPCH_Q1 = 1, TPCH_Q3 = 3, TPCH_Q6 = 6, TPCH_Q9 = 9, TPCH_Q18 = 18 };

static void TPCH6_BOSS(benchmark::State& state, int dataSize, std::string const& engineLibrary,
                       bool Q1Q6only) {
  auto& engine = initEngine_TPCH(engineLibrary, dataSize, Q1Q6only);
  auto eval = [&engine, &engineLibrary](boss::ComplexExpression const& expression) mutable {
    return engine.evaluate("EvaluateInEngine"_(engineLibrary, expression));
  };

  auto const queryQ6 = "Group"_(
      "Project"_("Select"_("LINEITEM"_,
                           "Where"_("And"_(
                               "Less"_("L_QUANTITY"_, 24), "GreaterEqual"_("L_DISCOUNT"_, 0.05F),
                               "GreaterEqual"_(0.07F, "L_DISCOUNT"_),
                               "Greater"_("DateObject"_("1995-01-01"), "L_SHIPDATE"_),
                               "GreaterEqual"_("L_SHIPDATE"_, "DateObject"_("1994-01-01"))))),
                 "As"_("revenue"_, "Times"_("L_EXTENDEDPRICE"_, "L_DISCOUNT"_))),
      "Sum"_("revenue"_));

  if constexpr(VERBOSE_QUERY_OUTPUT) {
    std::cout << "BOSS Q6 output = " << eval(queryQ6) << std::endl;
  }

  vtune.startSampling("TPC-H 6 - BOSS");
  for(auto _ : state) { // NOLINT
    auto output = eval("Extract"_("Extract"_(queryQ6, 1), 1));
    benchmark::DoNotOptimize(output);
  }
  vtune.stopSampling();
}

static void TPCH6_monetdb(benchmark::State& state, int dataSize, bool Q1Q6only) {
  auto& connection = initMonetDB(dataSize, Q1Q6only);
  monetdb_result* result = nullptr;

  auto queryQ6 = "select "s
                 "    sum(l_extendedprice * l_discount) as revenue "s
                 "from "s
                 "    lineitem "s
                 "where "s
                 "    l_shipdate >= date '1994-01-01' "s
                 "    and l_shipdate < date '1995-01-01'"s
                 "    and l_discount between 0.05 and 0.07"s
                 "    and l_quantity < 24;"s;

  if constexpr(VERBOSE_QUERY_OUTPUT) {
    std::cout << "MonetDB Q6 output = ";
    monetdb_query(connection, queryQ6.data(), 1, &result, NULL, NULL);
    printResult(result, 1);
    monetdb_cleanup_result(connection, result);
  }

  vtune.startSampling("TPC-H 6 - MonetDB");
  for(auto _ : state) { // NOLINT
    monetdb_query(connection, queryQ6.data(), 1, &result, NULL, NULL);
    benchmark::DoNotOptimize(result);
  }
  vtune.stopSampling();

  monetdb_cleanup_result(connection, result);
}

static void TPCH6_duckdb(benchmark::State& state, int dataSize, bool Q1Q6only) {
  auto& connection = initDuckDB(dataSize, Q1Q6only);

  auto lineitem = connection.Table("lineitem");
  auto queryQ6 = lineitem
                     ->Filter({"l_shipdate >= cast('1994-01-01' AS date)",
                               "l_shipdate < cast('1995-01-01' AS date)",
                               "l_discount BETWEEN 0.05 AND 0.07", "l_quantity < 24;"})
                     ->Aggregate("sum(l_extendedprice * l_discount) AS revenue");

  if constexpr(VERBOSE_QUERY_OUTPUT) {
    std::cout << "DuckDB Q6 output = ";
    auto result = queryQ6->Execute();
    result->Print();
  }

  vtune.startSampling("TPC-H 6 - DuckDB");
  for(auto _ : state) { // NOLINT
    auto result = queryQ6->Execute();
    benchmark::DoNotOptimize(result);
  }
  vtune.stopSampling();
}

static std::vector<string>
    librariesToTest{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static void TPCH_test(benchmark::State& state, int query, int dataSize, int engine, bool Q1Q6only) {
  static auto lastEngine = engine;
  if(lastEngine != engine) {
    switch(lastEngine) {
    case MONETDB:
      releaseMonetDB();
      break;
    case DUCKDB:
      releaseDuckDB();
      break;
    default:
      // assuming >= BOSS_ENGINES_START
      persistentEnginePtr().reset();
      break;
    }
  }
  lastEngine = engine;

  switch(engine) {
  case MONETDB:
    switch(query) {
    case TPCH_Q1:
    case TPCH_Q3:
      break;
    case TPCH_Q6:
      TPCH6_monetdb(state, dataSize, Q1Q6only);
      break;
    case TPCH_Q9:
    case TPCH_Q18:
    default:
      break;
    }
    break;
  case DUCKDB:
    switch(query) {
    case TPCH_Q1:
    case TPCH_Q3:
      break;
    case TPCH_Q6:
      TPCH6_duckdb(state, dataSize, Q1Q6only);
      break;
    case TPCH_Q9:
    case TPCH_Q18:
    default:
      break;
    }
    break;
  default:
    if(engine >= BOSS_ENGINES_START) {
      auto engineIndex = engine - BOSS_ENGINES_START;
      switch(query) {
      case TPCH_Q1:
      case TPCH_Q3:
        break;
      case TPCH_Q6:
        TPCH6_BOSS(state, dataSize, librariesToTest[engineIndex], Q1Q6only);
        break;
      case TPCH_Q9:
      case TPCH_Q18:
      default:
        break;
      }
    }
    break;
  }
}

template <typename... Args>
benchmark::internal::Benchmark* RegisterBenchmarkNolint([[maybe_unused]] Args... args) {
#ifdef __clang_analyzer__
  // There is not way to disable clang-analyzer-cplusplus.NewDeleteLeaks
  // even though it is perfectly safe. Let's just please clang analyzer.
  return nullptr;
#else
  return benchmark::RegisterBenchmark(args...);
#endif
}

template <typename Func0, typename Func1>
void RegisterForAllEngines(std::string const& name, Func0&& funcVectors, Func1&& funcBOSS,
                           int minRange, int maxRange) {
  auto nameTestVectors = name + "/Vectors";
  RegisterBenchmarkNolint(nameTestVectors.c_str(), funcVectors)->Range(minRange, maxRange);
  for(auto const& bossLibraryName : librariesToTest) {
    auto nameTestBOSS = name + "/";
    nameTestBOSS += bossLibraryName;
    RegisterBenchmarkNolint(nameTestBOSS.c_str(), funcBOSS, bossLibraryName)
        ->Range(minRange, maxRange);
  }
}

template <typename Func0, typename Func1>
void RegisterForAllEngines(std::string const& name, Func0&& funcVectors, Func1&& funcBOSS,
                           int minRange, int maxRange, int minSelectivity, int maxSelectivity) {
  auto nameTestVectors = name + "/Vectors";
  RegisterBenchmarkNolint(nameTestVectors.c_str(), funcVectors)
      ->Ranges({{minRange, maxRange}, {minSelectivity, maxSelectivity}});
  for(auto const& bossLibraryName : librariesToTest) {
    auto nameTestBOSS = name + "/";
    nameTestBOSS += bossLibraryName;
    RegisterBenchmarkNolint(nameTestBOSS.c_str(), funcBOSS, bossLibraryName)
        ->Ranges({{minRange, maxRange}, {minSelectivity, maxSelectivity}});
  }
}

int main(int argc, char** argv) {
  // read custom arguments
  bool Q1Q6only = false;
  for(int i = 0; i < argc; ++i) {
    if(std::string("--Q1Q6only") == argv[i]) {
      Q1Q6only = true;
    } else if(std::string("--library") == argv[i]) {
      if(++i < argc) {
        librariesToTest.emplace_back(argv[i]);
      }
    }
  }
  // register basic benchmarks
  static auto const minSize = 1U << 10U;
  static auto const maxSize = 1U << 27U;
  RegisterForAllEngines("Addition", Addition_Vectors, Addition_BOSS, minSize, maxSize);
  RegisterForAllEngines("Comparison", Comparison_Vectors, Comparison_BOSS, minSize, maxSize);
  RegisterForAllEngines("Logic", Logic_Vectors, Logic_BOSS, minSize, maxSize);
  RegisterForAllEngines("Compound", Compound_Vectors, Compound_BOSS, minSize, maxSize);
  RegisterForAllEngines("StringContains", StringContains_Vectors, StringContains_BOSS, minSize,
                        maxSize);
  RegisterForAllEngines("StringJoin", StringJoin_Vectors, StringJoin_BOSS, minSize, maxSize);
  RegisterForAllEngines("SelectBool", SelectBool_Vectors, SelectBool_BOSS, minSize, maxSize, 1,
                        maxSize);
  RegisterForAllEngines("SelectInt", SelectInt_Vectors, SelectInt_BOSS, minSize, maxSize, 1,
                        maxSize);
  RegisterForAllEngines("SelectString", SelectString_Vectors, SelectString_BOSS, minSize, maxSize,
                        1, maxSize);
  // register TPC-H benchmarks
  for(int dataSize : std::vector<int>{1, 10, 100, 1000, 2000, 5000, 10000, 100000}) {
    for(int engine = 0; engine < BOSS_ENGINES_START + librariesToTest.size(); ++engine) {
      for(int query :
          Q1Q6only
              ? std::vector<int>{/*TPCH_Q1,*/ TPCH_Q6}                                     // NOLINT
              : std::vector<int>{/*TPCH_Q1, TPCH_Q3,*/ TPCH_Q6 /*, TPCH_Q9, TPCH_Q18*/}) { // NOLINT
        std::ostringstream testName;
        testName << "TPCH_Q" << query << "/";
        if(engine < BOSS_ENGINES_START) {
          testName << DBEngineNames[engine];
        } else {
          testName << librariesToTest[engine - BOSS_ENGINES_START];
        }
        testName << "/" << dataSize << "MB";
        RegisterBenchmarkNolint(testName.str().c_str(), TPCH_test, query, dataSize, engine,
                                Q1Q6only);
      }
    }
  }
  // initialise and run google benchmark
  ::benchmark::Initialize(&argc, argv);
  ::benchmark::RunSpecifiedBenchmarks();
}
