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
    
     (CreateTable Customer CUSTKEY NAME)
     (InsertInto  Customer 128120 "Alex")
     (InsertInto  Customer 128121 "John")
     (InsertInto  Customer 128121 "Max")

     (CreateTable Lineitem LORDERKEY QUANTITY)
     (InsertInto Lineitem 201 105)
     (InsertInto Lineitem 201 123)
     (InsertInto Lineitem 201 98)
     (InsertInto Lineitem 207 17) 
     (InsertInto Lineitem 209 71) 
     (InsertInto Lineitem 209 205) 
     (InsertInto Lineitem 209 12)    

     

     (CreateTable Orders ORDERKEY OCUSTKEY ORDERDATE TOTALPRICE)
     (InsertInto  Orders 201 128120 (UnixTime "1996-03-13") 18675)
     (InsertInto  Orders 207 128121 (UnixTime "1996-04-22") 1836)
     (InsertInto  Orders 209 128124 (UnixTime "1996-05-12") 37004)
     )))



(eval 
    '((lambda ()
          (Select
            (GroupBy 
              (Join (Join Lineitem Orders (Where (Equal LORDERKEY ORDERKEY))) Customer (Where (Equal CUSTKEY OCUSTKEY))) 
              (By NAME CUSTKEY ORDERKEY ORDERDATE TOTALPRICE) 
              (Sum QUANTITY))
             (Where (Greater QUANTITY 300))) ;; current logic does not allow for IN keyword or for obtaining columns after groupby to then use for a join
            )))






