#include "Storage.hpp"

#include "Utilities.hpp"
#include <map>

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

class ExpressionArrayBuilder : public arrow::DenseUnionBuilder {
public:
  ExpressionArrayBuilder() : arrow::DenseUnionBuilder(arrow::default_memory_pool()) {}

  explicit ExpressionArrayBuilder(arrow::MemoryPool* pool)
      : arrow::DenseUnionBuilder(pool) {}

  arrow::Status AppendExpression(Expression const& expr) {
    auto it = m_expressionToArray.find(expr);
    std::shared_ptr<ArrayBuilder> childBuilder;
    bool foundInCache(it != m_expressionToArray.end());
    auto& cachedTypeId = foundInCache ? it->second : m_expressionToArray[expr];
    if(foundInCache) {
      // retrieve from the cache
      childBuilder = child_builder(cachedTypeId);
    } else {
      // This type of expression is not supported yet
      // Append a new array for this expression
      // and cache the id for next time
      childBuilder = makeChildBuilder(expr);
      cachedTypeId = AppendChild(childBuilder);
    }
    auto status = Append(cachedTypeId);
    if(!status.ok()) {
      return status;
    }
    return appendToChildBuilder(expr, childBuilder);
  }

private:
  static arrow::Status appendToChildBuilder(Expression const& expr,
                                            std::shared_ptr<arrow::ArrayBuilder>& childBuilder) {
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
              for(int idx = 0; idx < e.getArguments().size(); ++idx) {
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

  static std::shared_ptr<arrow::ArrayBuilder> makeChildBuilder(Expression const& expr) {
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
                argBuilders.push_back(makeChildBuilder(arg));
                fields.push_back(
                    std::make_shared<arrow::Field>("arg" + std::to_string(fields.size()), nullptr));
              }

              return std::make_shared<ComplexExpressionArrayBuilder>(
                  std::make_shared<arrow::StructType>(fields), std::move(argBuilders));
            }),
        expr);
  }

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

  std::map<Expression, int8_t, CompareExpression> m_expressionToArray;
};

void appendExpression(std::shared_ptr<ExpressionArrayBuilder>& builder, Expression const& expr) {
  if(!builder) {
    builder = std::make_shared<ExpressionArrayBuilder>();
  }
  auto status = builder->AppendExpression(expr);
  if(!status.ok()) {
    throw std::runtime_error(status.message());
  }
}

void new_runtime::Relation::bulk_load(std::vector<std::map<std::string, boss::Expression>> tuples) {

  std::map<std::string, ExpressionArrayBuilder> columnToBuilder;

  for(auto const& tuple : tuples) {
    for(auto const& [column, expression] : tuple) {
//      if (columnToBuilder.find(column) == columnToBuilder.end()) {
//        columnToBuilder[column] = ExpressionArrayBuilder()
//      }

      auto status = columnToBuilder[column].AppendExpression(expression);
      if(!status.ok()) {
        throw std::runtime_error(status.message());
      }
    }
  }

  // Generate schema and arrays from builder
  std::vector<std::shared_ptr<arrow::Field>> fields;
  std::vector<std::shared_ptr<arrow::Array>> arrays;
  for(auto& builder : columnToBuilder) {
    fields.emplace_back(
        std::make_shared<arrow::Field>(std::string(builder.first), builder.second.type()));
    auto result = builder.second.Finish();
    if(!result.ok()) {
      throw std::runtime_error(result.status().message());
    }
    arrays.push_back(result.ValueUnsafe());
  }
  auto schema = std::make_shared<arrow::Schema>(fields);

  // Create table
  data = arrow::Table::Make(schema, arrays);
}