#pragma once

#include "../BulkExpression.hpp"
#include "../Operator.hpp"

namespace boss::engines::bulk {

template <typename OperatorUtils, typename OperatorRegistry> class Imputation {
  using TableArgument = typename OperatorUtils::TableArgument;
  using ListArgument = typename OperatorUtils::ListArgument;

  struct Offset {
    template <int defaultValueParameter = 0> class lastKnown {
    public:
      static int const offsetValue = -1;
      static int const defaultValue = defaultValueParameter;
    };
    template <int defaultValueParameter = 0> class nextKnown {
    public:
      static int const offsetValue = +1;
      static int const defaultValue = defaultValueParameter;
    };
  };

public:
  static void registerAll() {
    auto& operatorRegistry = OperatorRegistry::instance();
    operatorRegistry
        .template registerOperator<WindowOperator<typename Offset::template lastKnown<>, int>>(
            "LastKnownInt");
    operatorRegistry
        .template registerOperator<WindowOperator<typename Offset::template nextKnown<>, int>>(
            "NextKnownInt");
    operatorRegistry
        .template registerOperator<WindowOperator<typename Offset::template lastKnown<>, float>>(
            "LastKnownFloat");
    operatorRegistry
        .template registerOperator<WindowOperator<typename Offset::template nextKnown<>, float>>(
            "NextKnownFloat");
  }

private:
  template <typename OffsetType, typename T> class WindowOperator : public Operator<> {
  public:
    BulkExpression evaluate() const {
      auto const* contextArray = getContextArray();
      if(contextArray == nullptr) {
        return false;
      }

      // get the position and size of the array we arae currently evaluating
      auto bufferSize = contextArray->numRows();
      auto globalOffset = contextArray->getGlobalRowIndex();

      // get the column array from which we want to copy values
      auto const* tableArray = contextArray->getGlobalArray();
      auto columnIndex = contextArray->getGlobalColumnIndex();
      // we use for that a special iterator which works on heterogeneous values
      auto srcColumnIt = tableArray->template intraColumnBegin<T>(columnIndex);
      auto srcColumnItEnd = tableArray->template intraColumnEnd<T>(columnIndex);
      auto globalNumRows = tableArray->numRows();

      auto bufferArrayPtr = std::make_shared<ValueArray<T>>(bufferSize);
      auto bufferArrayIt = bufferArrayPtr->begin();
      auto bufferArrayItEnd = bufferArrayPtr->end();
      for(; bufferArrayIt != bufferArrayItEnd; ++bufferArrayIt, ++globalOffset) {
        if constexpr(OffsetType::offsetValue >= 0) {
          size_t offset = globalOffset + OffsetType::offsetValue;
          auto candidateValueIt = offset < globalNumRows ? srcColumnIt + offset : srcColumnIt;
          if(!candidateValueIt.isMatchingType()) {
            candidateValueIt.incrementUntilMatchingType();
          }
          if(candidateValueIt != srcColumnItEnd) {
            *bufferArrayIt = *candidateValueIt;
            continue;
          }
        } else {
          int offset = (int)globalOffset + OffsetType::offsetValue;
          auto candidateValueIt = offset >= 0 ? srcColumnIt + (size_t)offset : srcColumnIt;
          if(!candidateValueIt.isMatchingType()) {
            candidateValueIt.decrementUntilMatchingType();
          }
          if(candidateValueIt != srcColumnItEnd) {
            *bufferArrayIt = *candidateValueIt;
            continue;
          }
        }
        *bufferArrayIt = OffsetType::defaultValue;
      }
      return bufferArrayPtr;
    }
  };
};

} // namespace boss::engines::bulk
