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
         web-server/dispatch
         ; K: needed to dispatch files
         web-server/configuration/responders)

;K: racket component to embed html pages https://docs.racket-lang.org/web-server/templates.html
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
     )
   '(h1 "Brill Data")
   '(list
     (li (a ((href "BatteryDataMOD/:/Project/:/As/id/id/modId/modId/battId/battId/groupId/groupId/ts/ts/VBat_V/VBat_V/IBat_A/IBat_A/TBat_degC/TBat_degC/TPwr_degC/TPwr_degC/RBat_ohm/RBat_ohm/currentThroughput_kAh/currentThroughput_kAh/currentThroughput_Ah/currentThroughput_Ah/energyThroughput_kWh/energyThroughput_kWh/energyThroughput_Wh/energyThroughput_Wh/PBat_W/PBat_W/soh_pct/soh_pct/soc_pct/soc_pct/capacity_Ah/capacity_Ah/energyCapacityWh/energyCapacityWh/Rbat_ohm/Rbat_ohm")) "MOD Table"))
     (li (a ((href "BatteryDataMSTR/:/Project/:/As/id/id/battId/battId/groupId/groupId/ts/ts/Vsys_V/Vsys_V/Vsysout_V/Vsysout_V/Isys_A/Isys_A/Psys_W/Psys_W/soh_pct/soh_pct/soc_pct/soc_pct")) "MSTR Table"))
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
   ; K: REST interface to BOSS (https://lisp.sh/crud-web-api-in-racket/)
   [("rest" (string-arg) ...) #:method "get" rest-explain]
   ; K: REST feeded page
   [("rest-feeded-page") rest-feeded-page]
   ; K: static file dispatch
   ; files have to be in /usr/share/racket/pkgs/web-server-lib/web-server/default-web-root/htdocs
   [("d3.min.js") (λ (_) (file-response 200 #"OK" "d3.min.js"))]
   [((string-arg) ...) explain]
   ))

(CreateTable Customer FirstName LastName age)
(InsertInto  Customer "Holger" "German" 38)
(InsertInto  Customer "Dude" "Englishman" (Interpolate FirstName))
(InsertInto  Customer "Hubert" "Frenchman" 34)
(InsertInto  Customer "Andrea" "Italianman" 32)

; K: just an example table to play with, full of numbers
(CreateTable NumbersTable xnumber ynumber description)
(InsertInto  NumbersTable 1 3 "desc1")
(InsertInto  NumbersTable 2 4 "desc2")
(InsertInto  NumbersTable 3 5 "desc3")
(InsertInto  NumbersTable 4 7 "desc4")
(InsertInto  NumbersTable 5 6 "desc5")

;K: create tables for brill data
(CreateTable BatteryDataMOD id modId battId groupId ts VBat_V IBat_A
        TBat_degC TPwr_degC RBat_ohm currentThroughput_kAh currentThroughput_Ah
        energyThroughput_kWh energyThroughput_Wh PBat_W soh_pct soc_pct
        capacity_Ah energyCapacityWh Rbat_ohm)
(CreateTable BatteryDataMSTR id battId
        groupId ts Vsys_V Vsysout_V Isys_A Psys_W soh_pct soc_pct)

;K: insert some example brill data (MOD table)
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439942" 1 "bat00001" 1 1621439942 4.098275703625976 -7.152663230895996 24.216670989990234 28.307552337646484 0.0031040541362017393 0 273.6097106933594 1 13.611668586730957 -29.31358593539994 100.0 96.89697027206421 9.6 34.56 0.009619786217808724)
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439945" 1 "bat00001" 1 1621439945 4.014287340264566 -7.208133697509766 24.132081985473633 28.248552322387695 0.0030578826554119587 0 273.6712341308594 1 13.86682415008545 -28.93551984884787 100.0 96.96139693260193 9.6 34.56 0.009661147557199002)
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439951" 1 "bat00001" 1 1621439951 3.971659462638643 -7.21223783493042 24.132972717285156 28.180339813232422 0.0030844255816191435 0 273.793212890625 1 14.37280559539795 -28.64455264390184 100.0 97.0033586025238 9.6 34.56 0.009741058107465506)
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439958" 1 "bat00001" 1 1621439958 4.149831990390193 -6.250072956085205 24.167692184448242 28.06363296508789 0.0032424400560557842 0 273.9148254394531 0 14.877556800842285 -25.936752695434983 100.0 96.96428775787354 9.6 34.56 0.009839177504181862)
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439961" 1 "bat00001" 1 1621439961 4.020162001717279 -6.206937789916992 24.207721710205078 28.03136444091797 0.0032291263341903687 0 273.97711181640625 1 15.136006355285645 -24.95289545004732 100.0 96.97486162185669 9.6 34.56 0.0)
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439986" 1 "bat00001" 1 1621439986 4.062847458068497 -6.113036632537842 24.115488052368164 27.81932258605957 0.0032181928399950266 0 0 1 16.870643615722656 -24.836335343585972 100.0 97.24040031433105 9.6 34.56 0.01024849433451891)
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439989" 1 "bat00001" 1 1621439989 4.058658562057062 -6.203075885772705 24.098249435424805 27.785268783569336 0.0031562112271785736 0 274.4566345214844 1 17.126771926879883 -25.17616705488108 100.0 97.27863669395447 9.6 34.56 0.010294723045080901)
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439992" 1 "bat00001" 1 1621439992 4.025677227793033 -6.155654430389404 24.098325729370117 27.76317596435547 0.003184996545314789 0 274.5015869140625 1 17.31338882446289 -24.78067786258192 100.0 97.26682305335999 9.6 34.56 0.01032440084964037 )
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439996" 1 "bat00001" 1 1621439996 4.025685272728989 -6.206934452056885 24.12013053894043 27.743240356445312 0.0032127187587320805 0 274.554931640625 1 17.535158157348633 -24.987164612439578 100.0 97.33890891075134 9.6 34.56 0.010357930138707162)
;;; (InsertInto  BatteryDataMOD "B0002-M001-1621439999" 1 "bat00001" 1 1621439999 4.070777118517901 -6.054783821105957 24.06668472290039 27.72107696533203 0.0031947374809533358 0 274.6173400878906 1 17.794403076171875 -24.647675436530513 100.0 97.37784266471863 9.6 34.56 0.010397192649543285)

;K: insert some example brill data (MSTR table)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446643" "bat00001" 1 1621446643 35.67003698989318 0.27448258832524697 -1.6647907495498657 -59.383147616875696 100.0 93.29416751861572)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446664" "bat00001" 1 1621446664 36.03329023151762 36.73265998320935 -0.7469757199287415 -26.91599291208916 100.0 93.27582120895386)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446674" "bat00001" 1 1621446674 36.282745629012986 36.44713615417605 -0.4673474431037903 -16.95664839850444 100.0 93.28009486198425)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446686" "bat00001" 1 1621446686 36.18495342187054 0.8744733514122697 -0.42233923077583313 -15.282325393852155 100.0 93.2604432106018)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446696" "bat00001" 1 1621446696 36.229273357498606 36.34494532524042 -0.46644049882888794 -16.89880033707979 100.0 93.25528144836426)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446707" "bat00001" 1 1621446707 36.28089801993667 36.3291625401902 -0.7168140411376953 -26.006657125775412 100.0 87.78356313705444)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446717" "bat00001" 1 1621446717 36.30690373967116 36.34194635260533 96.96870422363281 3520.6334100080803 100.0 86.30366921424866)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446728" "bat00001" 1 1621446728 36.363941036114 36.390747618356066 49.03596115112305 1783.1407999486153 100.0 81.71635270118713)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446738" "bat00001" 1 1621446738 36.31385012600392 36.31958610635722 78.45291900634766 2848.9275427440334 100.0 78.21718454360962)
;;; (InsertInto  BatteryDataMSTR "B0002-MSTR-1621446749" "bat00001" 1 1621446749 36.285769899625315 36.33604640023004 91.23709106445312 3310.608092675907 100.0 80.75348734855652)

(provide main)
(define (main)
  (serve/servlet start
                 #:stateless? #t
                 #:servlet-path "/"
                 #:servlet-regexp #rx""
                 #:listen-ip #f
                 #:command-line? #t
                 ))
