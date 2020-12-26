#pragma once

namespace boss::engines::bulk {

template <typename F, typename... Args>
struct LambdaInfo : LambdaInfo<decltype(&F::template operator()<Args...>)> {};

template <typename F, typename R, typename... Args> struct LambdaInfo<R (F::*)(Args...) const> {
  using ArgCount = std::integral_constant<size_t, sizeof...(Args)>;
  using ReturnType = R;
};

} // namespace boss::engines::bulk
