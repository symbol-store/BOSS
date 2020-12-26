#pragma once

#include <array>
#include <vector>

namespace boss::engines::bulk {

/********************* class Evaluator ************************/

/* to call iteratively a generic lambda function              */
/* from a specifc list of argument Batch types                */
/**************************************************************/

template <typename... Types> class ForTypes {
public:
  template <typename AtomicFunc = bool, typename BatchFunc = bool> class Evaluator {
  public:
    using Func = std::conditional_t<!std::is_same_v<AtomicFunc, bool>, AtomicFunc, BatchFunc>;

    Evaluator(AtomicFunc const& func) : m_func(func) {}
    Evaluator(AtomicFunc&& func) : m_func(func) {}
    Evaluator(Evaluator const&) = default;

    template <typename BatchOut, typename... BatchIn, typename UsedFunc = AtomicFunc,
              typename std::enable_if_t<!std::is_same_v<UsedFunc, bool>, bool> = false>
    void operator()(BatchOut& out, BatchIn const&... in) const {
      [&, this](auto&&... inIt) {
        auto outIt = out.begin();
        for(; outIt != out.end() && ((inIt != in.end()) && ...); ++outIt, ((++inIt), ...)) {
          *outIt = m_func((*inIt)...);
        }
      }(in.begin()...);
    }

    template <typename BatchOut, typename... BatchIn, typename UsedFunc = BatchFunc,
              typename std::enable_if_t<!std::is_same_v<UsedFunc, bool>, int> = 0>
    void operator()(BatchOut& out, BatchIn const&... in) const {
      m_batchFunc(out, in...);
    }

    static constexpr bool isAllowedType(UniqueId::type typeId) {
      return ((typeId == UniqueId::forType<Types>()) || ...);
    }

    template <typename Type> static constexpr bool isAllowedType() {
      return ((std::is_same_v<Type, Types>) || ...);
    }

    template <typename... Ts> struct AreAllowedTypes {
      static constexpr bool value = (isAllowedType<Ts>() && ...);
    };

  private:
    union {
      AtomicFunc m_func;
      BatchFunc m_batchFunc;
    };
  }; // class Evaluator

}; // class ForTypes

} // namespace boss::engines::bulk
