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

(test-exit)
