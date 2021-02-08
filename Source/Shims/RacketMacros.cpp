#include <string>
std::string getRacketMacroShims() {
  // NOLINTNEXTLINE
  return
      R"(
    (define-syntax-rule (Plus a b) (evaluate (new-Expression (new-ComplexExpression `Plus `(,(new-Expression a) ,(new-Expression b))))))
    (define-syntax-rule (CreateTable name columns ...) (evaluate (new-Expression (new-ComplexExpression 'CreateTable (append (list (new-Expression (new-Symbol (symbol->string name)))) (map new-Expression (list columns ...)))))))
    (define-syntax-rule (InsertInto name values ...) (evaluate (new-Expression (new-ComplexExpression 'InsertInto (append (list (new-Expression (new-Symbol (symbol->string name)))) (map new-Expression (list values ...)))))))
    (define-syntax-rule (GroupBy input groupFunction aggregationFunction) (evaluate (new-Expression (new-ComplexExpression `GroupBy `(,(new-Expression (new-Symbol (symbol->string input))) ,(new-Expression (new-ComplexExpression `Function `(,(new-Expression 0)) )) ,(new-Expression (new-Symbol (symbol->string aggregationFunction))))))))
)";
}
