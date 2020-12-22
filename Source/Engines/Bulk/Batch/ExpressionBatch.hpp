#pragma once

#include "Batch.hpp"
#include "RLEBatch.hpp"
#include "SymbolBatch.hpp"
#include "ValueBatch.hpp"

#include <memory>
#include <tuple>

namespace boss::engines::bulk {

template <typename F, typename... Args>
struct lambda_details : lambda_details<decltype(&F::template operator()<Args...>)> {};

template <typename F, typename R, typename... Args> struct lambda_details<R (F::*)(Args...) const> {
  using argument_count = std::integral_constant<size_t, sizeof...(Args)>;
  using return_type = R;
};

template <typename EvaluatorType, typename Func, size_t N>
class ExpressionBatchBase : public Batch {
public:
  static bool isAllowedType(UniqueId::type typeId) {
    return isAllowedType(typeId, typename EvaluatorType::types{});
  }

  template <typename Type> static constexpr bool isAllowedType() {
    return isAllowedType<Type>(typename EvaluatorType::types{});
  }

  template <template <typename...> typename TLIST, typename... TYPES>
  static bool isAllowedType(UniqueId::type typeId, TLIST<TYPES...>) {
    return ((typeId == UniqueId::forType<TYPES>()) || ...);
  }

  template <typename Type, template <typename...> typename TLIST, typename... TYPES>
  static constexpr bool isAllowedType(TLIST<TYPES...>) {
    return ((std::is_same_v<Type, TYPES>) || ...);
  }

  template <typename... Ts> struct AreAllowedTypes {
    static constexpr bool value = (isAllowedType<Ts>() && ...);
  };

  ExpressionBatchBase(Func&& func) : m_evaluator(func) {}
  ExpressionBatchBase(ExpressionBatchBase const& other) : m_evaluator(other.m_evaluator) {}

  ~ExpressionBatchBase() {}

  Batch* clone() override { return new ExpressionBatchBase(*this); }
  size_t size() const override { return 0; }
  void insert(Expression::ArgumentType const& val) override {}
  UniqueId::type typeId() const override { return UniqueId::forType<ExpressionBatchBase>(); }
  UniqueId::type evaluatedTypeId() const override { return typeId(); }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<Expression>(); }
  bool isRLE() const override { return false; }
  bool canContain(Expression::ArgumentType const& val) const override { return false; }
  Batch* evaluate(BatchFactory const&) override { return this; }

protected:
  EvaluatorType m_evaluator;
};

template <typename EvaluatorType, typename Func, size_t N = lambda_details<Func>::argument_count,
          typename... BatchArgs> // Use evalutor type instead?
class ExpressionBatch : public ExpressionBatchBase<EvaluatorType, Func, N> {
public:
  using ValueType = Expression;
  static constexpr UniqueId::type UniqueId = UniqueId::forType<ExpressionBatch>();

  using ArgumentTuple = std::tuple<std::unique_ptr<BatchArgs>...>;
  using ExpressionBatchBase = ExpressionBatchBase<EvaluatorType, Func, N>;

  ExpressionBatch(ExpressionBatch const& other)
      : ExpressionBatchBase(other),
        m_arguments(std::apply(
            [](auto&&... arg) {
              return std::make_tuple(
                  (std::unique_ptr<typename std::remove_reference_t<decltype(arg)>::element_type>(
                      static_cast<typename std::remove_reference_t<decltype(arg)>::element_type*>(
                          arg.get()->clone())))...);
            },
            other.m_arguments)) {}

  ExpressionBatch(ExpressionBatchBase const& other, ArgumentTuple const& batchArgs)
      : ExpressionBatchBase(other), m_arguments(batchArgs) {}
  ExpressionBatch(ExpressionBatchBase const& other, ArgumentTuple&& batchArgs)
      : ExpressionBatchBase(other), m_arguments(std::move(batchArgs)) {}

  ~ExpressionBatch() {}

  Batch* clone() override { return new ExpressionBatch(*this); }

  class Iterator {
    // TODO?
    // would require to build a temporary list of expression,
    // and iterate on them
  };

  size_t size() const override {
    if constexpr(ArgumentCount::value == 0) {
      return 1;
    } else {
      return std::get<0>(m_arguments).get()->size();
    }
  }

  void insert(Expression::ArgumentType const& val) override {
    auto& expression = std::get<Expression>(val);
    auto argIt = expression.getArguments().begin();

    auto ForEachTupleArgument = [&argIt, &expression](auto&& arg) {
      if(argIt != expression.getArguments().end()) {
        auto& batch = *arg.get();
        batch.insert(*argIt);
        ++argIt;
      }
    };

    auto argsBegin = expression.getArguments().begin();
    auto argsEnd = expression.getArguments().end();

    size_t sizeArgs = std::distance(argsBegin, argsEnd);
    if(sizeArgs > ArgumentCount::value) {
      if(ArgumentCount::value == 2 &&
         std::get<0>(m_arguments).get()->elementTypeId() == UniqueId::forType<Expression>()) {
        // handle compound batch

        size_t numPassingOverArgs = sizeArgs - 1;

        Expression::ArgumentList compoundList{argsBegin, std::next(argsBegin, numPassingOverArgs)};
        Expression compoundExpr{expression.getHead(), compoundList};

        Expression::ArgumentList newList{compoundExpr, *std::next(argsBegin, numPassingOverArgs)};
        Expression newExpr{expression.getHead(), newList};
        insert(newExpr);
      } else {
        // otherwise just truncate it
        // (should not come here though)
        Expression::ArgumentList newList{argsBegin, std::next(argsBegin, ArgumentCount::value)};
        Expression newExpr{expression.getHead(), newList};
        insert(newExpr);
      }
    } else {
      std::apply([&ForEachTupleArgument](auto&&... args) { ((ForEachTupleArgument(args)), ...); },
                 m_arguments);
    }
  }

  UniqueId::type typeId() const override { return UniqueId::forType<ExpressionBatch>(); }
  UniqueId::type elementTypeId() const override { return UniqueId::forType<Expression>(); }
  UniqueId::type evaluatedTypeId() const override {
    return typeId(); /*deduceReturnTypeId();*/
  }                  // TODO. maybe not needed?

  using RLE = std::bool_constant<false>;
  bool isRLE() const override { return RLE::value; }

  bool canContain(Expression::ArgumentType const& val) const override {
    return std::holds_alternative<Expression>(val);
  }

  template <typename TupleOfBatches> struct checkIsRLE;

  template <typename... BatchType>
  struct checkIsRLE<std::tuple<std::unique_ptr<BatchType>...>>
      : std::conjunction<typename BatchType::RLE::type...> {};

  Batch* evaluate(BatchFactory const& factory) override {
    std::vector<Batch*> evaluatedArgs;
    evaluatedArgs.reserve(N);

    auto ForEachArgument = [this, &factory, &evaluatedArgs](auto const& unevaluated) {
      auto* evaluatedBatch = unevaluated.get()->evaluate(factory);

      // check if the evaluated type is allowed in the function
      if(!ExpressionBatchBase::isAllowedType(evaluatedBatch->elementTypeId())) {
        // not ready to evaluate yet
        return false;
      }

      evaluatedArgs.push_back(evaluatedBatch);
      return true;
    };

    bool readyToEvaluate = std::apply(
        [&, this](auto const&... arg) { return ((ForEachArgument(arg)) && ...); }, m_arguments);

    if(readyToEvaluate) {
      auto* evaluated = evaluateHelper(evaluatedArgs.begin(), evaluatedArgs.end(), factory);
      if(evaluated) {
        return evaluated;
      }
    } else {
      // need to manually delete the args
      // since they haven't been taken care of by the evaluateHelper
      // TODO: should just pass unique_ptr directly to evaluateHelper
      for(auto* batch : evaluatedArgs) {
        delete batch;
      }
    }

    return this->clone();
  }

private:
  using ArgumentCount = std::tuple_size<ArgumentTuple>;
  using ArgumentIndexSequence = std::make_index_sequence<ArgumentCount::value>;

  ArgumentTuple m_arguments;

  template <typename OutputBatchType, typename InputBatchTuple, size_t... Indices>
  void evaluateImpl(OutputBatchType& out, InputBatchTuple const& in,
                    std::index_sequence<Indices...>) const {
    this->m_evaluator(out, (*std::get<Indices>(in).get())...);
  }

  template <typename T> using UniquePtrToElementType = typename T::element_type::ValueType;

  template <size_t Index = 0, typename BatchPos, typename End, typename... BatchTupleTypes>
  Batch*
  evaluateHelper(BatchPos batchPos, End end, BatchFactory const& factory,
                 std::tuple<BatchTupleTypes...>&& batchTuple = std::tuple<BatchTupleTypes...>(),
                 bool isRLE = true) const {
    using BatchTuple = std::tuple<BatchTupleTypes...>;
    if constexpr(Index == N) {
      if constexpr(std::tuple_size_v<BatchTuple> != N) {
        return nullptr;
      } else if constexpr(!ExpressionBatchBase::template AreAllowedTypes<
                              UniquePtrToElementType<BatchTupleTypes>...>::value) {
        return nullptr;
      } else {
        using ReturnType =
            typename lambda_details<Func, UniquePtrToElementType<BatchTupleTypes>...>::return_type;
        if constexpr(std::is_same_v<ReturnType, Expression::Symbol>) {
          auto* outputBatch = new SymbolBatch();
          evaluateImpl(*outputBatch, batchTuple, ArgumentIndexSequence{});
          return outputBatch;
        } else if(isRLE) {
          auto* outputBatch = new RLEBatch<ReturnType>(size(), ReturnType());
          evaluateImpl(*outputBatch, batchTuple, ArgumentIndexSequence{});
          return outputBatch;
        } else {
          auto* outputBatch = new ValueBatch<ReturnType>(size(), ReturnType());
          evaluateImpl(*outputBatch, batchTuple, ArgumentIndexSequence{});
          return outputBatch;
        }
      }
    } else if constexpr(Index != std::tuple_size_v<BatchTuple>) {
      // some arguments failed
      // just clean things up properly and return
      delete *batchPos;
      return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
    } else {
      auto* batchPtr = *batchPos;
      isRLE = isRLE && batchPtr->isRLE();

      if(batchPtr->typeId() == UniqueId::forType<SymbolBatch>()) {
        if constexpr(ExpressionBatchBase::template isAllowedType<Expression::Symbol>()) {
          auto uniquePtr = std::unique_ptr<SymbolBatch>(static_cast<SymbolBatch*>(batchPtr));
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      } else if(batchPtr->typeId() == UniqueId::forType<ValueBatch<bool>>()) {
        if constexpr(ExpressionBatchBase::template isAllowedType<bool>()) {
          auto uniquePtr =
              std::unique_ptr<ValueBatch<bool>>(static_cast<ValueBatch<bool>*>(batchPtr));
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      } else if(batchPtr->typeId() == UniqueId::forType<RLEBatch<bool>>()) {
        if constexpr(ExpressionBatchBase::template isAllowedType<bool>()) {
          auto uniquePtr = std::unique_ptr<RLEBatch<bool>>(static_cast<RLEBatch<bool>*>(batchPtr));
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      } else if(batchPtr->typeId() == UniqueId::forType<ValueBatch<int>>()) {
        if constexpr(ExpressionBatchBase::template isAllowedType<int>()) {
          auto uniquePtr =
              std::unique_ptr<ValueBatch<int>>(static_cast<ValueBatch<int>*>(batchPtr));
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      } else if(batchPtr->typeId() == UniqueId::forType<RLEBatch<int>>()) {
        if constexpr(ExpressionBatchBase::template isAllowedType<int>()) {
          auto uniquePtr = std::unique_ptr<RLEBatch<int>>(static_cast<RLEBatch<int>*>(batchPtr));
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      } else if(batchPtr->typeId() == UniqueId::forType<ValueBatch<float>>()) {
        if constexpr(ExpressionBatchBase::template isAllowedType<float>()) {
          auto uniquePtr =
              std::unique_ptr<ValueBatch<float>>(static_cast<ValueBatch<float>*>(batchPtr));
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      } else if(batchPtr->typeId() == UniqueId::forType<RLEBatch<float>>()) {
        if constexpr(ExpressionBatchBase::template isAllowedType<float>()) {
          auto uniquePtr =
              std::unique_ptr<RLEBatch<float>>(static_cast<RLEBatch<float>*>(batchPtr));
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      } else if(batchPtr->typeId() == UniqueId::forType<ValueBatch<std::string>>()) {
        if constexpr(ExpressionBatchBase::template isAllowedType<std::string>()) {
          auto uniquePtr = std::unique_ptr<ValueBatch<std::string>>(
              static_cast<ValueBatch<std::string>*>(batchPtr));
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      } else if(batchPtr->typeId() == UniqueId::forType<RLEBatch<std::string>>()) {
        if constexpr(ExpressionBatchBase::template isAllowedType<std::string>()) {
          auto uniquePtr =
              std::unique_ptr<RLEBatch<std::string>>(static_cast<RLEBatch<std::string>*>(batchPtr));
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      } else {
        // generic Batch (likely an expression batch)
        if constexpr(ExpressionBatchBase::template isAllowedType<Expression>()) {
          auto uniquePtr = std::unique_ptr<Batch>(batchPtr);
          if constexpr(std::is_same_v<BatchTuple, std::tuple<>>) {
            return evaluateHelper<Index + 1>(++batchPos, end, factory,
                                             std::make_tuple(std::move(uniquePtr)), isRLE);
          } else {
            return std::apply(
                [&, this](auto&&... arg) {
                  return evaluateHelper<Index + 1>(
                      ++batchPos, end, factory,
                      std::make_tuple((std::move(arg))..., std::move(uniquePtr)), isRLE);
                },
                batchTuple);
          }
        } else {
          delete batchPtr;
          return evaluateHelper<Index + 1>(++batchPos, end, factory, std::move(batchTuple), isRLE);
        }
      }
    }
  }
};

template <typename EvaluatorType, typename Func, size_t N = lambda_details<Func>::argument_count,
          typename... BatchArgs>
auto* clone(ExpressionBatchBase<EvaluatorType, Func, N> const& batchBase,
            std::tuple<std::unique_ptr<BatchArgs>...> const& batchArgs) {
  return new ExpressionBatch<EvaluatorType, Func, N, BatchArgs...>(batchBase, batchArgs);
}

template <typename EvaluatorType, typename Func, size_t N = lambda_details<Func>::argument_count,
          typename... BatchArgs>
auto* clone(ExpressionBatchBase<EvaluatorType, Func, N> const& batchBase,
            std::tuple<std::unique_ptr<BatchArgs>...>&& batchArgs) {
  return new ExpressionBatch<EvaluatorType, Func, N, BatchArgs...>(batchBase, std::move(batchArgs));
}

} // namespace boss::engines::bulk
