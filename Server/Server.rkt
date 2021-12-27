#!/usr/bin/env racket -tm
#lang racket
(require threading)
(require racket/list)
(require macro-debugger/expand)
(require json)
(require web-server/servlet-env
         web-server/servlet
         web-server/templates
         web-server/dispatch
         web-server/configuration/responders)
(require "BOSS.rkt")
(define library-path (make-parameter null))

(define (unflatten l)
  (foldl
   (lambda (op plan)
     (define (append-to-deepest-list l element)
       (if (and (not (empty? l)) (list? (last l)))
           (if (and (equal? (first element) '::)
                    (or (empty? (last l)) (not (list? (last (last l))))))
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

(define (html-table-handler req operators)
  (let ((plan (expand-only #`(~> #,@(unflatten (map
                                                (lambda (op) (read (open-input-string op))) operators)))
                           (list #'~>)))
        (schema (expand-only  #`( ~> #,@(unflatten
                                         (map
                                          (lambda (op) (read (open-input-string op)))
                                          operators))
                                     Schema)
                              (list #'~>)))
        )
    (embed-in-page '(h1 "Result")
                   (list->html-table (eval #`(EvaluateInEngine #,(library-path) #,plan))
                                     (eval #`(EvaluateInEngine #,(library-path) #,schema)))
                   '(hr)
                   `(pre ,(format "~a" (syntax->datum (expand-only plan (list #'~>)))))
                   )
    ))

(define (rest-handler req operators)
  ;jsonify needs evaluation of schema and plan, which is not ok for modifying queries
  (define string-query (format "~a" operators))
  (define is-modifying-query 
    (or 
     (string-contains? string-query "CreateTable")
     (string-contains? string-query "InsertInto")
     )
    )

  (let ((plan
         (expand-only
          #`(~> #,@(unflatten
                    (map
                     (lambda (op)
                       (read
                        (open-input-string op))) operators)))
          (list #'~>)))
        (schema
         (expand-only
          #`( ~> #,@(unflatten
                     (map
                      (lambda (op)
                        (read
                         (open-input-string op)))
                      operators))
                 Schema)
          (list #'~>)))
        )

    (when is-modifying-query (eval #`(EvaluateInEngine #,(library-path) #,schema)) )

    (define json-response
      (if is-modifying-query
          (
           string-append
           "[{\"query\":\""
           string-query
           "\",\"executed\":\"yes\"}]"
                                       
           )

          (jsonify
           (eval #`(EvaluateInEngine #,(library-path) #,schema))
           (eval #`(EvaluateInEngine #,(library-path) #,plan)) 
           )
          )
      )

    (response/full
     200
     #"OK"
     (current-seconds)
     TEXT/HTML-MIME-TYPE
     '()
     (list (string->bytes/utf-8
            json-response
            ))
     )
    )
  )

(define (listsToJsexpr schema data)
  (map
   (lambda (record)
     (make-hasheq              
      (map
       (lambda (item field)
         (cons (string->symbol(~a(car field))) (~a item))
         )
       record
       schema
       )
      )
     )
   data
   )
  )

(define (jsonify schema data)
  (define my-jsexpr (listsToJsexpr schema data))
  (define result
    (with-output-to-string
      (λ ()
        (string-trim (~a (write-json my-jsexpr)) "#<void>")
        )
      )
    )
  result
  )

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
     (li (a ((href "html-table/Customer/:/Project/:/As/Name/FirstName/Last/LastName/Age/age")) "Simple Projection Query"))
     (li (a ((href "html-table/Customer/:::/Select/:/Where/:/Equal/FirstName/\"Holger\"/:::/Group/Count")) "Simple Aggregation Query"))
     (li (a ((href "rest-example-page")) "REST example page"))
     (li (a ((href "html-table/\"libBOSSMQTTEngine.so\"/:/EvaluateInEngine/:/StartMQTTServer")) "Start MQTT (make sure the library is installed)"))
     ))
  )

(define (rest-example-page req)
 (response/full
  200
  #"OK"
  (current-seconds)
  TEXT/HTML-MIME-TYPE
  '()
  (list (string->bytes/utf-8
    (include-template "htdocs/restExamplePage.html")
    ))
 )
)

(define-values (start route-url)
  (dispatch-rules
   [("") index]
   ; REST interface to BOSS (https://lisp.sh/crud-web-api-in-racket/)
   [("rest" (string-arg) ...) #:method "get" rest-handler]
   [("rest-example-page") rest-example-page]
   ; api divided in "html-table" and "rest" so that racket can serve static files
   [("html-table" (string-arg) ...) html-table-handler]
   ))


(require racket/cmdline)
(command-line #:once-each
              ["--library" path "path to the boss engine implementation" (library-path path)]
              #:usage-help
              "run me like this:"
              ""
              "  Server.rkt -- --library ./path/to/your/default/engine/library" )

(provide main)
(define (main . args)
  (eval #`
   (EvaluateInEngine
    #,(library-path)
    (CreateTable Customer FirstName LastName age)
    (InsertInto  Customer "Holger" "German" 38)
    (InsertInto  Customer "Dude" "Englishman" (Interpolate FirstName))
    (InsertInto  Customer "Hubert" "Frenchman" 34)))
  (serve/servlet start
                 #:stateless? #t
                 #:servlet-path "/"
                 #:servlet-regexp #rx""
                 #:listen-ip #f
                 #:command-line? #t
                 #:extra-files-paths (list (build-path "bin/htdocs"))
                 ))
