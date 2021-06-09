#include <string>
/**
 * This is some racket code that establishes a convenient interface to BOSS --
 * right now I am just doing it using a raw string literal (maybe we'll use our expression builder
 * at some point)
 */
std::string getRacketMacroShims() {
  // NOLINTNEXTLINE
  return
      R"(
;; Begin Racket
(require racket/match)
(define (convert-to-boss-expression x)
  (match x
    [(list 'quote argument) (convert-to-boss-expression argument)]
    [(list head arguments ...) (new-Expression (new-ComplexExpression head (map convert-to-boss-expression arguments)))]
    [(and i (? integer?)) (new-Expression i)]
    [(and s (? string?)) (new-Expression s)]
    [(and s (? symbol?)) (new-Expression
                          (new-Symbol
                           (first (string-split (symbol->string s) ":"))
                           ))]
    [_ 'unknown]
    )
  )
(define-syntax-rule (define-operator name arguments ...)
  (define-syntax (name stx)
    (syntax-case stx ()
      ((_ arguments ...) #'(evaluate (convert-to-boss-expression '(name arguments ...))) )
      )
    ))
(define-syntax-rule (define-operators '(name arguments ...) ...)
  (begin (define-operator name arguments ...) ...)
  )

;; here is where we define new operators
(define-operators
  '(InsertInto relation values ...)
  '(Where conditionExpression)
  '(Greater left right)
  '(Plus operands ...)
  '(CreateTable relationName attributes ...)
  '(Group input groupFunction aggregateFunction ...)
  '(Group input aggregateFunction ...)
  '(Select input predicate)
  '(Project input projectionFunction)
  '(Join leftInput rightInput predicate)
  '(Order input predicate)
  '(Assuming input assumption ...)
  '(Top input predicate number)
  )
;; End Racket
     )";
}

// Local Variables:
// mode: poly-c++
// fill-column: 0
// End:
