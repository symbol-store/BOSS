#pragma once

#include "../../Expression.hpp"

#include <memory>

namespace boss::engines::bulk {

class CompoundArray;
template <typename T> class ValueArray;

using BulkExpressionSystem =
    ExtensibleExpressionSystem<std::shared_ptr<CompoundArray>, std::shared_ptr<ValueArray<bool>>,
                               std::shared_ptr<ValueArray<int>>, std::shared_ptr<ValueArray<float>>,
                               std::shared_ptr<ValueArray<std::string>>,
                               std::shared_ptr<ValueArray<Symbol>>>;
using BulkAtomicExpression = BulkExpressionSystem::AtomicExpression;
using BulkComplexExpression = BulkExpressionSystem::ComplexExpression;
using BulkExpression = BulkExpressionSystem::Expression;
using BulkExpressionArguments = BulkExpressionSystem::ExpressionArguments;
} // namespace boss::engines::bulk