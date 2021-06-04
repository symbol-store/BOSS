#pragma once

#include "Batch/Batch.hpp"

namespace boss::engines::bulk {

template <typename... ArgumentTypes> class BatchVisitDispatcher {
public:
  template <typename Func> static bool visit(Func&& func, BulkExpression const& expression) {
    return visit(std::forward<Func>(func), expression, ArgumentTypeList{});
  }
  template <typename Func> static bool visit(Func&& func, BulkExpression&& expression) {
    return visit(std::forward<Func>(func), std::move(expression), ArgumentTypeList{});
  }

  template <typename Func, template <typename...> typename List, typename... ArgumentType>
  static bool visit(Func&& func, BulkExpression const& expression,
                    List<ArgumentType...> /*unused*/) {
    return (... || visit<std::decay_t<Func>, ArgumentType>(func, expression));
  }
  template <typename Func, template <typename...> typename List, typename... ArgumentType>
  static bool visit(Func&& func, BulkExpression&& expression, List<ArgumentType...> /*unused*/) {
    return (... ||
            visit<std::decay_t<Func>, ArgumentType>(func, std::forward<ArgumentType>(expression)));
  }

private:
  template <typename...> struct TypeList {};
  using ArgumentTypeList = TypeList<ArgumentTypes...>;

  template <typename Func, typename ArgumentType>
  static bool visit(Func& func, BulkExpression const& expression) {
    if(std::holds_alternative<ArgumentType>(expression)) {
      func(std::get<ArgumentType>(expression));
      return true;
    }
    return false;
  }

  template <typename Func, typename ArgumentType>
  static bool visit(Func& func, BulkExpression&& expression) {
    if(std::holds_alternative<ArgumentType>(expression)) {
      func(std::get<ArgumentType>(std::move(expression)));
      return true;
    }
    return false;
  }
};

} // namespace boss::engines::bulk
