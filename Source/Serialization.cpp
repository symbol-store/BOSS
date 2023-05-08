#include "Serialization.hpp"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <string>
#include <string_view>
namespace boss::serialization::url {

using boss::ComplexExpression;
using boss::Expression;
using boss::Symbol;
Expression opportunisticallyParseExpression(std::string_view& url,
                                            std::optional<Expression>&& firstArgument = {});

boss::expressions::ExpressionArguments parseArguments(std::string_view& url) {
  boss::expressions::ExpressionArguments arguments;
  while(url.at(0) != ')') {
    arguments.push_back(opportunisticallyParseExpression(url));
    if(url.front() == ',') {
      url.remove_prefix(1);
    }
  }
  return arguments;
};

Expression opportunisticallyParseExpression(std::string_view& url,
                                            std::optional<Expression>&& firstArgument) {
  auto argumentListStart = url.find_first_of("(,)");
  auto head = url.substr(0, argumentListStart);
  url.remove_prefix(argumentListStart);
  if(argumentListStart == std::string_view::npos || url.front() != '(') {
    if(std::all_of(head.begin(), head.end(), ::isdigit)) {
      return std::stol(std::string(head));
    }
    return Symbol(std::string(head));
  }
  url.remove_prefix(1);

  boss::expressions::ExpressionArguments arguments;
  if(firstArgument) {
    arguments.push_back(std::move(firstArgument).value());
  }
  auto parsedArguments = parseArguments(url);
  arguments.insert(arguments.end(), std::move_iterator(parsedArguments.begin()),
                   std::move_iterator(parsedArguments.end()));
  url.remove_prefix(1);
  return ComplexExpression(Symbol(std::string(head)), std::move(arguments));
}

Expression parseComponent(std::string_view url, std::optional<Expression>&& firstArgument = {}) {
  return opportunisticallyParseExpression(url, std::move(firstArgument));
}

boss::Expression parse(std::string_view url, std::optional<Expression>&& firstArgument) {
  auto endOfComponent = url.find("/");
  Expression result = parseComponent(url.substr(0, endOfComponent));
  url.remove_prefix(endOfComponent == std::string_view::npos ? url.size() : endOfComponent + 1);
  while(!url.empty()) {
    auto endOfComponent = url.find("/");
    result = parseComponent(url.substr(0, endOfComponent), std::move(result));
    url.remove_prefix(endOfComponent == std::string_view::npos ? url.size() : endOfComponent + 1);
  }
  return result;
}

} // namespace boss::serialization::url

struct BOSSExpression* parseURL(char const* url) {
  return new BOSSExpression{.delegate = boss::serialization::url::parse(url)};
}
