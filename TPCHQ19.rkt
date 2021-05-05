#lang racket
;; utilities
(load-extension "RacketBOSS.so")
(require rackunit)

(define-syntax-rule (expect producer expected message)
  (check-equal?
   (eval
    '((lambda ()
        producer
        ))) expected message)
  )


;; init db
(eval
 '((lambda () 
    
     (CreateTable Part PARTKEY BRAND CONTAINER SIZE)
     (InsertInto  Part 128120 "Brand#12" "SM PACK" 2)
     (InsertInto  Part 128121 "Brand#23" "MED PACK" 8)
     (InsertInto  Part 128122 "Brand#34" "LG CASE" 14)
     (InsertInto  Part 128123 "Brand#34" "SM PACK" 23)

     (CreateTable Lineitem LPARTKEY QUANTITY SHIPMODE SHIPINSTRUCT EXTENDEDPRICE DISCOUNT)
     (InsertInto Lineitem 128120 8 "AIR REG" "DELIVER IN PERSON" 100 004) ;;using whole numbers because we get an error on floats
     (InsertInto Lineitem 128121 18 "AIR REG" "DELIVER IN PERSON" 202 002)
     (InsertInto Lineitem 128122 24 "AIR" "DELIVER IN PERSON" 104 006) 
     (InsertInto Lineitem 128123 21 "AIR REG" "DELIVER IN PERSON" 2323 009)
 )))

(eval 
    '((lambda ()
      (GroupBy
        (Project
            (Select
              (Join Part Lineitem (Where (Equal PARTKEY LPARTKEY)))
              (Where 
                (Or
                  (And 
                          (Equal BRAND "Brand#12")
                          (Or ;; p_container in ('SM CASE', 'SM BOX', 'SM PACK', 'SM PKG')
                              (Equal CONTAINER "SM CASE")
                              (Equal CONTAINER "SM BOX")
                              (Equal CONTAINER "SM PACK")
                              (Equal CONTAINER "SM PKG")
                          )
                          (Or ;; l_quantity >= 1
                              (Equal QUANTITY 1)
                              (Greater QUANTITY 1)
                          )
                          (Or ;; l_quantity <= 1 + 10
                              (Equal QUANTITY (Plus 1 10))
                              (Greater (Plus 1 10) QUANTITY)
                          )
                          (Or ;; p_size between 1
                              (Equal SIZE 1)
                              (Greater SIZE 1)
                          )
                          (Or ;; and 5
                              (Equal SIZE 5)
                              (Greater 5 SIZE)
                          )
                          (Or ;; l_shipmode in ('AIR', 'AIR REG')
                              (Equal SHIPMODE "AIR")
                              (Equal SHIPMODE "AIR REG")
                          ) 
                          (Equal SHIPINSTRUCT "DELIVER IN PERSON") ;; l_shipinstruct = 'DELIVER IN PERSON'
                      )
                  (And
                          (Equal BRAND "Brand#23")
                          (Or ;; p_container in ('MED BAG', 'MED BOX', 'MED PKG', 'MED PACK')
                              (Equal CONTAINER "MED BAG")
                              (Equal CONTAINER "MED BOX")
                              (Equal CONTAINER "MED PACK")
                              (Equal CONTAINER "MED PKG")
                          )
                          (Or ;; l_quantity >= 10
                              (Equal QUANTITY 10)
                              (Greater QUANTITY 10)
                          )
                          (Or ;; l_quantity <= 10 + 10
                              (Equal QUANTITY (Plus 10 10))
                              (Greater (Plus 10 10) QUANTITY)
                          )
                          (Or ;; p_size between 1
                              (Equal SIZE 1)
                              (Greater SIZE 1)
                          )
                          (Or ;; and 10
                              (Equal SIZE 10)
                              (Greater 10 SIZE)
                          )
                          (Or ;; l_shipmode in ('AIR', 'AIR REG')
                              (Equal SHIPMODE "AIR")
                              (Equal SHIPMODE "AIR REG")
                          ) 
                          (Equal SHIPINSTRUCT "DELIVER IN PERSON") ;; l_shipinstruct = 'DELIVER IN PERSON'
                  )
                  (And
                          (Equal BRAND "Brand#34")
                          (Or ;; p_container in ('LG CASE', 'LG BOX', 'LG PACK', 'LG PKG')
                              (Equal CONTAINER "LG CASE")
                              (Equal CONTAINER "LG BOX")
                              (Equal CONTAINER "LG PACK")
                              (Equal CONTAINER "LG PKG")
                          )
                          (Or ;; l_quantity >= 20
                              (Equal QUANTITY 20)
                              (Greater QUANTITY 20)
                          )
                          (Or ;; l_quantity <= 20 + 10
                              (Equal QUANTITY (Plus 20 10))
                              (Greater (Plus 20 10) QUANTITY)
                          )
                          (Or ;; p_size between 1
                              (Equal SIZE 1)
                              (Greater SIZE 1)
                          )
                          (Or ;; and 15
                              (Equal SIZE 15)
                              (Greater 15 SIZE)
                          )
                          (Or ;; l_shipmode in ('AIR', 'AIR REG')
                              (Equal SHIPMODE "AIR")
                              (Equal SHIPMODE "AIR REG")
                          ) 
                          (Equal SHIPINSTRUCT "DELIVER IN PERSON") ;; l_shipinstruct = 'DELIVER IN PERSON'
                  )
                )
              )
            )
        (As revenue (Times EXTENDEDPRICE (Subtract 100 DISCOUNT)))
        )
        (Function 0)
        (Sum revenue)
      )
)))
