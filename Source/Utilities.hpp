#pragma once
#include <string>
#include <utility>
#include <variant>

namespace boss::utilities {
template <class... Fs> struct overload : Fs... {
  explicit overload(Fs&&... ts) : Fs{std::forward<Fs>(ts)}... {}
  using Fs::operator()...;
};

template <class... Ts> overload(Ts&&...) -> overload<std::remove_reference_t<Ts>...>;

template <typename MaybeMember, typename Variant> struct isVariantMember;

template <typename MaybeMember, typename... ActualMembers>
struct isVariantMember<MaybeMember, std::variant<ActualMembers...>>
    : public std::disjunction<std::is_same<MaybeMember, ActualMembers>...> {};

template <class, template <class...> class> struct isInstanceOfTemplate : public std::false_type {};

template <class... Ts, template <class...> class U>
struct isInstanceOfTemplate<U<Ts...>, U> : public std::true_type {};

template <class... Ts, template <class...> class U>
struct isInstanceOfTemplate<const U<Ts...>, U> : public std::true_type {};

template <template <typename...> typename NewWrapper, typename... Args>
struct rewrap_variant_arguments;

template <template <typename...> typename NewWrapper, typename... Args>
struct rewrap_variant_arguments<NewWrapper, std::variant<Args...>> {
  using type = std::variant<NewWrapper<Args>...>;
};

template <template <typename...> typename NewWrapper, typename... Args>
struct rewrap_variant_arguments_and_add_const;

template <template <typename...> typename NewWrapper, typename... Args>
struct rewrap_variant_arguments_and_add_const<NewWrapper, std::variant<Args...>> {
  using type = std::variant<NewWrapper<Args const>...>;
};
template <typename T, typename... Args> struct variant_amend;

template <typename... Args0, typename... Args1>
struct variant_amend<std::variant<Args0...>, Args1...> {
  using type = std::variant<Args0..., Args1...>;
};

} // namespace boss::utilities
