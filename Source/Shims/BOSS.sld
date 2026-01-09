(define-library (BOSS)
    (import (scheme base) (srfi 166) (srfi 158) (chibi match))

  (export symbolNameToNewBOSSSymbol boolToNewBOSSExpression getBOSSExpressionTypeID floatToNewBOSSExpression
          newComplexBOSSExpression BOSSEvaluate getFloatValueFromBOSSExpression convert-to-boss-expression convert-from-boss-expression
          stringToNewBOSSExpression getIntValueFromBOSSExpression intToNewBOSSExpression)
  (begin
   (define bossTypeID '(bool int8 int32 long float double string symbol complexExpression))

   (define (convert-to-boss-expression x)
       (match x
              ((head arguments ...) (newComplexBOSSExpression (convert-to-boss-expression head) (length arguments) (map convert-to-boss-expression arguments)))
              (('quote argument) (convert-to-boss-expression argument))
              ((? exact-integer? i) (longToNewBOSSExpression i))
              ((? real? f) (doubleToNewBOSSExpression f))
              ((? string? s) (stringToNewBOSSExpression s))
              ((? symbol? s) (symbolNameToNewBOSSSymbol (symbol->string s)))
              )
     )

   (define (convert-from-boss-expression x)
       (case (list-ref bossTypeID  (getBOSSExpressionTypeID x))

         ('complexExpression
          (let ((args (getArgumentsFromBOSSExpression x)))
            `(
              ,(string->symbol (bossSymbolToNewString (getHeadFromBOSSExpression x)))
              ,@(generator-map->list
                 (lambda (i) (convert-from-boss-expression (getArgumentFromBOSSExpressionArray args i)))
                 (make-iota-generator (getArgumentCountFromBOSSExpression x)) ))
            ))
         ('int32 (getIntValueFromBOSSExpression x))
         ('string (getNewStringValueFromBOSSExpression x))
         ('long (getLongValueFromBOSSExpression x))
         (else (show #f "unknown, type: " (list-ref bossTypeID  (getBOSSExpressionTypeID x))) )
         )
     )


   )
  (include-shared "libBOSS"))

;; Local Variables:
;; mode: lisp
;; eval: (lispy-mode)
;; End:
