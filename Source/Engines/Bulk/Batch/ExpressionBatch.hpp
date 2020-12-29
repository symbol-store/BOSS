#pragma once

#include "Batch.hpp"
#include "RLEBatch.hpp"
#include "SymbolBatch.hpp"
#include "ValueBatch.hpp"

#include "../Utils/LambdaInfo.hpp"

#include <memory>
#include <tuple>

namespace boss::engines::bulk {

template <typename EvaluatorType, typename Func, size_t FuncArgCount>
class ExpressionBatch : public Batch {
public:
  using ValueType = ComplexExpression;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<ExpressionBatch>();

  using ArgumentList = std::array<std::unique_ptr<Batch>, FuncArgCount>;

  ExpressionBatch(EvaluatorType const& evaluator, ArgumentList const& batchArgs)
      : m_evaluator(evaluator), m_arguments(batchArgs) {}

  ExpressionBatch(EvaluatorType const& evaluator, ArgumentList&& batchArgs)
      : m_evaluator(evaluator), m_arguments(std::move(batchArgs)) {}

  ExpressionBatch(ExpressionBatch const& other)
      : m_evaluator(other.m_evaluator),
        m_arguments(std::apply(
            [](auto&&... arg) {
              return ArgumentList{(std::unique_ptr<Batch>(arg ? arg.get()->clone() : nullptr))...};
            },
            other.m_arguments)) {}

  Batch* clone() override { return new ExpressionBatch(*this); }

  class Iterator {
    // TODO?
    // would require to build a temporary list of complex expressions,
    // and iterate on them
  };

  size_t size() const override {
    if constexpr(FuncArgCount == 0) {
      return 1;
    } else {
      return std::get<0>(m_arguments).get()->size();
    }
  }

  void insert(Expression const& val) override {
    auto& expression = std::get<ComplexExpression>(val);
    auto argIt = expression.getArguments().begin();

    auto ForEachTupleArgument = [&argIt, &expression](auto& batchPtr) {
      if(argIt != expression.getArguments().end()) {
        auto& argument = *argIt;

        if(!std::holds_alternative<Symbol>(argument) &&
           !std::holds_alternative<ComplexExpression>(argument)) {
          auto& batch = *batchPtr.get();
          if(batch.isRLE() && !batch.canContain(argument)) {
            // there are more than one single value
            // make it a normal value batch
            std::visit(
                [&batchPtr, &batch](auto&& value) {
                  using type = std::decay_t<decltype(value)>;
                  batchPtr = std::unique_ptr<Batch>(new ValueBatch<type>(batch.size(), value));
                },
                argument);
          }
        }

        batchPtr.get()->insert(argument);
        ++argIt;
      }
    };

    auto argsBegin = expression.getArguments().begin();
    auto argsEnd = expression.getArguments().end();

    size_t sizeArgs = std::distance(argsBegin, argsEnd);
    if(sizeArgs > FuncArgCount) {
      if(FuncArgCount == 2 &&
         std::get<0>(m_arguments).get()->elementTypeId() == UniqueId::forType<ComplexExpression>()) {
        // handle compound batch

        size_t numPassingOverArgs = sizeArgs - 1;

        ExpressionArguments compoundList{argsBegin, std::next(argsBegin, numPassingOverArgs)};
        ComplexExpression compoundExpr{expression.getHead(), compoundList};

        ExpressionArguments newList{compoundExpr, *std::next(argsBegin, numPassingOverArgs)};
        ComplexExpression newExpr{expression.getHead(), newList};
        insert(newExpr);
      } else {
        // otherwise just truncate it
        // (should not come here though)
        ExpressionArguments newList{argsBegin, std::next(argsBegin, FuncArgCount)};
        ComplexExpression newExpr{expression.getHead(), newList};
        insert(newExpr);
      }
    } else {
      std::apply([&ForEachTupleArgument](auto&... args) { ((ForEachTupleArgument(args)), ...); },
                 m_arguments);
    }
  }

  UniqueId::type typeId() const override { return UniqueId::forType<ExpressionBatch>(); }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<ComplexExpression>(); }
  UniqueId::type evaluatedTypeId() const override {
    return typeId(); /*deduceReturnTypeId();*/
  }                  // TODO. maybe not needed?

  using RLE = std::bool_constant<false>;
  bool isRLE() const override { return RLE::value; }

  bool canContain(Expression const& val) const override {
    return std::holds_alternative<ComplexExpression>(val);
  }

  Batch* evaluate(BatchFactory const& factory) override {
    auto* evaluated = evaluateHelper(factory);
    if(evaluated) {
      return evaluated;
    } else {
      return clone();
    }
  }

protected:
  EvaluatorType const& m_evaluator;
  ArgumentList m_arguments;

  // helpers to retrieve return type for a specific set of Batch argument types
  template <typename T> using UniquePtrToElementType = typename T::element_type::ValueType;
  template <typename... BatchTupleTypes>
  using ReturnType =
      typename LambdaInfo<Func, UniquePtrToElementType<BatchTupleTypes>...>::ReturnType;

  // calls the evaluator with specific Batch types as arguments (not just generic Batch)
  template <typename OutputBatchType, typename InputBatchTuple, size_t... Indices>
  void evaluateImpl(OutputBatchType& out, InputBatchTuple const& in,
                    std::index_sequence<Indices...>) const {
    m_evaluator(out, (*std::get<Indices>(in).get())...);
  }

  // build a tuple of specific Batch argument types
  // from dynamic information extracted from generic Batch list
  template <size_t Index = 0, typename... BatchTupleTypes>
  Batch*
  evaluateHelper(BatchFactory const& factory,
                 std::tuple<BatchTupleTypes...>&& batchTuple = std::tuple<BatchTupleTypes...>(),
                 bool isRLE = true) const {
    using BatchTuple = std::tuple<BatchTupleTypes...>;
    if constexpr(Index == FuncArgCount) {
      if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
        return nullptr;
      } else if constexpr(!EvaluatorType::template AreAllowedTypes<
                              UniquePtrToElementType<BatchTupleTypes>...>::value) {
        return nullptr;
      } else {
        using ReturnType = ReturnType<BatchTupleTypes...>;
        if constexpr(std::is_same_v<ReturnType, Symbol>) {
          auto* outputBatch = new SymbolBatch();
          evaluateImpl(*outputBatch, batchTuple, std::make_index_sequence<FuncArgCount>{});
          return outputBatch;
        } else if(isRLE) {
          auto* outputBatch = new RLEBatch<ReturnType>(size(), ReturnType());
          evaluateImpl(*outputBatch, batchTuple, std::make_index_sequence<FuncArgCount>{});
          return outputBatch;
        } else {
          auto* outputBatch = new ValueBatch<ReturnType>(size(), ReturnType());
          evaluateImpl(*outputBatch, batchTuple, std::make_index_sequence<FuncArgCount>{});
          return outputBatch;
        }
      }
    } else {
      auto& batchPtr = std::get<Index>(m_arguments);
      auto& evaluatedBatch = *batchPtr.get()->evaluate(factory);

      // TODO: templatise it with allowed types
      if(evaluatedBatch.elementTypeId() == UniqueId::forType<bool>()) {
        if constexpr(!EvaluatorType::template isAllowedType<bool>()) {
          return nullptr;
        } else if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
          if(evaluatedBatch.isRLE()) {
            return evaluateHelper<Index + 1>(factory,
                                             std::make_tuple(std::unique_ptr<RLEBatch<bool>>(
                                                 static_cast<RLEBatch<bool>*>(&evaluatedBatch))),
                                             isRLE);
          } else {
            return evaluateHelper<Index + 1>(factory,
                                             std::make_tuple(std::unique_ptr<ValueBatch<bool>>(
                                                 static_cast<ValueBatch<bool>*>(&evaluatedBatch))),
                                             false);
          }
        } else {
          return std::apply(
              [&, this](auto&&... arg) {
                if(evaluatedBatch.isRLE()) {
                  return evaluateHelper<Index + 1>(
                      factory,
                      std::make_tuple((std::move(arg))...,
                                      std::unique_ptr<RLEBatch<bool>>(
                                          static_cast<RLEBatch<bool>*>(&evaluatedBatch))),
                      isRLE);
                } else {
                  return evaluateHelper<Index + 1>(
                      factory,
                      std::make_tuple((std::move(arg))...,
                                      std::unique_ptr<ValueBatch<bool>>(
                                          static_cast<ValueBatch<bool>*>(&evaluatedBatch))),
                      false);
                }
              },
              batchTuple);
        }
      } else if(evaluatedBatch.elementTypeId() == UniqueId::forType<int>()) {
        if constexpr(!EvaluatorType::template isAllowedType<int>()) {
          return nullptr;
        } else if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
          if(evaluatedBatch.isRLE()) {
            return evaluateHelper<Index + 1>(factory,
                                             std::make_tuple(std::unique_ptr<RLEBatch<int>>(
                                                 static_cast<RLEBatch<int>*>(&evaluatedBatch))),
                                             isRLE);
          } else {
            return evaluateHelper<Index + 1>(factory,
                                             std::make_tuple(std::unique_ptr<ValueBatch<int>>(
                                                 static_cast<ValueBatch<int>*>(&evaluatedBatch))),
                                             false);
          }
        } else {
          return std::apply(
              [&, this](auto&&... arg) {
                if(evaluatedBatch.isRLE()) {
                  return evaluateHelper<Index + 1>(
                      factory,
                      std::make_tuple((std::move(arg))...,
                                      std::unique_ptr<RLEBatch<int>>(
                                          static_cast<RLEBatch<int>*>(&evaluatedBatch))),
                      isRLE);
                } else {
                  return evaluateHelper<Index + 1>(
                      factory,
                      std::make_tuple((std::move(arg))...,
                                      std::unique_ptr<ValueBatch<int>>(
                                          static_cast<ValueBatch<int>*>(&evaluatedBatch))),
                      false);
                }
              },
              batchTuple);
        }
      } else if(evaluatedBatch.elementTypeId() == UniqueId::forType<float>()) {
        if constexpr(!EvaluatorType::template isAllowedType<float>()) {
          return nullptr;
        } else if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
          if(evaluatedBatch.isRLE()) {
            return evaluateHelper<Index + 1>(factory,
                                             std::make_tuple(std::unique_ptr<RLEBatch<float>>(
                                                 static_cast<RLEBatch<float>*>(&evaluatedBatch))),
                                             isRLE);
          } else {
            return evaluateHelper<Index + 1>(factory,
                                             std::make_tuple(std::unique_ptr<ValueBatch<float>>(
                                                 static_cast<ValueBatch<float>*>(&evaluatedBatch))),
                                             false);
          }
        } else {
          return std::apply(
              [&, this](auto&&... arg) {
                if(evaluatedBatch.isRLE()) {
                  return evaluateHelper<Index + 1>(
                      factory,
                      std::make_tuple((std::move(arg))...,
                                      std::unique_ptr<RLEBatch<float>>(
                                          static_cast<RLEBatch<float>*>(&evaluatedBatch))),
                      isRLE);
                } else {
                  return evaluateHelper<Index + 1>(
                      factory,
                      std::make_tuple((std::move(arg))...,
                                      std::unique_ptr<ValueBatch<float>>(
                                          static_cast<ValueBatch<float>*>(&evaluatedBatch))),
                      false);
                }
              },
              batchTuple);
        }
      } else if(evaluatedBatch.elementTypeId() == UniqueId::forType<std::string>()) {
        if constexpr(!EvaluatorType::template isAllowedType<std::string>()) {
          return nullptr;
        } else if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
          if(evaluatedBatch.isRLE()) {
            return evaluateHelper<Index + 1>(
                factory,
                std::make_tuple(std::unique_ptr<RLEBatch<std::string>>(
                    static_cast<RLEBatch<std::string>*>(&evaluatedBatch))),
                isRLE);
          } else {
            return evaluateHelper<Index + 1>(
                factory,
                std::make_tuple(std::unique_ptr<ValueBatch<std::string>>(
                    static_cast<ValueBatch<std::string>*>(&evaluatedBatch))),
                false);
          }
        } else {
          return std::apply(
              [&, this](auto&&... arg) {
                if(evaluatedBatch.isRLE()) {
                  return evaluateHelper<Index + 1>(
                      factory,
                      std::make_tuple((std::move(arg))...,
                                      std::unique_ptr<RLEBatch<std::string>>(
                                          static_cast<RLEBatch<std::string>*>(&evaluatedBatch))),
                      isRLE);
                } else {
                  return evaluateHelper<Index + 1>(
                      factory,
                      std::make_tuple((std::move(arg))...,
                                      std::unique_ptr<ValueBatch<std::string>>(
                                          static_cast<ValueBatch<std::string>*>(&evaluatedBatch))),
                      false);
                }
              },
              batchTuple);
        }
      } else if(evaluatedBatch.elementTypeId() == UniqueId::forType<Symbol>()) {
        if constexpr(!EvaluatorType::template isAllowedType<Symbol>()) {
          return nullptr;
        } else if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
          return evaluateHelper<Index + 1>(factory,
                                           std::make_tuple(std::unique_ptr<SymbolBatch>(
                                               static_cast<SymbolBatch*>(&evaluatedBatch))),
                                           isRLE);
        } else {
          return std::apply(
              [&, this](auto&&... arg) {
                return evaluateHelper<Index + 1>(
                    factory,
                    std::make_tuple(
                        (std::move(arg))...,
                        std::unique_ptr<SymbolBatch>(static_cast<SymbolBatch*>(&evaluatedBatch))),
                    isRLE);
              },
              batchTuple);
        }
      } else if(evaluatedBatch.elementTypeId() == UniqueId::forType<ComplexExpression>()) {
        // TODO: if really needed to handle complex expressions as argument
        // it should be a less generic type than Batch
        // it won't even compile...
        if constexpr(!EvaluatorType::template isAllowedType<ComplexExpression>()) {
          return nullptr;
        } else if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
          return evaluateHelper<Index + 1>(
              factory, std::make_tuple(std::unique_ptr<Batch>(&evaluatedBatch)), isRLE);
        } else {
          return std::apply(
              [&, this](auto&&... arg) {
                return evaluateHelper<Index + 1>(
                    factory,
                    std::make_tuple((std::move(arg))..., std::unique_ptr<Batch>(&evaluatedBatch)),
                    isRLE);
              },
              batchTuple);
        }
      } else {
        // unhandled batch type?
        return nullptr;
      }
    }
  }
};

} // namespace boss::engines::bulk
