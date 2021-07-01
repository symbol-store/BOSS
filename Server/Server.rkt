#lang racket
;; right now, you need to run it (in the directory where you have RacketBOSS.so) like this
;; racket -e '(load "...../Server.rkt")' -e "(require 'Server)"
(load-extension "RacketBOSS.so")
(require web-server/servlet-env
         web-server/servlet
         web-server/dispatch)

(define (query req table)
  (response/xexpr
   `(html (body (h2 ,table)
                (list
                 ,@(eval '((lambda () ;; here comes the actual query
                                (Project
                                 Customer
                                 (As HtmlElement 'li
                                     Name '(em FirstName)
                                     )
                                 )
                                ))))
                ))))

(define-values (start route-url)
  (dispatch-rules
   [((string-arg)) query]
   ))

(eval ;; create some sample data in the customer table
 '((lambda ()
     (CreateTable Customer FirstName LastName)
     (InsertInto  Customer "Holger" "German")
     (InsertInto  Customer "Hubert" "Frenchman")
     ))
 )

(serve/servlet start
               #:stateless? #t
               #:servlet-path "/"
               #:servlet-regexp #rx""
               )

