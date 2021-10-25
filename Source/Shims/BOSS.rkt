#lang racket
(require racket/match
         racket/trace
         ffi/unsafe
         (rename-in racket/contract [-> -->]))

(define libBoss (ffi-lib "lib/libBOSS"))

(define symbolToNewString
  (get-ffi-obj "bossSymbolToNewString" libBoss (_fun _pointer -> _string))
  )
(define symbolNameToNewSymbol
  (get-ffi-obj "symbolNameToNewBOSSSymbol" libBoss (_fun _string  -> _pointer)))
(define freeSymbol (get-ffi-obj "freeBOSSSymbol" libBoss (_fun _pointer -> _void)))

(define (gcSymbol x) (begin (register-finalizer x freeSymbol)
                       x))

(define _Symbol (make-ctype _pointer
                            (lambda (s) (gcSymbol (symbolNameToNewSymbol (symbol->string s))))
                            (lambda (s)  (let ( [r (string->symbol (symbolToNewString s))])
                                           (freeSymbol s)
                                           r))))


(define intToNewExpression (get-ffi-obj "intToNewBOSSExpression" libBoss (_fun _int  -> _pointer)))
(define floatToNewExpression
  (get-ffi-obj "floatToNewBOSSExpression" libBoss (_fun _float  -> _pointer)))
(define stringToNewExpression
  (get-ffi-obj "stringToNewBOSSExpression" libBoss (_fun _string  -> _pointer)))
(define symbolNameToNewExpression
  (get-ffi-obj "bossSymbolNameToNewBOSSExpression" libBoss (_fun _string  -> _pointer)))

(define newComplexExpression
  (get-ffi-obj "newComplexBOSSExpression" libBoss
               (_fun _Symbol _size _pointer -> _pointer)))
(define freeExpression
  (get-ffi-obj "freeBOSSExpression" libBoss
               (_fun _pointer -> _void)))
(define/contract (gcExpression x)
  (cpointer? . --> . cpointer?)
  (begin

    (register-finalizer x freeExpression)
    x))

(define (convert-to-boss-expression x)
  (match x
    [(list 'quote argument) (convert-to-boss-expression argument)]
    [(list head arguments ...)
     (gcExpression
      (newComplexExpression head (length arguments)
                            (list->cblock (map convert-to-boss-expression arguments) _pointer)))]
    [(and i (? exact-integer?)) (gcExpression (intToNewExpression i))]
    [(and f (? real?)) (gcExpression (floatToNewExpression f))]
    [(and s (? string?)) (gcExpression (stringToNewExpression s))]
    [(and s (? symbol?))
     (if (string-contains? (symbol->string s) ":")
         (gcExpression
          (newComplexExpression
           'list
           2
           (list->cblock
            (map convert-to-boss-expression
                 (map string->symbol
                      (list (first (string-split (symbol->string s) ":"))
                            (second (string-split (symbol->string s) ":"))))) _pointer)))
         (gcExpression (symbolNameToNewExpression (symbol->string s)))
         )
     ]
    [_ 'unknown]
    )
  )

(define bossTypeID (_enum '(bool int float string symbol complexExpression))
  )

(define getTypeID (get-ffi-obj "getBOSSExpressionTypeID" libBoss (_fun _pointer -> bossTypeID)))
(define getBoolValueFromExpression (get-ffi-obj "getBoolValueFromBOSSExpression" libBoss
                                                (_fun _pointer -> _bool)))
(define getIntValueFromExpression (get-ffi-obj "getIntValueFromBOSSExpression" libBoss
                                               (_fun _pointer -> _int)))
(define getFloatValueFromExpression (get-ffi-obj "getFloatValueFromBOSSExpression" libBoss
                                                 (_fun _pointer -> _float)))
(define getStringValueFromExpression (get-ffi-obj "getNewStringValueFromBOSSExpression" libBoss
                                                  (_fun _pointer -> _string)))
(define getHeadFromExpression (get-ffi-obj "getHeadFromBOSSExpression" libBoss
                                           (_fun _pointer -> _Symbol)))
(define getArgumentCountFromExpression (get-ffi-obj "getArgumentCountFromBOSSExpression" libBoss
                                                    (_fun _pointer -> _size)))
(define getArgumentsFromExpression (get-ffi-obj "getArgumentsFromBOSSExpression" libBoss
                                                (_fun _pointer -> _pointer)))

(define getSymbolNameFromExpression (get-ffi-obj "getNewSymbolNameFromBOSSExpression" libBoss
                                                 (_fun _pointer -> _string)))


(define (convert-from-boss-expression x)
  (case (getTypeID x)
    ['bool (getBoolValueFromExpression x)]
    ['int (getIntValueFromExpression x)]
    ['float (getFloatValueFromExpression x)]
    ['string (let ([r (getStringValueFromExpression x)])
               r) ]
    ['symbol (getSymbolNameFromExpression x)]
    ['complexExpression
     (let ([arguments (map convert-from-boss-expression
                           (ptr-ref
                            (getArgumentsFromExpression x)
                            (_array/list _pointer (getArgumentCountFromExpression x))
                            ))
                      ])
       (if (eq? 'List (getHeadFromExpression x))
           arguments
           `(,(getHeadFromExpression x) ,@arguments)))
     ]
    [else "unknown"]
    )
  )

(define _Expression (make-ctype _pointer convert-to-boss-expression
                                (lambda (x) (convert-from-boss-expression (gcExpression x)) )
                                ))

(define evaluate
  (get-ffi-obj "BOSSEvaluate" libBoss (_fun _Expression -> _Expression)))

(provide evaluate)
(provide gcExpression)
(define-syntax-rule (define-operator name arguments ...)
  (begin
    (provide name)
    (define-syntax (name stx)
      (syntax-case stx ()
        ((_ arguments ...)  #'(evaluate '(name arguments ...)))
        )
      )))
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
  ;; '(Group input aggregateFunction ...)
  '(Select input predicate)
  '(Project input projectionFunction)
  '(ProjectAll input name)
  '(Join leftInput rightInput predicate)
  '(Order input predicate)
  '(Load relation file)
  '(Schema input)
  '(Assuming input assumption ...)
  '(Top input predicate number)
  '(EvaluateInEngine pathToLibrary expression ...)
  )

(provide main)
(define (main . expressions)
  (for ([expression expressions])
    (printf "~s" (eval (read (open-input-string expression)))))
  )
