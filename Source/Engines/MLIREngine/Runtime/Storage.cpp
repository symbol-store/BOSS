#include "Storage.hpp"

#include "Utilities.hpp"
#include "Strings.hpp"
#include <functional>
#include <iostream>
#include <map>
#include <fstream>
#include <sstream>

using boss::ComplexExpression;
using boss::Expression;
using boss::ExpressionArguments;
using boss::Symbol;
using boss::utilities::operator""_;
using boss::mlir::runtime::string::RuntimeString;

using std::string;
using namespace std::string_literals;

class ComplexExpressionArrayBuilder : public arrow::StructBuilder {
public:
  ComplexExpressionArrayBuilder(std::shared_ptr<arrow::DataType>&& type,
                                std::vector<std::shared_ptr<arrow::ArrayBuilder>>&& fieldBuilders,
                                arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StructBuilder(type, pool, std::move(fieldBuilders)) {}
};

class SymbolArrayBuilder : public arrow::StructBuilder {
public:
  SymbolArrayBuilder(std::shared_ptr<arrow::DataType>&& type,
                     std::vector<std::shared_ptr<arrow::ArrayBuilder>>&& fieldBuilders,
                     arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StructBuilder(type, pool, std::move(fieldBuilders)) {}

  arrow::Status AppendSymbol(std::string const& symbolName, size_t globalOffset) {
    auto status = Append();
    ARROW_RETURN_NOT_OK(status);
    auto stringStatus = std::dynamic_pointer_cast<arrow::StringDictionaryBuilder>(child_builder(0))
                            ->Append(symbolName);
    ARROW_RETURN_NOT_OK(stringStatus);
    auto offsetStatus =
        std::dynamic_pointer_cast<arrow::Int64Builder>(child_builder(1))->Append(globalOffset);
    ARROW_RETURN_NOT_OK(offsetStatus);
    return arrow::Status::OK();
  }
};

struct CompareExpression {
  bool operator()(Expression const& lhs, Expression const& rhs) const {
    return compare(lhs, rhs) < 0;
  }

private:
  int compare(Expression const& lhs, Expression const& rhs) const {
    if(lhs.index() != rhs.index()) {
      return lhs.index() < rhs.index() ? -1 : 1;
    }

    // uncomment to dispatch symbols
    if(auto const* lhsSymbol = std::get_if<Symbol>(&lhs)) {
      auto const& rhsSymbol = std::get<Symbol>(rhs);
      return lhsSymbol->getName() < rhsSymbol.getName() ? -1 : 1;
    }

    if(auto const* lhsExpr = std::get_if<ComplexExpression>(&lhs)) {
      auto const& rhsExpr = std::get<ComplexExpression>(rhs);
      if(lhsExpr->getHead().getName() != rhsExpr.getHead().getName()) {
        return lhsExpr->getHead().getName() < rhsExpr.getHead().getName() ? -1 : 1;
      }

      auto lhsArgsIt = lhsExpr->getArguments().begin();
      auto rhsArgsIt = rhsExpr.getArguments().begin();
      auto lhsArgsItEnd = lhsExpr->getArguments().end();
      auto rhsArgsItEnd = rhsExpr.getArguments().end();
      size_t lhsNumArgs = std::distance(lhsArgsIt, lhsArgsItEnd);
      size_t rhsNumArgs = std::distance(rhsArgsIt, rhsArgsItEnd);

      if(lhsNumArgs != rhsNumArgs) {
        return lhsNumArgs < rhsNumArgs ? -1 : 1;
      }

      while(lhsArgsIt != lhsArgsItEnd /*&& rhsArgsIt != rhsArgsItEnd*/) {
        int argCompare = compare(*lhsArgsIt, *rhsArgsIt);
        if(argCompare != 0) {
          return argCompare;
        }
        ++lhsArgsIt;
        ++rhsArgsIt;
      }
    }

    // "normal" values (of identical type) are all dispatched to the same array
    return 0;
  }
};

struct CompareTuple {
  // true iff lhs < rhs
  bool operator()(std::map<std::string, boss::Expression> const& lhs,
                  std::map<std::string, boss::Expression> const& rhs) const {
    // TODO this is an approximation and may lead to crashes if there is a hash collision
    if(hashFieldNames(lhs) != hashFieldNames(rhs)) {
      return hashFieldNames(lhs) < hashFieldNames(rhs);
    }

    // TODO assumes order for all tuples is always the same
    auto lhsPtr = lhs.begin();
    auto rhsPtr = rhs.begin();

    while(lhsPtr != lhs.end() && rhsPtr != rhs.end()) {
      auto compValue = compareExpressions(lhsPtr->second, rhsPtr->second);
      if(compValue != 0) {
        return compValue < 0;
      }
      lhsPtr++;
      rhsPtr++;
    }

    return false;
  }

private:
  static size_t hashFieldNames(std::map<std::string, boss::Expression> const& fields) {
    size_t hash = 0;
    for(auto const& field : fields) {
      hash += std::hash<std::string>()(field.first);
    }
    return hash;
  }

  static int compareExpressions(Expression const& lhs, Expression const& rhs) {
    if(lhs.index() != rhs.index()) {
      return lhs.index() < rhs.index() ? -1 : 1;
    }

    // uncomment to dispatch symbols
    if(auto const* lhsSymbol = std::get_if<Symbol>(&lhs)) {
      auto const& rhsSymbol = std::get<Symbol>(rhs);
      return lhsSymbol->getName() < rhsSymbol.getName() ? -1 : 1;
    }

    if(auto const* lhsExpr = std::get_if<ComplexExpression>(&lhs)) {
      auto const& rhsExpr = std::get<ComplexExpression>(rhs);
      if(lhsExpr->getHead().getName() != rhsExpr.getHead().getName()) {
        return lhsExpr->getHead().getName() < rhsExpr.getHead().getName() ? -1 : 1;
      }

      auto lhsArgsIt = lhsExpr->getArguments().begin();
      auto rhsArgsIt = rhsExpr.getArguments().begin();
      auto lhsArgsItEnd = lhsExpr->getArguments().end();
      auto rhsArgsItEnd = rhsExpr.getArguments().end();
      size_t lhsNumArgs = std::distance(lhsArgsIt, lhsArgsItEnd);
      size_t rhsNumArgs = std::distance(rhsArgsIt, rhsArgsItEnd);

      if(lhsNumArgs != rhsNumArgs) {
        return lhsNumArgs < rhsNumArgs ? -1 : 1;
      }

      while(lhsArgsIt != lhsArgsItEnd /*&& rhsArgsIt != rhsArgsItEnd*/) {
        int argCompare = compareExpressions(*lhsArgsIt, *rhsArgsIt);
        if(argCompare != 0) {
          return argCompare;
        }
        ++lhsArgsIt;
        ++rhsArgsIt;
      }
    }

    // "normal" values (of identical type) are all dispatched to the same array
    return 0;
  }
};

class TypedDatabaseBuilder : public arrow::StructBuilder {
public:
  using Tuple = std::map<std::string, boss::Expression>;

  TypedDatabaseBuilder(std::shared_ptr<arrow::DataType>&& type,
                       std::vector<std::shared_ptr<arrow::ArrayBuilder>>&& fieldBuilders,
                       arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StructBuilder(type, pool, std::move(fieldBuilders)) {}

  static std::shared_ptr<TypedDatabaseBuilder> Make(Tuple const& tuple) {
    std::vector<std::shared_ptr<arrow::ArrayBuilder>> argBuilders;
    std::vector<std::shared_ptr<arrow::Field>> fields;

    for(auto const& [fieldName, expression] : tuple) {
      fields.emplace_back(std::make_shared<arrow::Field>(fieldName, nullptr));
      argBuilders.push_back(builderFromExpression(expression));
    }

    return std::make_shared<TypedDatabaseBuilder>(std::make_shared<arrow::StructType>(fields),
                                                  std::move(argBuilders));
  }

  arrow::Status AppendTuple(Tuple const& tuple, size_t globalUnionIndex) {
    auto i = 0U;
    auto parentStatus = Append();
    ARROW_RETURN_NOT_OK(parentStatus);
    for(auto const& field : tuple) {
      auto childStatus =
          appendToChildBuilder(field.second, this->child_builder(i++), globalUnionIndex);
      ARROW_RETURN_NOT_OK(childStatus);
    }
    return arrow::Status::OK();
  }

private:
  static std::shared_ptr<arrow::ArrayBuilder> builderFromExpression(Expression const& expr) {
    return std::visit(
        boss::utilities::overload(
            [&](bool /*v*/) -> std::shared_ptr<arrow::ArrayBuilder> {
              return std::make_shared<arrow::BooleanBuilder>();
            },
            [&](int /*v*/) -> std::shared_ptr<arrow::ArrayBuilder> {
              return std::make_shared<arrow::Int32Builder>();
            },
            [&](size_t /*v*/) -> std::shared_ptr<arrow::ArrayBuilder> {
              return std::make_shared<arrow::Int64Builder>();
            },
            [&](float /*v*/) -> std::shared_ptr<arrow::ArrayBuilder> {
              return std::make_shared<arrow::FloatBuilder>();
            },
            [&](string const& /*v*/) -> std::shared_ptr<arrow::ArrayBuilder> {
              return std::make_shared<arrow::StringBuilder>();
            },
            [&](Symbol const& /*s*/) -> std::shared_ptr<arrow::ArrayBuilder> {
              std::vector<std::shared_ptr<arrow::ArrayBuilder>> argBuilders(2);
              std::vector<std::shared_ptr<arrow::Field>> fields(2);

              // Make field to hold symbol name
              argBuilders[0] = std::make_shared<arrow::StringDictionaryBuilder>();
              fields[0] = std::make_shared<arrow::Field>("Symbol", nullptr);
              // Make fields to hold offset in global union array order
              argBuilders[1] = std::make_shared<arrow::Int64Builder>();
              fields[1] = std::make_shared<arrow::Field>("GlobalOffset", nullptr);

              return std::make_shared<SymbolArrayBuilder>(
                  std::make_shared<arrow::StructType>(fields), std::move(argBuilders));
            },
            [&](ComplexExpression const& e) -> std::shared_ptr<arrow::ArrayBuilder> {
              std::vector<std::shared_ptr<arrow::ArrayBuilder>> argBuilders;
              argBuilders.reserve(1 + e.getArguments().size());

              std::vector<std::shared_ptr<arrow::Field>> fields;
              fields.reserve(1 + e.getArguments().size());

              // head
              auto headBuilder = std::make_shared<arrow::StringDictionaryBuilder>();
              argBuilders.push_back(std::move(headBuilder));
              // TODO currently using the name of the first field to store head name. The name is
              // also stored in the array itself.
              // TODO maybe make the array null instead? Or find a way to add metadata
              fields.push_back(std::make_shared<arrow::Field>(e.getHead().getName(), nullptr));

              // args
              for(auto const& arg : e.getArguments()) {
                argBuilders.push_back(builderFromExpression(arg));
                fields.push_back(
                    std::make_shared<arrow::Field>("arg" + std::to_string(fields.size()), nullptr));
              }

              if(e.getHead().getName() == "NextValue") {
                // If the symbol is next value, also add an index for this value
                argBuilders.push_back(std::make_shared<arrow::Int64Builder>());
                fields.push_back(std::make_shared<arrow::Field>("GlobalOffset", nullptr));
              }

              return std::make_shared<ComplexExpressionArrayBuilder>(
                  std::make_shared<arrow::StructType>(fields), std::move(argBuilders));
            }),
        expr);
  }

  arrow::Status appendToChildBuilder(Expression const& expr,
                                     std::shared_ptr<arrow::ArrayBuilder> const& childBuilder,
                                     size_t globalIdx) {
    return std::visit(
        boss::utilities::overload(
            [&](bool v) {
              return std::dynamic_pointer_cast<arrow::BooleanBuilder>(childBuilder)->Append(v);
            },
            [&](int v) {
              return std::dynamic_pointer_cast<arrow::Int32Builder>(childBuilder)->Append(v);
            },
            [&](size_t v) {
              return std::dynamic_pointer_cast<arrow::Int64Builder>(childBuilder)->Append(v);
            },
            [&](float v) {
              return std::dynamic_pointer_cast<arrow::FloatBuilder>(childBuilder)->Append(v);
            },
            [&](string const& v) {
              return std::dynamic_pointer_cast<arrow::StringBuilder>(childBuilder)->Append(v);
            },
            [&](Symbol const& s) {
              return std::dynamic_pointer_cast<SymbolArrayBuilder>(childBuilder)
                  ->AppendSymbol(s.getName(), globalIdx);
            },
            [&](ComplexExpression const& e) {
              auto exprBuilder =
                  std::dynamic_pointer_cast<ComplexExpressionArrayBuilder>(childBuilder);

              // append to the args structure
              auto structStatus = exprBuilder->Append();
              ARROW_RETURN_NOT_OK(structStatus);

              // append head
              auto headStatus = std::dynamic_pointer_cast<arrow::StringDictionaryBuilder>(
                                    exprBuilder->child_builder(0))
                                    ->Append(e.getHead().getName());
              ARROW_RETURN_NOT_OK(headStatus);

              // append each argument
              for(auto idx = 0U; idx < e.getArguments().size(); ++idx) {
                auto argBuilder = exprBuilder->child_builder(1 + idx);
                auto status = appendToChildBuilder(e.getArguments()[idx], argBuilder, globalIdx);
                ARROW_RETURN_NOT_OK(status);
              }

              // If the symbol needs to refer to other database values, add index
              if(e.getHead().getName() == "NextValue") {
                auto idxBdr = exprBuilder->child_builder(e.getArguments().size() + 1);
                auto s = std::dynamic_pointer_cast<arrow::Int64Builder>(idxBdr)->Append(globalIdx);
                ARROW_RETURN_NOT_OK(s);
              }

              return arrow::Status::OK();
            }),
        expr);
  }
};

class DatabaseBuilder : public arrow::DenseUnionBuilder {
public:
  using arrow::DenseUnionBuilder::DenseUnionBuilder;
  using Tuple = std::map<std::string, boss::Expression>;

  arrow::Status AppendTuple(Tuple const& tuple) {
    auto it = tupleToBuilder.find(tuple);
    std::shared_ptr<ArrayBuilder> childBuilder;
    bool foundInCache(it != tupleToBuilder.end());
    auto& cachedTypeId = foundInCache ? it->second : tupleToBuilder[tuple];
    if(foundInCache) {
      // retrieve from the cache
      childBuilder = child_builder(cachedTypeId);
    } else {
      // This type of expression is not supported yet
      // Append a new array for this expression
      // and cache the id for next time
      childBuilder = makeTypedDatabaseBuilder(tuple);
      cachedTypeId = AppendChild(childBuilder);
    }
    auto status = Append(cachedTypeId);
    ARROW_RETURN_NOT_OK(status);
    return appendToChildBuilder(tuple, childBuilder);
  }

private:
  arrow::Status appendToChildBuilder(Tuple const& tuple,
                                     std::shared_ptr<ArrayBuilder> const& childBuilder) {
    auto globalUnionIndex = length() - 1;
    return std::dynamic_pointer_cast<TypedDatabaseBuilder>(childBuilder)
        ->AppendTuple(tuple, globalUnionIndex);
  }

  static std::shared_ptr<TypedDatabaseBuilder> makeTypedDatabaseBuilder(Tuple const& tuple) {
    return TypedDatabaseBuilder::Make(tuple);
  }

  std::map<Tuple, uint8_t, CompareTuple> tupleToBuilder;
};

void new_runtime::Relation::bulk_load(
    std::vector<std::map<std::string, boss::Expression>> const& tuples) {
  DatabaseBuilder databaseBuilder(arrow::default_memory_pool());

  for(auto const& tuple : tuples) {
    auto result = databaseBuilder.AppendTuple(tuple);
    if(!result.ok()) {
      throw std::runtime_error("Error adding tuple");
    }
  }

  auto finishResult = databaseBuilder.Finish();
  if(!finishResult.ok()) {
    throw std::runtime_error("Error finishing array construction");
  }

  relation = std::dynamic_pointer_cast<arrow::DenseUnionArray>(finishResult.ValueUnsafe());
}

std::vector<std::string> split(const std::string& s, char delimiter)
{
  std::vector<std::string> splits;
  std::string split;
  std::istringstream ss(s);
  while (std::getline(ss, split, delimiter))
  {
    splits.push_back(split);
  }
  return splits;
}

bool isInteger(std::string const& expr) {
  char* p;
  strtol(expr.c_str(), &p, 10);
  return *p == 0;
}

bool isFloat(std::string const& expr) {
  char* p;
  strtof(expr.c_str(), &p);
  return *p == 0;
}

boss::Expression fileStringToExpression(std::string const& s) {
  if (isInteger(s)) {
    return Expression(std::stoi(s));
  }
  if (isFloat(s)) {
    return Expression(std::stof(s));
  }
  if (s == "true") {
    return Expression(true);
  }
  if (s == "false") {
    return Expression(false);
  }
  if (s.at(0) == 's') {
    return Expression(s.substr(1));
  }
  if (s.at(0) == 'S') {
    return Expression(Symbol(s.substr(1)));
  }
  if (s.at(0) == 'e') {
    // This is an expression, split arguments by space
    auto splitExpression = split(s, ' ');
    auto head = splitExpression[0].substr(1);
    std::vector<Expression> arguments;
    for (auto i = 1U; i < splitExpression.size(); i++) {
      arguments.emplace_back(fileStringToExpression(splitExpression[i]));
    }
    return ComplexExpression(Symbol(head), arguments);
  }
  throw std::runtime_error("Error parsing file: No valid expression found");
}

void new_runtime::Relation::loadFromFile(const string& fileName) {
  DatabaseBuilder databaseBuilder(arrow::default_memory_pool());

  std::ifstream file;
  file.open(fileName);
  if (!file.is_open()) {
    throw std::runtime_error("Error opening file " + fileName);
  }

  // Get header
  std::string line;
  if (!std::getline(file, line)) {
    throw std::runtime_error("Failed to read CSV header");
  }

  auto columnNames = split(line, ',');

  while (std::getline(file, line)) {
    std::map<std::string, boss::Expression> tuple;
    auto columnData = split(line, ',');
    if (columnData.size() != columnNames.size()) {
      throw std::runtime_error("Unexpected number of data points in row");
    }

    for (auto i = 0U; i < columnNames.size(); i++) {
      auto const& name = columnNames[i];
      auto const& rawData = columnData[i];
      tuple[name] = fileStringToExpression(rawData);
    }
    auto status = databaseBuilder.AppendTuple(tuple);
    if (!status.ok()) {
      throw std::runtime_error(status.message());
    }
  }

  auto finishResult = databaseBuilder.Finish();
  if(!finishResult.ok()) {
    throw std::runtime_error("Error finishing array construction");
  }

  relation = std::dynamic_pointer_cast<arrow::DenseUnionArray>(finishResult.ValueUnsafe());
}

std::shared_ptr<arrow::ArrayBuilder>
new_runtime::RelationBuilder::getOrCreateColumnBuilder(std::string fieldName,
                                                       Fields const& fields) {
  auto structBuilder = getOrCreateTypedStructBuilder(fields);

  auto structType = structBuilder->type();
  for(auto i = 0; i < structType->num_fields(); i++) {
    if(structType->field(i)->name() == fieldName) {
      return structBuilder->child_builder(i);
    }
  }

  throw std::runtime_error("The field " + fieldName + " didn't exist in the builder!");
}

new_runtime::Relation* new_runtime::RelationBuilder::build() {
  auto result = builder->Finish();
  if(!result.ok()) {
    return nullptr;
  }

  auto denseUnionArray = std::dynamic_pointer_cast<arrow::DenseUnionArray>(result.ValueOrDie());
  return new Relation(denseUnionArray);
}

std::shared_ptr<arrow::ArrayBuilder>
new_runtime::RelationBuilder::builderForType(boss::mlir::types::RuntimeTypes type) {
  switch(type) {
  case boss::mlir::types::RuntimeTypes::INT:
    return std::make_shared<arrow::Int32Builder>();
  case boss::mlir::types::RuntimeTypes::BOOLEAN:
    return std::make_shared<arrow::BooleanBuilder>();
  case boss::mlir::types::RuntimeTypes::STRING:
    return std::make_shared<arrow::StringBuilder>();
  case boss::mlir::types::RuntimeTypes::FLOAT:
    return std::make_shared<arrow::FloatBuilder>();
  case boss::mlir::types::RuntimeTypes::SYMBOL:
    // TODO do we want binary? Probably just store as BSON.
    return std::make_shared<arrow::BinaryBuilder>();
  default:
    throw std::runtime_error("Cannot insert type into relation");
  }
}

std::shared_ptr<arrow::ArrayBuilder>
new_runtime::RelationBuilder::getOrCreateTypedStructBuilder(Fields const& fields) {
  auto index = getOrCreateTypedStructBuilderIndex(fields);
  auto structBuilder =
      std::dynamic_pointer_cast<arrow::StructBuilder>(builder->child_builder(index));
  if(!structBuilder) {
    throw std::runtime_error("A child builder wasn't a struct builder");
  }
  return structBuilder;
}

int8_t new_runtime::RelationBuilder::getOrCreateTypedStructBuilderIndex(
    const new_runtime::RelationBuilder::Fields& fields) {
  auto childBuilderIndex = fieldsToBuilder.find(fields);
  if(childBuilderIndex == fieldsToBuilder.end()) {
    // We have never seen these fields before. Create a new child builder.
    std::vector<std::shared_ptr<arrow::ArrayBuilder>> argBuilders;
    std::vector<std::shared_ptr<arrow::Field>> arrowFields;

    for(auto const& [fieldName, type] : fields) {
      arrowFields.emplace_back(std::make_shared<arrow::Field>(fieldName, nullptr));
      auto childBuilder = builderForType(type);
      argBuilders.push_back(childBuilder);
    }

    auto structBuilder = std::make_shared<TypedDatabaseBuilder>(
        std::make_shared<arrow::StructType>(arrowFields), std::move(argBuilders));
    auto newChildIndex = builder->AppendChild(structBuilder);
    fieldsToBuilder[fields] = newChildIndex;
    return newChildIndex;
  }
  return childBuilderIndex->second;
}

extern "C" int loadIndirect_Int(arrow::DenseUnionArray* array, size_t columnIndex, size_t typeId,
                                size_t valueOffset) {
  auto childArray = array->field(typeId);
  auto structArray = std::dynamic_pointer_cast<arrow::StructArray>(childArray);
  auto column = structArray->field(columnIndex);
  auto intColumn = std::dynamic_pointer_cast<arrow::Int32Array>(column);
  return intColumn->Value(valueOffset);
}

extern "C" new_runtime::Relation* finalizeRelationBuilder(new_runtime::RelationBuilder& builder) {
  return builder.build();
}

extern "C" void addToRelation_Int(arrow::ArrayBuilder* builder, int value) {
  auto status = dynamic_cast<arrow::Int32Builder*>(builder)->Append(value);
}

extern "C" void addToRelation_Float(arrow::ArrayBuilder* builder, float value) {
  auto status = dynamic_cast<arrow::FloatBuilder*>(builder)->Append(value);
}

extern "C" void addToRelation_Bool(arrow::ArrayBuilder* builder, bool value) {
  auto status = dynamic_cast<arrow::BooleanBuilder*>(builder)->Append(value);
}

extern "C" void addToRelation_String(arrow::ArrayBuilder* builder, RuntimeString* value) {
  auto stringArray = dynamic_cast<arrow::StringBuilder*>(builder);
  auto status = stringArray->Append(value->data, value->length);
}

extern "C" size_t advanceBuilder(arrow::StructBuilder* structBuilder,
                                 arrow::DenseUnionBuilder* unionBuilder, int8_t child) {
  // TODO error handling
  unionBuilder->Append(child);
  structBuilder->Append();
  return structBuilder->length() - 1;
}
