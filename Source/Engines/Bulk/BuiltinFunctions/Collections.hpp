#pragma once

#include "../Batch/ValueBatch.hpp"
#include "../BatchVisitDispatcher.hpp"
#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class Collections {
  using AnyTypeArgument = typename OperatorUtils::AnyTypeArgument;
  using AnySimpleTypeArgument = typename OperatorUtils::AnySimpleTypeArgument;
  using AnyCollectionArgument = typename OperatorUtils::AnyCollectionArgument;
  using AnySimpleTypeCollectionArgument = typename OperatorUtils::AnySimpleTypeCollectionArgument;

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry.template registerOperator<CountOperator>("Count");
    operatorRegistry.template registerOperator<CountOperator>("Length");
    operatorRegistry.template registerOperator<ExtractOperator>("Extract");
    operatorRegistry.template registerOperator<FirstOperator>("First");
    operatorRegistry.template registerOperator<LastOperator>("Last");
    operatorRegistry.template registerOperator<ColumnOperator>("Column");
    operatorRegistry.template registerOperator<IndexOfOperator>("IndexOf");
  }

private:
  class CountOperator : public Operator<1, AnyCollectionArgument> {
  public:
    template <typename ArrayType>
    BulkExpression evaluate(std::shared_ptr<ArrayType> const& arrayPtr) const {
      return static_cast<int>(arrayPtr->length());
    }

    BulkExpression evaluate(BulkComplexExpression const& list) const {
      return static_cast<int>(list.getArguments().size());
    }
  };

  class ExtractOperator : public Operator<2, AnyCollectionArgument, AllowedArguments<int>> {
  public:
    template <typename ArrayPtrType>
    BulkExpression evaluate(ArrayPtrType const& arrayPtr, int index) const {
      using ArrayType = typename ArrayPtrType::element_type;
      if constexpr(std::is_same_v<ArrayType, CompoundArray>) {
        return arrayPtr->extract(index - 1);
      } else {
        using ValueType = typename ArrayType::ValueType;
        return (ValueType)(*(arrayPtr->begin() + (index - 1)));
      }
    }

    BulkExpression evaluate(BulkComplexExpression const& list, int index) const {
      return list.getArguments()[index - 1];
    }
  };

  class FirstOperator : public Operator<1, AnyCollectionArgument> {
  public:
    template <typename ArrayPtrType> BulkExpression evaluate(ArrayPtrType const& arrayPtr) const {
      using ArrayType = typename ArrayPtrType::element_type;
      if constexpr(std::is_same_v<ArrayType, CompoundArray>) {
        return arrayPtr->extract(0);
      } else {
        using ValueType = typename ArrayType::ValueType;
        return (ValueType)(*(arrayPtr->begin()));
      }
    }

    BulkExpression evaluate(BulkComplexExpression const& list) const {
      return list.getArguments().front();
    }
  };

  class LastOperator : public Operator<1, AnyCollectionArgument> {
  public:
    template <typename ArrayPtrType> BulkExpression evaluate(ArrayPtrType const& arrayPtr) const {
      using ArrayType = typename ArrayPtrType::element_type;
      size_t index = arrayPtr->length() - 1;
      if constexpr(std::is_same_v<ArrayType, CompoundArray>) {
        return arrayPtr->extract(index);
      } else {
        using ValueType = typename ArrayType::ValueType;
        return (ValueType)(*(arrayPtr->begin() + index));
      }
    }

    BulkExpression evaluate(BulkComplexExpression const& list) const {
      return list.getArguments().back();
    }
  };

  class ColumnOperator : public Operator<2, AllowedArguments<std::shared_ptr<CompoundArray>>,
                                         AllowedArguments<int>> {
  public:
    BulkExpression evaluate(std::shared_ptr<CompoundArray> const& arrayPtr, int index) const {
      return arrayPtr->column(index - 1);
    }
  };

  class IndexOfOperator
      : public Operator<2, AnySimpleTypeCollectionArgument, AnySimpleTypeArgument> {
  public:
    template <typename ArrayPtrType, typename ValueType>
    auto evaluate(ArrayPtrType const& arrayPtr, ValueType const& value) const {
      using ArrayType = typename ArrayPtrType::element_type;
      using ArrayValueType = typename ArrayType::ValueType;
      if constexpr(std::is_convertible_v<ValueType, ArrayValueType>) {
        int index = 1;
        for(auto const& arrayValue : *arrayPtr) {
          if constexpr(std::is_same_v<Symbol, ValueType>) {
            if(value.getName() == ((ArrayValueType)arrayValue).getName()) {
              return index;
            }
          } else {
            if((ArrayValueType)arrayValue == value) {
              return index;
            }
          }
          ++index;
        }
      }
      // is there anything better to return?
      return 0;
    }

    template <typename ValueType>
    BulkExpression evaluate(BulkComplexExpression const& list, ValueType const& value) const {
      int index = 1;
      for(auto const& arg : list.getArguments()) {
        bool found = std::visit(
            [&value](auto const& typedArg) {
              using ArgValueType = std::decay_t<decltype(typedArg)>;
              if constexpr(std::is_convertible_v<ValueType, ArgValueType>) {
                if constexpr(std::is_same_v<Symbol, ValueType>) {
                  return typedArg.getName() == value.getName();
                } else {
                  return typedArg == value;
                }
              }
              return false;
            },
            arg);
        if(found) {
          return index;
        }
        ++index;
      }
      // is there anything better to return?
      return 0;
    }
  };
};

} // namespace boss::engines::bulk
