(define-library (BOSS)
    (import (scheme base) (srfi 166) (srfi 158) (chibi match))

  (export symbolNameToNewBOSSSymbol boolToNewBOSSExpression getBOSSExpressionTypeID floatToNewBOSSExpression
          newComplexBOSSExpression BOSSEvaluate getFloatValueFromBOSSExpression convert-to-boss-expression convert-from-boss-expression
          stringToNewBOSSExpression getIntValueFromBOSSExpression intToNewBOSSExpression boss-eval)
  (begin
   (define bossTypeID '(bool int8 int32 long float double string symbol complexExpression))

   (define (convert-to-boss-expression x)
       (match x
              (('quote argument) (convert-to-boss-expression argument))
              ((head arguments ...) (newComplexBOSSExpression (symbolNameToNewBOSSSymbol (symbol->string head)) (length arguments) (map convert-to-boss-expression arguments)))
              ((? boolean? b) (boolToNewBOSSExpression b))
              ((? exact-integer? i) (if (and (>= i -2147483648) (<= i 2147483647))
                                        (intToNewBOSSExpression i)
                                        (longToNewBOSSExpression i)))
              ((? real? f) (doubleToNewBOSSExpression f))
              ((? string? s) (stringToNewBOSSExpression s))
              ((? symbol? s) (symbolNameToNewBOSSExpression (symbol->string s)))
              )
     )

   (define (convert-from-boss-expression x)
       (case (list-ref bossTypeID  (getBOSSExpressionTypeID x))

         ('complexExpression
          (let ((args (getArgumentsFromBOSSExpression x)))
            (dynamic-wind
              (lambda () #f)
              (lambda ()
                `(
                  ,(string->symbol (bossSymbolToNewString (getHeadFromBOSSExpression x)))
                  ,@(generator-map->list
                     (lambda (i) (convert-from-boss-expression (getArgumentFromBOSSExpressionArray args i)))
                     (make-iota-generator (getArgumentCountFromBOSSExpression x)) )))
              (lambda ()
                (freeBOSSArguments args)))))
         ('int32 (getIntValueFromBOSSExpression x))
         ('int8 (getCharValueFromBOSSExpression x))
         ('string (getNewStringValueFromBOSSExpression x))
         ('long (getLongValueFromBOSSExpression x))
         ('double (getDoubleValueFromBOSSExpression x))
         ('float (getFloatValueFromBOSSExpression x))
         ('bool (getBoolValueFromBOSSExpression x))
         ('symbol (string->symbol (getNewSymbolNameFromBOSSExpression x)))
         (else (show #f "unknown, type: " (list-ref bossTypeID  (getBOSSExpressionTypeID x))) )
         )
     )

   (define-syntax boss-eval (syntax-rules () ((boss-eval query) (convert-from-boss-expression (BOSSEvaluate (convert-to-boss-expression (quote query)))))))

   )
  (include-shared "libBOSS"))

;; Local Variables:
;; mode: lisp
;; eval: (lispy-mode)
;; End:
