#include "../Source/BOSS.hpp"
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
using namespace std;

TEMPLATE_TEST_CASE("Simpletons", "", boss::engines::bulk::Engine) {
  auto engine = TestType();

  SECTION("Relational") {
    auto& table1 = engine.table("T1");
    table1.template addColumn<int>("A");
    table1.template addColumn<int>("B");
    table1.template addColumn<float>("C");

    std::vector<int> column1 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> column2 = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    std::vector<float> column3 = {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f};

    typename TestType::SymbolicTable::TupleVectorImpl tuples;
    tuples.reserve(column1.size());
    for(size_t idx = 0; idx < column1.size(); ++idx) {
      tuples.emplace_back();
      auto& tuple = tuples.back();
      tuple.reserve(3);
      tuple.add(column1[idx]);
      tuple.add(column2[idx]);
      tuple.add(column3[idx]);
    }

    REQUIRE(table1.addRows(tuples) == true);

    REQUIRE(table1.select("C", 10.0f, TestType::SymbolicTable::PredicateOp::EQUALS)
                .project({"A"})
                .template get<int>(0, 0) == 10);
    REQUIRE(table1.select("B", 9, TestType::SymbolicTable::PredicateOp::GREATER)
                .project({"B"})
                .template get<int>(0, 0) == 10);
    REQUIRE(table1.project({"C"})
                .select("C", 1.0f, TestType::SymbolicTable::PredicateOp::DIFFERENT)
                .select("C", 0.0f, TestType::SymbolicTable::PredicateOp::DIFFERENT)
                .template get<float>(0, 0) == 10.0f);

    REQUIRE(table1.sort("B").project({"B"}).template get<int>(0, 0) == 1);
    REQUIRE(table1.project({"B"}).sort("B").template get<int>(0, 9) == 10);

    REQUIRE(table1.aggregate({"A"}, TestType::SymbolicTable::AggregateOp::MIN)
                .template get<int>(0, 0) == 1);
    REQUIRE(table1.aggregate({"A"}, TestType::SymbolicTable::AggregateOp::MAX)
                .template get<int>(0, 0) == 10);
    REQUIRE(table1.aggregate({"B"}, TestType::SymbolicTable::AggregateOp::MIN)
                .template get<int>(0, 0) == 1);
    REQUIRE(table1.aggregate({"B"}, TestType::SymbolicTable::AggregateOp::MAX)
                .template get<int>(0, 0) == 10);
    REQUIRE(table1.aggregate({"C"}, TestType::SymbolicTable::AggregateOp::MIN)
                .template get<float>(0, 0) == 0.0f);
    REQUIRE(table1.aggregate({"C"}, TestType::SymbolicTable::AggregateOp::MAX)
                .template get<float>(0, 0) == 10.0f);
  }
}