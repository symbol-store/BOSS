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
(define-syntax (InsertInto syntax-object) (syntax-case syntax-object ()
  ((_ relation values ...) #'((lambda () (evaluate (convert-to-boss-expression '(InsertInto relation values ...))) (void))))
))
(define-syntax (Plus syntax-object) (syntax-case syntax-object ()
  ((_ operands ...) #'(evaluate (convert-to-boss-expression '(Plus operands ...))))
))
(define-syntax (Where syntax-object) (syntax-case syntax-object ()
  ((_ conditionExpression) #'(evaluate (convert-to-boss-expression '(Where conditionExpression))))
))
(define-syntax (Greater syntax-object) (syntax-case syntax-object ()
  ((_ left right) #'(evaluate (convert-to-boss-expression '(Greater left right))))
))
(define-syntax (CreateTable syntax-object) (syntax-case syntax-object ()
  ((_ relationName attributes ...) #'(evaluate (convert-to-boss-expression '(CreateTable relationName attributes ...))))
))
(define-syntax (GroupBy syntax-object) (syntax-case syntax-object ()
  ((_ input groupFunction aggregateFunction) #'(evaluate (convert-to-boss-expression '(GroupBy input groupFunction aggregateFunction))))
  ((_ input aggregateFunction) #'(evaluate (convert-to-boss-expression '(GroupBy input aggregateFunction))))
))
(define-syntax (Select syntax-object) (syntax-case syntax-object ()
  ((_ input predicate) #'(evaluate (convert-to-boss-expression '(Select input predicate))))
))
(define-syntax (Project syntax-object) (syntax-case syntax-object ()
  ((_ input projectionFunction) #'(evaluate (convert-to-boss-expression '(Project input projectionFunction))))
))
(define-syntax (Join syntax-object) (syntax-case syntax-object ()
  ((_ leftInput rightInput predicate) #'(evaluate (convert-to-boss-expression '(Join leftInput rightInput predicate))))
))
)";
}
