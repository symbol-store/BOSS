#pragma once

#include <utility>
#include <variant>

namespace boss::utilities {
template <class... Fs> struct overload : Fs... {
  template <class... Ts> explicit overload(Ts&&... ts) : Fs{std::forward<Ts>(ts)}... {}
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

} // namespace boss::utilities
