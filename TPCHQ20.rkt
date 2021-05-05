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
    
    (CreateTable Nation NATIONKEY NNAME)
    (InsertInto Nation 12 "Canada")
    (CreateTable Supplier SUPPKEY SNAME SNATIONKEY SADDRESS)
    (InsertInto Supplier 102 "Supplier#102" 12 "Exhibition Rd, South Kensington, Lodnon")
    (InsertInto Supplier 103 "Supplier#103" 12 "Charing Cross, London")
    (InsertInto Supplier 104 "Supplier#104" 12 "Chelsea and Westminster, London")
    (InsertInto Supplier 105 "Supplier#105" 12 "Hammersmith, London")



    (CreateTable Lineitem LPARTKEY LSUPPKEY QUANTITY SHIPMODE SHIPDATE SHIPINSTRUCT EXTENDEDPRICE DISCOUNT)
    (InsertInto Lineitem 128120 102 8 "AIR REG" (UnixTime "1994-01-01") "DELIVER IN PERSON" 100 004) ;;using whole numbers because we get an error on floats
    (InsertInto Lineitem 128121 103 18 "AIR REG" (UnixTime "1994-01-01") "DELIVER IN PERSON" 202 002)
    (InsertInto Lineitem 128122 104 24 "AIR" (UnixTime "1994-01-01") "DELIVER IN PERSON" 104 006) 
    (InsertInto Lineitem 128123 105 21 "AIR REG" (UnixTime "1994-01-01") "DELIVER IN PERSON" 2323 009)


    (CreateTable Partsupp PSPARTKEY PSSUPPKEY PSAVAILQTY)
    (InsertInto Partsupp 128120 102 1000)
    (InsertInto Partsupp 128121 103 1023)
    (InsertInto Partsupp 128122 104 14)
    (InsertInto Partsupp 128123 105 73)

    (CreateTable Part PARTKEY BRAND CONTAINER SIZE PNAME)
    (InsertInto  Part 128120 "Brand#12" "SM PACK" 12 "forester")
    (InsertInto  Part 128121 "Brand#23" "MED PACK" 23 "forestdog")
    (InsertInto  Part 128122 "Brand#34" "LG CASE" 23 "foo")
    (InsertInto  Part 128123 "Brand#34" "SM PACK" 23 "bar")
 )))

(eval 
  '((lambda ()
  ;;; (SortBy
    (Project
      (Select
        (Join
          (Join 
            Supplier Nation (Where (Equal SNATIONKEY NATIONKEY))
          )
          (Join 
            (Join 
              Partsupp 
              (Select Part (Where (StringContainsQ PNAME "forest"))) ;; name like "forest%" - not quite, but close enough
              (Where (Equal PARTKEY PSPARTKEY))
            )
            (Project
              (Select Lineitem (
                Where 
                  (And 
                    (Or (Equal SHIPDATE (UnixTime "1994-01-01")) (Greater SHIPDATE (UnixTime "1994-01-01")))
                    (Greater (UnixTime "1995-01-01") SHIPDATE) ;;not sure how to: date '1994-01-01' + interval '1' year
                  )
                )
              )
              (As halfquantitiy (Times 5 QUANTITY) LPARTKEY LPARTKEY LSUPPKEY LSUPPKEY)
            )
            (Where (And 
              (Equal LPARTKEY PSPARTKEY)
              (Equal LSUPPKEY PSSUPPKEY)
              (Greater (Times 10 PSAVAILQTY) halfquantitiy)
              ))
          )
          (Where (Equal SUPPKEY PSSUPPKEY))
        )
        (Where (Equal NNAME "Canada"))
      )
      (As SNAME SNAME SADDRESS SADDRESS)
    )
    ;;; (Greater 1 0) ;;true
    ;;; (By SNAME)
  ;;; )
  
  ))
)


;;long join done
;;; (eval 
;;;   '((lambda ()
;;;     (Join
;;;       (Join 
;;;         Supplier Nation (Where (Equal SNATIONKEY NATIONKEY))
;;;       )
;;;       (Join 
;;;         (Join 
;;;           Partsupp 
;;;           (Select Part (Where (StringContainsQ PNAME "forest"))) ;; name like "forest%" - not quite, but close enough
;;;           (Where (Equal PARTKEY PSPARTKEY))
;;;         )
;;;         (Project
;;;           (Select Lineitem (
;;;             Where 
;;;               (And 
;;;                 (Or (Equal SHIPDATE (UnixTime "1994-01-01")) (Greater SHIPDATE (UnixTime "1994-01-01")))
;;;                 (Greater (UnixTime "1995-01-01") SHIPDATE) ;;not sure how to: date '1994-01-01' + interval '1' year
;;;               )
;;;             )
;;;           )
;;;           (As halfquantitiy (Times 5 QUANTITY) LPARTKEY LPARTKEY LSUPPKEY LSUPPKEY)
;;;         )
;;;         (Where (And 
;;;           (Equal LPARTKEY PSPARTKEY)
;;;           (Equal LSUPPKEY PSSUPPKEY)
;;;           (Greater (Times 10 PSAVAILQTY) halfquantitiy)
;;;           ))
;;;       )
;;;       (Where (Equal SUPPKEY PSSUPPKEY))
;;;     )
  
;;;   ))
;;; )