#pragma once

#include "Expression.hpp"
#include <algorithm>

namespace boss::algorithm {
template <typename Container, typename Visitor> void visitEach(Container c, Visitor v) {
  ::std::for_each(c.begin(), c.end(), [&v](auto&& item) {
    boss::std::visit([&v](auto&& item) { return v(item); }, item);
  });
}

  template <typename Container, typename Init, typename Visitor>
auto visitAccumulate(Container c, Init i, Visitor v) {
  return ::std::accumulate(c.begin(), c.end(), i, [&v](auto&& state, auto&& item) {
    return boss::std::visit(
        [&state, &v](auto&& item) { return v(::std::forward<decltype(state)>(state), item); },
        item);
  });
}

template <typename Container, typename TransformVisitor, typename Init, typename AccumulateVisitor>
auto visitTransformAccumulate(Container c, TransformVisitor t, Init i, AccumulateVisitor v) {
  return ::std::accumulate(c.begin(), c.end(), i, [&v, &t](auto&& state, auto&& item) {
    return boss::std::visit(
        [&state, &v](auto&& item) { return v(::std::forward<decltype(state)>(state), item); },
        boss::std::visit([&t](auto&& item) { return t(item); }, item));
  });
}

} // namespace boss::algorithm
