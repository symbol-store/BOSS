#pragma once

#include "Expression.hpp"
#include <algorithm>
#include <iterator>

namespace boss::algorithm {
template <typename ContainerIt, typename Visitor>
void visitEach(ContainerIt begin, ContainerIt end, Visitor v) {
  using std::for_each;
  for_each(begin, end, [&v](auto&& item) { visit([&v](auto&& item) { return v(item); }, item); });
}

template <typename Container, typename Visitor> void visitEach(Container c, Visitor v) {
  visitEach(begin(c), end(c), std::forward<Visitor>(v));
}

template <typename ContainerIt, typename Init, typename Visitor>
auto visitAccumulate(ContainerIt begin, ContainerIt end, Init i, Visitor v) {
  using std::accumulate;
  return accumulate(begin, end, i, [&v](auto&& state, auto&& item) {
    return visit(
        [&state, &v](auto&& item) {
          return v(::std::forward<decltype(state)>(state), ::std::forward<decltype(item)>(item));
        },
        ::std::forward<decltype(item)>(item));
  });
}

template <typename Container, typename Init, typename Visitor>
auto visitAccumulate(Container c, Init i, Visitor v) {
  return visitAccumulate(begin(c), end(c), std::forward<Init>(i), std::forward<Visitor>(v));
}

template <typename ContainerIt, typename TransformVisitor, typename Init,
          typename AccumulateVisitor>
auto visitTransformAccumulate(ContainerIt begin, ContainerIt end, TransformVisitor t, Init i,
                              AccumulateVisitor v) {
  using std::accumulate;
  return accumulate(begin, end, i, [&v, &t](auto&& state, auto&& item) {
    return visit(
        [&state, &v](auto&& item) { return v(::std::forward<decltype(state)>(state), item); },
        ::std::visit([&t](auto&& item) { return t(::std::forward<decltype(item)>(item)); },
                     ::std::forward<decltype(item)>(item)));
  });
}

template <typename Container, typename TransformVisitor, typename Init, typename AccumulateVisitor>
auto visitTransformAccumulate(Container c, TransformVisitor t, Init i, AccumulateVisitor v) {
  return visitTransformAccumulate(begin(c), end(c), std::forward<TransformVisitor>(t),
                                  std::forward<Init>(i), std::forward<AccumulateVisitor>(v));
}

template <typename ContainerIt, typename TransformVisitor>
auto visitTransform(ContainerIt begin, ContainerIt end, TransformVisitor t) {
  using std::transform;
  return transform(std::move_iterator(begin), std::move_iterator(end), begin, [&t](auto&& item) {
    return visit([&t](auto&& item) { return t(::std::forward<decltype(item)>(item)); },
                 ::std::forward<decltype(item)>(item));
  });
}

template <typename Container, typename TransformVisitor>
auto visitTransform(Container& c, TransformVisitor t) {
  return visitTransform(begin(c), end(c), std::forward<TransformVisitor>(t));
}

} // namespace boss::algorithm
