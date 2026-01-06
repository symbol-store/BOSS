(define-library (BOSS)
  (import (scheme base))
  (import (chibi match))
  (export symbolNameToNewBOSSSymbol boolToNewBOSSExpression getBOSSExpressionTypeID floatToNewBOSSExpression
          newComplexBOSSExpression BOSSEvaluate getFloatValueFromBOSSExpression convert-to-boss-expression
          stringToNewBOSSExpression getIntValueFromBOSSExpression intToNewBOSSExpression)
  (begin (define (convert-to-boss-expression x)
             (match x
                    ((list 'quote argument) (convert-to-boss-expression argument))
                    )
           ))
  (include-shared "libBOSS"))

;; Local Variables:
;; mode: lisp
;; End:
