#include <string>
std::string getRacketMacroShims() {
  // NOLINTNEXTLINE
  return
      R"(
(require racket/match)
(define (convert-to-boss-expression x)
  (match x
    [(list head arguments ...) (new-Expression (new-ComplexExpression head (map convert-to-boss-expression arguments)))]
    [(and i (? integer?)) (new-Expression i)]
    [(and s (? string?)) (new-Expression s)]
    [(and s (? symbol?)) (new-Expression (new-Symbol (symbol->string s)))]
    [_ 'unknown]
    )
  )
    (define-syntax-rule (Plus args ...) (evaluate (convert-to-boss-expression (list 'Plus args ...))))
    (define-syntax-rule (Greater args ...) (evaluate (convert-to-boss-expression (list 'Greater args ...))))
    (define-syntax-rule (CreateTable args ...) (evaluate (convert-to-boss-expression (list 'CreateTable args ...))))
    (define-syntax-rule (InsertInto args ...) (evaluate (convert-to-boss-expression (list 'InsertInto args ...))))
    (define-syntax-rule (GroupBy args ...) (evaluate (convert-to-boss-expression (list 'GroupBy args ...))))
    (define-syntax-rule (Select args ...) (evaluate (convert-to-boss-expression (list 'Select args ...))))
)";
}
