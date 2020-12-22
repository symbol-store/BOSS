#pragma once

#include <array>
#include <vector>

namespace boss::engines::bulk {

template <typename... TYPES> class ForTypes {
public:
  template <typename AtomicFunc = bool, typename BatchFunc = bool> class Evaluator {
  public:
    template <typename...> struct TypeList {};
    using types = TypeList<TYPES...>;

    Evaluator(AtomicFunc const& func) : m_func(func) {}
    Evaluator(AtomicFunc&& func) : m_func(func) {}
    Evaluator(Evaluator const&) = default;

    template <typename BatchOut, typename... BatchIn, typename Func = AtomicFunc,
              typename std::enable_if_t<!std::is_same_v<Func, bool>, bool> = false>
    void operator()(BatchOut& out, BatchIn const&... in) const {
      [&, this](auto&&... inIt) {
        auto outIt = out.begin();
        for(; outIt != out.end() && ((inIt != in.end()) && ...); ++outIt, ((++inIt), ...)) {
          *outIt = m_func((*inIt)...);
        }
      }(in.begin()...);
    }

    template <typename BatchOut, typename... BatchIn, typename Func = BatchFunc,
              typename std::enable_if_t<!std::is_same_v<Func, bool>, int> = 0>
    void operator()(BatchOut& out, BatchIn const&... in) const {
      m_batchFunc(out, in...);
    }

  private:
    AtomicFunc m_func;
    BatchFunc m_batchFunc;
  }; // class Evaluator

}; // class ForTypes

} // namespace boss::engines::bulk
