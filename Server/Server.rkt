#lang racket
;; right now, you need to run it (in the directory where you have RacketBOSS.so) like this
;; racket -tm ..../Server/Server.rkt

(require threading)
(require racket/list)
(require "../Source/Shims/BOSS.rkt")
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
         web-server/dispatch)

;K: probably the way to go https://docs.racket-lang.org/web-server/templates.html
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

;K: like embed-in-page, but with external html
;K: TODO: pass the file as a string. A very hard task, indeed.
(define (embed-ext my-param)
  (response/full
  200
  #"OK"
  (current-seconds)
  TEXT/HTML-MIME-TYPE
  '()
  (list (string->bytes/utf-8
    (let ([param my-param])
    (include-template "../Server/visualPage.html"))
    ))
   )
  )
;K: like embed-in-page, but with external html (custom for MOD, TODO optimise)
(define (embed-extMOD my-param)
  (response/full
  200
  #"OK"
  (current-seconds)
  TEXT/HTML-MIME-TYPE
  '()
  (list (string->bytes/utf-8
    (let ([param my-param])
    (include-template "../Server/visualMOD.html"))
    ))
   )
  )
;K: like embed-in-page, but with external html (custom for MSTR, TODO optimise)
(define (embed-extMSTR my-param)
  (response/full
  200
  #"OK"
  (current-seconds)
  TEXT/HTML-MIME-TYPE
  '()
  (list (string->bytes/utf-8
    (let ([param my-param])
    (include-template "../Server/visualMSTR.html"))
    ))
   )
  )

;K: a very cool new explain function
(define (visual-explain req operators)
  (if (equal? (last operators) "RunNativeFunction")
      (let ((plan #`(~> #,@(unflatten
                            (map (lambda (op) (read (open-input-string op)))
                                 operators)))))
        (embed-in-page '(h1 "Visual Result")
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

        
        ;(embed-ext "foo")
        (embed-ext (format "~v" (eval plan)))

        )))

;K: custom explain for MOD table
(define (visual-explainMOD req operators)
  (if (equal? (last operators) "RunNativeFunction")
      (let ((plan #`(~> #,@(unflatten
                            (map (lambda (op) (read (open-input-string op)))
                                 operators)))))
        (embed-in-page '(h1 "Visual Result")
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

        
        ;(embed-ext "foo")
        (embed-extMOD (format "~v" (eval plan)))

        )))


;K: custom explain for MSTR table
(define (visual-explainMSTR req operators)
  (if (equal? (last operators) "RunNativeFunction")
      (let ((plan #`(~> #,@(unflatten
                            (map (lambda (op) (read (open-input-string op)))
                                 operators)))))
        (embed-in-page '(h1 "Visual Result")
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

        
        ;(embed-ext "foo")
        (embed-extMSTR (format "~v" (eval plan)))

        )))

;K: explain funtion returning data on get
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
        (list (string->bytes/utf-8 (string-append (format "~a" (syntax->datum plan)) ";" (format "~a" (eval plan)) ) ))
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

        
        ;(embed-ext "foo")
        (response/full
        200
        #"OK"
        (current-seconds)
        TEXT/HTML-MIME-TYPE
        '()
        ;;; (list (string->bytes/utf-8 (string-append (format "~a" (eval schema)) ";" (format "~a" (eval plan)) ) ))
        ; (list (string->bytes/utf-8 (jsonify (format "~a" (eval schema)) (format "~a" (eval plan)) ) ))
        (list (string->bytes/utf-8 (jsonify (eval schema) (eval plan) ) ))
        )

        )))

;K: jsonifier
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

;old version (not json)
;;; (define (jsonify schema data)
;;;   (string-append schema ";" data)
;;;   )

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
     ; K: just an example page to play with
     (li (a ((href "kpage")) "K Page"))
     ))
  )

; K: just an example page to play with
(define (kpage req)
  (embed-in-page
   '(h1 "K Page")
   "This is K Page!"
   '(list
     (li (a ((href "NumbersTable/:/Project/:/As/x/xnumber/y/ynumber/desc/description")) "The Numbers Table"))
     (li (a ((href "Customer/:/Project/:/As/Name/FirstName/Last/LastName/Age/age")) "The Customer Table"))
     (li (a ((href "visual/NumbersTable/:/Project/:/As/x/xnumber/y/ynumber/desc/description")) "The Visual Numbers Table"))
     )
   '(h1 "Brill Data")
   '(list
     (li (a ((href "BatteryDataMOD/:/Project/:/As/id/id/modId/modId/battId/battId/groupId/groupId/ts/ts/VBat_V/VBat_V/IBat_A/IBat_A/TBat_degC/TBat_degC/TPwr_degC/TPwr_degC/RBat_ohm/RBat_ohm/currentThroughput_kAh/currentThroughput_kAh/currentThroughput_Ah/currentThroughput_Ah/energyThroughput_kWh/energyThroughput_kWh/energyThroughput_Wh/energyThroughput_Wh/PBat_W/PBat_W/soh_pct/soh_pct/soc_pct/soc_pct/capacity_Ah/capacity_Ah/energyCapacityWh/energyCapacityWh/Rbat_ohm/Rbat_ohm")) "MOD Table"))
     (li (a ((href "visualMOD/BatteryDataMOD/:/Project/:/As/id/id/modId/modId/battId/battId/groupId/groupId/ts/ts/VBat_V/VBat_V/IBat_A/IBat_A/TBat_degC/TBat_degC/TPwr_degC/TPwr_degC/RBat_ohm/RBat_ohm/currentThroughput_kAh/currentThroughput_kAh/currentThroughput_Ah/currentThroughput_Ah/energyThroughput_kWh/energyThroughput_kWh/energyThroughput_Wh/energyThroughput_Wh/PBat_W/PBat_W/soh_pct/soh_pct/soc_pct/soc_pct/capacity_Ah/capacity_Ah/energyCapacityWh/energyCapacityWh/Rbat_ohm/Rbat_ohm")) "visual MOD Table"))
     (li (a ((href "BatteryDataMSTR/:/Project/:/As/id/id/battId/battId/groupId/groupId/ts/ts/Vsys_V/Vsys_V/Vsysout_V/Vsysout_V/Isys_A/Isys_A/Psys_W/Psys_W/soh_pct/soh_pct/soc_pct/soc_pct")) "MSTR Table"))
     (li (a ((href "visualMSTR/BatteryDataMSTR/:/Project/:/As/id/id/battId/battId/groupId/groupId/ts/ts/Vsys_V/Vsys_V/Vsysout_V/Vsysout_V/Isys_A/Isys_A/Psys_W/Psys_W/soh_pct/soh_pct/soc_pct/soc_pct")) "visual MSTR Table"))
     )
   '(h1 "Brill Data w/ REST interface")
   '(list
     (li (a ((href "rest-feeded-page")) "REST feeded page"))
     )
  )
)

; K: a rest feeded page
(define (rest-feeded-page req)
 (response/full
  200
  #"OK"
  (current-seconds)
  TEXT/HTML-MIME-TYPE
  '()
  (list (string->bytes/utf-8
    (include-template "../Server/visualRestPage.html")
    ))
 )
)

(define-values (start route-url)
  (dispatch-rules
   [("") index]
   ; K: just an example page to play with
   [("kpage") kpage]
   ; K: visual/something will evaluate as explain, but with visualisation component
   [("visual" (string-arg) ...) visual-explain]
   ; K: visual functions and page, custom for brill table (TODO optimise)
   [("visualMOD" (string-arg) ...) visual-explainMOD]
   [("visualMSTR" (string-arg) ...) visual-explainMSTR]
   ; K: REST interface to BOSS (https://lisp.sh/crud-web-api-in-racket/)
   [("rest" (string-arg) ...) #:method "get" rest-explain]
   ; K: REST feeded page
   [("rest-feeded-page") rest-feeded-page]
   [((string-arg) ...) explain]
   ))

(CreateTable Customer FirstName LastName age)
(InsertInto  Customer "Holger" "German" 38)
(InsertInto  Customer "Dude" "Englishman" (Interpolate FirstName))
(InsertInto  Customer "Hubert" "Frenchman" 34)


(provide main)
(define (main)
  (serve/servlet start
                 #:stateless? #t
                 #:servlet-path "/"
                 #:servlet-regexp #rx""
                 #:listen-ip #f
                 #:command-line? #t
                 ))
