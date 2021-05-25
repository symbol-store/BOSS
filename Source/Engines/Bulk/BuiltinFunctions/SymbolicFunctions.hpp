#pragma once

#include "../OperatorUtils.hpp"

#include "../../../Expression.hpp"

namespace boss::engines::bulk {

template <typename BatchPrototypes> class SymbolicFunctions {
  using Utils = OperatorUtils<BatchPrototypes>;
  using AnyBatch = typename BatchPrototypes::AnyBatch;
  using AnySimpleBatch = typename BatchPrototypes::AnySimpleBatch;

public:
  static void registerAll(BatchPrototypes& prototypes) {
    prototypes.template allowedTypes<std::string>().template registerFunction<1>(
        "Symbol", [](auto&& batchPtr) {
          return Utils::evaluateElements([](auto const& name) -> Symbol { return Symbol(name); },
                                         batchPtr);
        });

    prototypes.template argBatchTypes<AnySimpleBatch>().template registerFunction<1>(
        "Function", [](auto bodyBatchPtr) {
          Batch::ReadablePtr bodyPtr(std::move(bodyBatchPtr));
          return Batch::WritablePtr(
              new FunctionBatch(FunctionBatch::ParameterList{}, std::move(bodyPtr)));
        });

    prototypes.template argBatchTypes<AllowedBatches<CompoundBatch, SymbolBatch>, AnyBatch>()
        .template registerFunction<2>("Function", [](auto&& argBatchPtr, auto bodyBatchPtr) {
          using ArgsBatchPtrType = std::decay_t<decltype(argBatchPtr)>;
          using ArgsBatchType = typename ArgsBatchPtrType::BatchType;
          FunctionBatch::ParameterList args;
          if constexpr(std::is_base_of_v<CompoundBatch, ArgsBatchType>) {
            args.reserve(argBatchPtr->size());
            for(auto const& symbolBatchPtr : *argBatchPtr) {
              if(symbolBatchPtr->typeId() == UniqueId::forType<SymbolBatch>()) {
                args.emplace_back(
                    (*static_cast<SymbolBatch const*>(symbolBatchPtr.get())->begin()));
              }
            }
          } else {
            args.emplace_back((*argBatchPtr->begin()));
          }
          Batch::ReadablePtr bodyPtr(std::move(bodyBatchPtr));
          return Batch::WritablePtr(new FunctionBatch(args, std::move(bodyPtr)));
        });
  }
};

} // namespace boss::engines::bulk
