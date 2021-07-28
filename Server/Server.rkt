#lang racket
;; right now, you need to run it (in the directory where you have RacketBOSS.so) like this
;; racket -e '(load "...../Server.rkt")' -e "(require 'Server)"

(require threading)
(require racket/list)
(define (unflatten l)
  (foldl
   (lambda (op plan)
     (define (append-to-deepest-list l element)
       (if (and (not (empty? l)) (list? (last l)))
           (if (equal? (first element) '::)
               (append l '(::))

               (list-update l
                            (- (length l) 1)
                            (lambda (end)
                              (append-to-deepest-list end element))))
           (if (and (not (empty? l)) (equal? (last l) '::))
               (list-set l (- (length l) 1) (first element))
               (append l element))))
     (if (equal? op ':)
         (append-to-deepest-list plan (list (list)))
         (if (equal? op ':::)
             (append plan (list (list)))
             (append-to-deepest-list plan (list op))
             )
         
         ))
   '()
   l))

(load-extension "RacketBOSS.so")
(require web-server/servlet-env
         web-server/servlet
         threading
         web-server/dispatch)

(define (list->html-table data schema)
  `(div ((style "overflow-x: auto; overflow-y: auto; height:100%;"))
        (table
         ((class "table table-striped table-bordered")
          (style "background:white;width:1024px;white-space: nowrap;"))
         (thead
          (tr ,@(map
                 (lambda (attribute)
                   `(th ((style "position: sticky;top: 0;background-color: white;"))
                        ,(string-replace (string-replace
                                          (format "~a" (first attribute)) "$1" ".") "$0" "_"))
                   ) schema)))
         ,@(map
            (lambda (row) `(tr
                            ,@(map (lambda (col) `(td ,(format "~a" col))) row)))
            data)
         ))
  )



(define (embed-in-page . nested)
  (response/xexpr
   `(html
     (head
      (link ((href "https://cdn.jsdelivr.net/npm/bootstrap@5.0.2/dist/css/bootstrap.min.css")
             (rel "stylesheet")
             (integrity "sha384-EVSTQN3/azprG1Anm3QDgpJLIm9Nao0Yz1ztcQTwFspd3yD65VohhpuuCOmLASjC")
             (crossorigin="anonymous"))
            )
      (script ((src "https://cdn.jsdelivr.net/npm/bootstrap@5.0.2/dist/js/bootstrap.bundle.min.js")
               (integrity "sha384-MrcW6ZMFYlzcLA8Nl+NtUVF0sA7MsXsP1UyJoMp4YLEuNSfAP+JcXn/tWtIaxVXM")
               (crossorigin "anonymous"))
              )
      )
     (body (
            (style "width:1024px; margin:auto; border:1px solid black; border-radius:15px; height: 100%; overflow: hidden;")) ,@nested))
   )
  )

(define (explain req operators)
  (if (equal? (last operators) "RunNativeFunction")
      (let ((plan #`(~> #,@(unflatten
                            (map (lambda (op) (read (open-input-string op)))
                                 operators)))))
        (embed-in-page '(h1 "Result")
                       `(pre ,(format "~a" (eval plan)) )
                       '(hr)
                       `(pre ,(format "~a" (syntax->datum plan)))
                       )
        )


      (let ((plan #`(~> #,@(unflatten (map
                                       (lambda (op) (read (open-input-string op))) operators))))
            (schema #`(~> #,@(unflatten
                              (map
                               (lambda (op) (read (open-input-string op)))
                               operators))
                          Schema))
            )

        (embed-in-page '(h1 "Result")
                       (list->html-table (eval plan) (eval schema))
                       '(hr)
                       `(pre ,(format "~a" (syntax->datum plan)))
                       )
        )))

(define (index req)
  (embed-in-page
   '(h1 "Description")
   #<<"
URLs encode queries as threaded s-expressions. The rough idea is that every path component
is an element in a list. To nest lists, there are three "colon"-operators. A single colon
path component opens a new list. A double colon closes the list. A triple colon closes
all lists (excluding the root), thus stacking another operator on top of the query.
"
   '(h1 "Examples")

   '(list
     (li (a ((href "Customer/:/Project/:/As/Name/FirstName/Last/LastName/Age/age")) "Simple Projection Query"))
     (li (a ((href "Customer/:::/Select/:/Where/:/Equal/FirstName/\"Holger\"/:::/Group/Count")) "Simple Aggregation Query"))
     ))
  )

(define-values (start route-url)
  (dispatch-rules
   [("") index]
   [((string-arg) ...) explain]
   ))

(eval ;; create some sample data in the customer table
 '((lambda ()
     (CreateTable Customer FirstName LastName age)
     (InsertInto  Customer "Holger" "German" 38)
     (InsertInto  Customer "Dude" "Englishman" (Interpolate FirstName))
     (InsertInto  Customer "Hubert" "Frenchman" 34)
     ))
 )

(serve/servlet start
               #:stateless? #t
               #:servlet-path "/"
               #:servlet-regexp #rx""
               #:listen-ip #f
               #:command-line? #t
               )
