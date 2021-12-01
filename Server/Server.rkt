#lang racket
;; install:
;; make install
;; run from the install folder:
;; racket -tm bin/Server.rkt

(require threading)
(require racket/list)
(require macro-debugger/expand)
(require "BOSS.rkt")
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

(require web-server/servlet-env
         web-server/servlet
         threading
         web-server/dispatch
         ;needed to dispatch files
         web-server/configuration/responders)

;racket component to embed html pages https://docs.racket-lang.org/web-server/templates.html
(require web-server/templates)

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
                   (list->html-table (eval #`(EvaluateInEngine "lib/libBOSSWolframEngine.so" #,plan))
                                     (eval #`(EvaluateInEngine "lib/libBOSSWolframEngine.so" #,schema)))
                   '(hr)
                   `(pre ,(format "~a" (syntax->datum (expand-only plan (list #'~>)))))
                   )
    ))

;function returning data on get
(define (rest-explain req operators)
  (if (equal? (last operators) "RunNativeFunction")
      (let ((plan #`(~> #,@(unflatten
                            (map (lambda (op) (read (open-input-string op)))
                                 operators)))))
        (response/full
        200
        #"OK"
        (current-seconds)
        TEXT/HTML-MIME-TYPE
        '()
        (list (string->bytes/utf-8 (jsonify (syntax->datum plan) (eval plan) ) ))
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

        
        (response/full
        200
        #"OK"
        (current-seconds)
        TEXT/HTML-MIME-TYPE
        '()
        (list (string->bytes/utf-8 (jsonify (eval schema) (eval plan) ) ))
        )

        )))

;given racket schema and data, it returns a JSON string
(define (jsonify schema data)
(string-replace 
  (string-append "["
                 (let ([json-data (format "~a"
                                          (map (lambda (record)         
                                                 (string-append "{\n"
                                                                (let ([json-record (format "~a"
                                                                                           (map (lambda (item field)
                                                                                                  (string-append "\"" (format "~a" (car field)) "\": \"" (format "~a" item) "\",\n" )
                                                                                                  )
                                                                                                record
                                                                                                schema
                                                                                                )
                                                                                           )]
                                                                      )
                                                                  (substring json-record 1 (- (string-length json-record) 3) )
                                                                  )               
                                                                "\n},\n")
                                                 )
                                               data
                                               )
                                          )]
                       )
                   (substring json-data 1 (- (string-length json-data) 3) )
                   )
                 "]" )
"$0" "_" )
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
     (li (a ((href "legacy/Customer/:/Project/:/As/Name/FirstName/Last/LastName/Age/age")) "Simple Projection Query"))
     (li (a ((href "legacy/Customer/:::/Select/:/Where/:/Equal/FirstName/\"Holger\"/:::/Group/Count")) "Simple Aggregation Query"))
     (li (a ((href "rest-example-page")) "REST example page"))
     (li (a ((href "legacy/\"libBOSSMQTTEngine.so\"/:/EvaluateInEngine/:/StartMQTTServer")) "Start MQTT (make sure the library is installed)"))
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
   [("rest" (string-arg) ...) #:method "get" rest-explain]
   [("rest-example-page") rest-example-page]
   ; api divided in "legacy" and "rest" so that racket can serve static files
   [("legacy" (string-arg) ...) explain]
   ))

(EvaluateInEngine
 "lib/libBOSSWolframEngine.so"
 (CreateTable Customer FirstName LastName age)
 (InsertInto  Customer "Holger" "German" 38)
 (InsertInto  Customer "Dude" "Englishman" (Interpolate FirstName))
 (InsertInto  Customer "Hubert" "Frenchman" 34))

; just an example XY table to play with
(EvaluateInEngine
 "lib/libBOSSWolframEngine.so"
(CreateTable NumbersTable xnumber ynumber description)
(InsertInto  NumbersTable 1 3 "desc1")
(InsertInto  NumbersTable 2 4 "desc2")
(InsertInto  NumbersTable 3 5 "desc3")
(InsertInto  NumbersTable 4 7 "desc4")
(InsertInto  NumbersTable 5 6 "desc5"))

(provide main)
(define (main)
  (serve/servlet start
                 #:stateless? #t
                 #:servlet-path "/"
                 #:servlet-regexp #rx""
                 #:listen-ip #f
                 #:command-line? #t
                 #:extra-files-paths (list (build-path "bin/htdocs"))
                 ))
