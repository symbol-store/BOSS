(import (scheme base)
        (chibi test))

;;; Tests for convert-to-boss-expression / convert-from-boss-expression round-trips
;;; and for boss-eval with a passthrough engine.

(test-group "convert-to-boss-expression and convert-from-boss-expression round-trips"

  (test-group "atomic types"

    (test "integer round-trip"
      42
      (convert-from-boss-expression (convert-to-boss-expression 42)))

    (test "large integer (long) round-trip"
      3000000000
      (convert-from-boss-expression (convert-to-boss-expression 3000000000)))

    (test "float round-trip"
      3.14
      (convert-from-boss-expression (convert-to-boss-expression 3.14)))

    (test "string round-trip"
      "hello"
      (convert-from-boss-expression (convert-to-boss-expression "hello")))

    (test "symbol round-trip"
      'mySymbol
      (convert-from-boss-expression (convert-to-boss-expression 'mySymbol)))

    (test "boolean true round-trip"
      #t
      (convert-from-boss-expression (convert-to-boss-expression #t)))

    (test "boolean false round-trip"
      #f
      (convert-from-boss-expression (convert-to-boss-expression #f))))

  (test-group "complex expressions"

    (test "nullary expression round-trip"
      '(Head)
      (convert-from-boss-expression (convert-to-boss-expression '(Head))))

    (test "unary expression round-trip"
      '(Plus 1)
      (convert-from-boss-expression (convert-to-boss-expression '(Plus 1))))

    (test "binary expression round-trip"
      '(Plus 1 2)
      (convert-from-boss-expression (convert-to-boss-expression '(Plus 1 2))))

    (test "nested expression round-trip"
      '(Select (From myTable) (Where (Greater age 30)))
      (convert-from-boss-expression
        (convert-to-boss-expression
          '(Select (From myTable) (Where (Greater age 30))))))

    (test "expression with string argument round-trip"
      '(Load "myfile.csv")
      (convert-from-boss-expression
        (convert-to-boss-expression '(Load "myfile.csv"))))

    (test "expression with mixed argument types round-trip"
      '(Insert "tableName" 42 3.14)
      (convert-from-boss-expression
        (convert-to-boss-expression '(Insert "tableName" 42 3.14))))))

;;; Large strings (200KB) crossing the boss<->chibi FFI boundary.
(test-group "large strings crossing the boss<->chibi boundary (200KB)"

  (let ((big-string (make-string 204800 #\a)))

    (test "large string atom round-trip"
      big-string
      (convert-from-boss-expression (convert-to-boss-expression big-string)))

    (test "large string as complex-expression argument round-trip"
      (list 'Load big-string)
      (convert-from-boss-expression
        (convert-to-boss-expression (list 'Load big-string))))

    (test "large string as expression head (symbol) round-trip"
      (string->symbol big-string)
      (convert-from-boss-expression
        (convert-to-boss-expression (string->symbol big-string))))))

(test-group "span arguments crossing the boss<->chibi boundary (zero-copy)"

  (let* ((span (bytevectorToNewInt8BOSSSpan #u8(97 98 0 99 255)))
         (address (getBOSSSpanBeginAddress span))
         (back (convert-from-boss-expression
                 (convert-to-boss-expression (list 'Column ':spans span)))))
    (test "round-trips to a Column expression holding a span object"
      #t
      (and (pair? back) (eq? (car back) 'Column) (BOSSExpressionSpan? (list-ref back 2))))
    (test "zero-copy: buffer address preserved through the round-trip"
      address
      (getBOSSSpanBeginAddress (list-ref back 2))))

  ;; :spans is optional -- a bare span object self-routes to the span argument list
  (let* ((span1 (bytevectorToNewInt8BOSSSpan #u8(1 2 3)))
         (span2 (bytevectorToNewInt8BOSSSpan #u8(1 2 3)))
         (with-keyword (convert-from-boss-expression
                         (convert-to-boss-expression (list 'Column ':spans span1))))
         (without-keyword (convert-from-boss-expression
                            (convert-to-boss-expression (list 'Column span2)))))
    (test "bare span self-routes like an explicit :spans span"
      (list 'Column #t)
      (list (car without-keyword) (BOSSExpressionSpan? (list-ref without-keyword 2))))
    (test "explicit :spans keyword yields the same shape"
      (list 'Column #t)
      (list (car with-keyword) (BOSSExpressionSpan? (list-ref with-keyword 2))))))

(test-exit)
