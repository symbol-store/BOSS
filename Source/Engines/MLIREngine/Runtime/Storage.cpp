#include "Storage.hpp"

#include "Utilities.hpp"
#include <functional>
#include <map>
#include <iostream>

using boss::ComplexExpression;
using boss::Expression;
using boss::ExpressionArguments;
using boss::Symbol;
using boss::utilities::operator""_;

using std::string;
using namespace std::string_literals;

class ComplexExpressionArrayBuilder : public arrow::StructBuilder {
public:
  ComplexExpressionArrayBuilder(std::shared_ptr<arrow::DataType>&& type,
                                std::vector<std::shared_ptr<arrow::ArrayBuilder>>&& fieldBuilders,
                                arrow::MemoryPool* pool = arrow::default_memory_pool())
      : arrow::StructBuilder(type, pool, std::move(fieldBuilders)) {}
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
    /*if(auto const* lhsSymbol = std::get_if<Symbol>(&lhs)) {
      auto const& rhsSymbol = std::get<Symbol>(rhs);
      return lhsSymbol->getName() < rhsSymbol.getName() ? -1 : 1;
    }*/

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

    while (lhsPtr != lhs.end() && rhsPtr != rhs.end()) {
      auto compValue = compareExpressions(lhsPtr->second, rhsPtr->second);
      if (compValue != 0) {
        return compValue < 0;
      }
      lhsPtr++; rhsPtr++;
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
    /*if(auto const* lhsSymbol = std::get_if<Symbol>(&lhs)) {
      auto const& rhsSymbol = std::get<Symbol>(rhs);
      return lhsSymbol->getName() < rhsSymbol.getName() ? -1 : 1;
    }*/

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

  arrow::Status AppendTuple(Tuple const& tuple) {
    auto i = 0U;
    for(auto const& field : tuple) {
      auto parentStatus = Append();
      auto childStatus = appendToChildBuilder(field.second, this->child_builder(i++));
      if(!childStatus.ok()) {
        return childStatus;
      }
      if(!parentStatus.ok()) {
        return parentStatus;
      }
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
              return std::make_shared<arrow::StringDictionaryBuilder>();
            },
            [&](ComplexExpression const& e) -> std::shared_ptr<arrow::ArrayBuilder> {
              std::vector<std::shared_ptr<arrow::ArrayBuilder>> argBuilders;
              argBuilders.reserve(1 + e.getArguments().size());

              std::vector<std::shared_ptr<arrow::Field>> fields;
              fields.reserve(1 + e.getArguments().size());

              // head
              auto headBuilder = std::make_shared<arrow::StringDictionaryBuilder>();
              argBuilders.push_back(std::move(headBuilder));
              fields.push_back(std::make_shared<arrow::Field>("head", nullptr));

              // args
              for(auto const& arg : e.getArguments()) {
                argBuilders.push_back(builderFromExpression(arg));
                fields.push_back(
                    std::make_shared<arrow::Field>("arg" + std::to_string(fields.size()), nullptr));
              }

              return std::make_shared<ComplexExpressionArrayBuilder>(
                  std::make_shared<arrow::StructType>(fields), std::move(argBuilders));
            }),
        expr);
  }

  static arrow::Status
  appendToChildBuilder(Expression const& expr,
                       std::shared_ptr<arrow::ArrayBuilder> const& childBuilder) {
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
              return std::dynamic_pointer_cast<arrow::StringDictionaryBuilder>(childBuilder)
                  ->Append(s.getName());
            },
            [&](ComplexExpression const& e) {
              auto exprBuilder =
                  std::dynamic_pointer_cast<ComplexExpressionArrayBuilder>(childBuilder);

              // append to the args structure
              auto structStatus = exprBuilder->Append();
              if(!structStatus.ok()) {
                return structStatus;
              }

              // append head
              auto headStatus = std::dynamic_pointer_cast<arrow::StringDictionaryBuilder>(
                                    exprBuilder->child_builder(0))
                                    ->Append(e.getHead().getName());
              if(!headStatus.ok()) {
                return headStatus;
              }

              // append each argument
              for(auto idx = 0U; idx < e.getArguments().size(); ++idx) {
                auto argBuilder = exprBuilder->child_builder(1 + idx);
                auto status = appendToChildBuilder(e.getArguments()[idx], argBuilder);
                if(!status.ok()) {
                  return status;
                }
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
    if(!status.ok()) {
      return status;
    }
    return appendToChildBuilder(tuple, childBuilder);
  }

private:
  static arrow::Status appendToChildBuilder(Tuple const& tuple,
                                            std::shared_ptr<ArrayBuilder> const& childBuilder) {
    return std::dynamic_pointer_cast<TypedDatabaseBuilder>(childBuilder)->AppendTuple(tuple);
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

std::shared_ptr<arrow::ArrayBuilder>
new_runtime::RelationBuilder::getOrCreateColumnBuilder(std::string fieldName, Fields const& fields) {
  auto structBuilder = getOrCreateTypedStructBuilder(fields);

  auto structType = structBuilder->type();
  for (auto i = 0; i < structType->num_fields(); i++) {
    if (structType->field(i)->name() == fieldName) {
      return structBuilder->child_builder(i);
    }
  }

  throw std::runtime_error("The field " + fieldName + " didn't exist in the builder!");
}

new_runtime::Relation* new_runtime::RelationBuilder::build() {
  auto result = builder->Finish();
  if (!result.ok()) {
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

std::shared_ptr<arrow::ArrayBuilder> new_runtime::RelationBuilder::getOrCreateTypedStructBuilder(Fields const& fields) {
  std::shared_ptr<arrow::StructBuilder> structBuilder;

  auto childBuilderIndex = fieldsToBuilder.find(fields);
  if (childBuilderIndex == fieldsToBuilder.end()) {
    // We have never seen these fields before. Create a new child builder.
    std::vector<std::shared_ptr<arrow::ArrayBuilder>> argBuilders;
    std::vector<std::shared_ptr<arrow::Field>> arrowFields;

    for(auto const& [fieldName, type] : fields) {
      arrowFields.emplace_back(std::make_shared<arrow::Field>(fieldName, nullptr));
      auto childBuilder = builderForType(type);
      argBuilders.push_back(childBuilder);
    }

    structBuilder = std::make_shared<TypedDatabaseBuilder>(std::make_shared<arrow::StructType>(arrowFields),
                                                           std::move(argBuilders));
    auto newChildIndex = builder->AppendChild(structBuilder);
    fieldsToBuilder[fields] = newChildIndex;
  } else {
    structBuilder = std::dynamic_pointer_cast<arrow::StructBuilder>(builder->child_builder(childBuilderIndex->second));
    if (!structBuilder) {
      throw std::runtime_error("A child builder wasn't a struct builder");
    }
  }
  return structBuilder;
}

extern "C" new_runtime::Relation* constructRelation(new_runtime::RelationBuilder& builder) {
  return builder.build();
}

extern "C" void addToRelation_Int(arrow::ArrayBuilder* builder, int value) {
  auto status = dynamic_cast<arrow::Int32Builder*>(builder)->Append(value);
}

extern "C" void addToRelation_Bool(arrow::ArrayBuilder* builder, bool value) {
  auto status = dynamic_cast<arrow::BooleanBuilder*>(builder)->Append(value);
}

extern "C" void advanceBuilder(arrow::StructBuilder* builder) {
  // TODO error handling
  builder->Append();
}