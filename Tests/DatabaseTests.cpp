#include "../Source/Engines/MLIREngine/Runtime/Database.hpp"
#include "../Source/Utilities.hpp"
#include <catch2/catch.hpp>
#include <iostream>
#include <memory>

using boss::utilities::operator""_;

runtime::Table createTestTable() {
  runtime::Table table;

  // Create a schema
  std::shared_ptr<arrow::Field> field_a, field_a_sym, field_b, field_b_sym;
  std::shared_ptr<arrow::Schema> schema;

  field_a = arrow::field("A", arrow::int32());
  field_a_sym = arrow::field("A_symbols", arrow::binary());
  field_b = arrow::field("B", arrow::boolean());
  field_b_sym = arrow::field("B_symbols", arrow::binary());

  schema = std::make_shared<arrow::Schema, std::vector<std::shared_ptr<arrow::Field>>>(
      {field_a, field_a_sym, field_b, field_b_sym});

  // load tuples into the table
  table.bulk_load(schema, {{{"A", 1}, {"B", false}},
                           {{"A", 2}, {"B", false}},
                           {{"A", boss::Symbol("undef")}, {"B", false}}});

  return table;
}

TEST_CASE("DATABASE TEST") {

  SECTION("RawValues") {
    // Just to demonstrate how the raw values can be accessed
    arrow::BinaryBuilder builder;

    auto val = "AAAA";

    builder.Append("01\0ABBBB", 8);
    builder.Append("02\0A", 4);
    builder.Append("03\0ABBBB", 8);
    builder.AppendEmptyValue();
    builder.Append("04\0ABBBB", 8);

    auto array = builder.Finish().ValueOrDie();

    auto binaryArray = std::static_pointer_cast<arrow::BinaryArray>(array);

    std::cout << binaryArray->ToString() << "\n";

    auto data = binaryArray->raw_data();

    for(auto i = 0u; i < binaryArray->length(); i++) {
      std::cout << "Offset " << i << " " << *(binaryArray->raw_value_offsets() + i) << "\n";
      for(auto j = *(binaryArray->raw_value_offsets() + i);
          j < *(binaryArray->raw_value_offsets() + i + 1); j++) {
        std::cout << +*(data + j);
      }
      std::cout << "\n";
    }
  }

  SECTION("TableTest") {
    runtime::Table table = createTestTable();

    // Get columns from table
    auto colA =
        std::static_pointer_cast<arrow::Int32Array>(table.getColumnDataPtr("A", false)->chunk(0));
    auto colASyms =
        std::static_pointer_cast<arrow::BinaryArray>(table.getColumnDataPtr("A", true)->chunk(0));

    CHECK(colA->length() == 3);
    CHECK(colA->Value(0) == 1);
    CHECK(colA->Value(1) == 2);
    CHECK(colASyms->IsNull(0));
    CHECK(!colASyms->IsNull(2));
    CHECK(colASyms->GetString(2) == "Symbol{undef}");
    CHECK(colA->IsNull(2));
  }

  SECTION("QueryTests") {
    runtime::Database database;
    runtime::Table table;

    // Create a schema
    std::shared_ptr<arrow::Field> field_a, field_a_sym, field_b, field_b_sym;
    std::shared_ptr<arrow::Schema> schema;

    field_a = arrow::field("A", arrow::int32());
    field_a_sym = arrow::field("A_symbols", arrow::binary());
    field_b = arrow::field("B", arrow::int32());
    field_b_sym = arrow::field("B_symbols", arrow::binary());

    schema = std::make_shared<arrow::Schema, std::vector<std::shared_ptr<arrow::Field>>>(
        {field_a, field_a_sym, field_b, field_b_sym});

    // load tuples into the table
    table.bulk_load(schema, {{{"A", 42}, {"B", 43}},
                             {{"A", 2}, {"B", 1}}});


    database.addRelation("Relation1", std::move(table));
    boss::engines::mlir::Engine e(std::move(database));

    auto result = std::get<size_t>(e.evaluate("CollectTuples"_("GetRelation"_((std::string)"Relation1"))));

    auto* resultTable = reinterpret_cast<runtime::Table*>(result);

    CHECK(resultTable->getSchema()->num_fields() == 2);

    CHECK(std::static_pointer_cast<arrow::Int32Array>(resultTable->getColumnDataPtr("A", false)->chunk(0))->Value(0) == 42);
    CHECK(std::static_pointer_cast<arrow::Int32Array>(resultTable->getColumnDataPtr("A", false)->chunk(0))->Value(1) == 2);
    CHECK(std::static_pointer_cast<arrow::Int32Array>(resultTable->getColumnDataPtr("B", false)->chunk(0))->Value(0) == 43);
    CHECK(std::static_pointer_cast<arrow::Int32Array>(resultTable->getColumnDataPtr("B", false)->chunk(0))->Value(1) == 1);
  }
}
