#pragma once
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace boss::utilities {
template <typename... Fs> struct overload : Fs... {
  explicit overload(Fs&&... ts) : Fs {std::forward<Fs>(ts)}... {}
  using Fs::operator()...;
};

template <typename... Ts> overload(Ts&&...) -> overload<std::remove_reference_t<Ts>...>;

template <typename, template <typename...> typename>
struct isInstanceOfTemplate : public std::false_type {};

template <typename... Ts, template <typename...> typename U>
struct isInstanceOfTemplate<U<Ts...>, U> : public std::true_type {};

template <typename... Ts, template <typename...> typename U>
struct isInstanceOfTemplate<const U<Ts...>, U> : public std::true_type {};

template <typename, template <typename...> typename>
struct isInstanceOfTemplateWithConstArguments : public std::false_type {};

template <typename... Ts, template <typename...> typename U>
struct isInstanceOfTemplateWithConstArguments<U<Ts const...>, U> : public std::true_type {};

template <typename... Ts, template <typename...> typename U>
struct isInstanceOfTemplateWithConstArguments<const U<Ts const...>, U> : public std::true_type {};

template <template <typename...> typename NewWrapper, typename... Args>
struct rewrap_variant_arguments;

template <template <typename...> typename NewWrapper, typename... Args>
struct rewrap_variant_arguments<NewWrapper, std::variant<Args...>> {
  using type = std::variant<NewWrapper<Args>...>;
};

template <typename... Args> struct make_variant_members_const;

template <typename... Args> struct make_variant_members_const<std::variant<Args...>> {
  using type = std::variant<Args const...>;
};

template <typename T, typename... Args> struct variant_amend;

template <typename... Args0, typename... Args1>
struct variant_amend<std::variant<Args0...>, Args1...> {
  using type = std::variant<Args0..., Args1...>;
};

namespace {
// --------------------
// see https://stackoverflow.com/a/73034562
// --------------------
template <template <class...> class Base, typename... Ts> void test(Base<Ts...>&);

template <template <class...> class, class, class = void>
constexpr bool is_template_base_of = false;

template <template <class...> class Base, class Derived>
constexpr bool is_template_base_of<Base, Derived,
                                   std::void_t<decltype(test<Base>(std::declval<Derived&>()))>> =
    true;
} // namespace

template <typename Derived>
constexpr bool is_variant_like = is_template_base_of<std::variant, Derived>;

template <typename MaybeMember, typename Variant> struct isVariantMember;

template <typename MaybeMember, typename... ActualMembers>
struct isVariantMember<MaybeMember, std::variant<ActualMembers...>>
    : public std::disjunction<std::is_same<MaybeMember, ActualMembers>...> {};

// ------------------------------
// see https://stackoverflow.com/a/33196728
// ------------------------------
template <typename...> using void_t = void;

template <typename L, typename R, typename = void> struct is_comparable : std::false_type {};

template <typename L, typename R>
using comparability = decltype(std::declval<L>() == std::declval<R>());

template <typename L, typename R>
struct is_comparable<L, R, void_t<comparability<L, R>>> : std::true_type {};

namespace {
template <typename VisitorType, typename InputType, size_t index = 0>
InputType opportunisticVisitAndTransform(VisitorType&& visitor, InputType&& x) {
  if constexpr(index >= std::variant_size_v<InputType>) {
    return std::forward<InputType>(x);
  } else if constexpr(std::is_invocable<VisitorType,
                                        std::variant_alternative_t<index, InputType>>::value) {
    if(std::holds_alternative<std::variant_alternative_t<index, InputType>>(x)) {
      return std::forward<VisitorType>(visitor)(
          std::get<std::variant_alternative_t<index, InputType>>(std::forward<InputType>(x)));
    }
    return opportunisticVisitAndTransform<VisitorType, InputType, index + 1>(
        std::forward<VisitorType>(visitor), std::forward<InputType>(x));

  } else {
    return opportunisticVisitAndTransform<VisitorType, InputType, index + 1>(
        std::forward<VisitorType>(visitor), std::forward<InputType>(x));
  }
};
} // namespace

} // namespace boss::utilities
