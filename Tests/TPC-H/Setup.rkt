(load-extension "../../Release/RacketBOSS.so")

(eval
 '((lambda () ;; run in dynamic scope (as boss definitions are dynamically loaded)
     (CreateTable Customer FirstName LastName)
     (InsertInto  Customer "Holger" "Pirk")

     (CreateTable Part PARTKEY TYPE SIZE BRAND NAME CONTAINER MFGR RETAILPRICE COMMENT)
     (InsertInto Part 1 "LARGE PLATED TIN" 31 "Brand#43" "burlywood plum powder puff mint" "LG BAG" "Manufacturer#4" 901.0000 "blithely busy reque")
     (InsertInto Part 2 "LARGE POLISHED STEEL" 4 "Brand#55" "hot spring dodger dim light" "LG CASE" "Manufacturer#5" 902.0000 "even ironic requests s")
     (InsertInto Part 3 "STANDARD PLATED COPPER" 30 "Brand#53" "dark slate grey steel misty" "WRAP CASE" "Manufacturer#5" 903.0000 "slyly ironic fox")

     (CreateTable Supplier SUPPKEY NATIONKEY COMMENT NAME ADDRESS PHONE ACCTBAL)
     (InsertInto Supplier 1 13 "blithely final pearls are. instructions thra" "Supplier#000000001" "wWs4pnykQOFl8mgVCU8EZMXqZs1w" "800-807-9579" 3082.00)
     (InsertInto Supplier 2 5 "requests integrate fluffily. fluffily ironic deposits wake. bold" "Supplier#000000002" "WkXT6MSAJrp4qWq3W9N" "348-617-6055" 3009.00)
     (InsertInto Supplier 3 22 "carefully express ideas shall have to unwin" "Supplier#000000003" "KjUqa42JEHaRDVQTHV6Yq2h" "471-986-9888" 9159.00)
     ; TODO: floats
     ; new-ComplexExpression: expects type <Expression *> as 2nd argument, given: '(Supplier 1 13 "blithely ... "800-807-9579" unknown); other arguments were: 'InsertInto
     ; (InsertInto Supplier 1 13 "blithely final pearls are. instructions thra" "Supplier#000000001" "wWs4pnykQOFl8mgVCU8EZMXqZs1w" "800-807-9579" 3082.8600)
     ; (InsertInto Supplier 2 5 "requests integrate fluffily. fluffily ironic deposits wake. bold" "Supplier#000000002" "WkXT6MSAJrp4qWq3W9N" "348-617-6055" 3009.7300)
     ; (InsertInto Supplier 3 22 "carefully express ideas shall have to unwin" "Supplier#000000003" "KjUqa42JEHaRDVQTHV6Yq2h" "471-986-9888" 9159.7800)

     (CreateTable Lineitem
                  ORDERKEY
                  PARTKEY
                  SUPPKEY
                  LINENUMBER
                  QUANTITY
                  EXTENDEDPRICE
                  DISCOUNT
                  TAX
                  RETURNFLAG
                  LINESTATUS
                  SHIPDATE
                  COMMITDATE
                  RECEIPTDATE
                  SHIPINSTRUCT
                  SHIPMODE
                  COMMENT
                  )
     (InsertInto Lineitem 1 1552 93 1 17 2471035 004 002 "N" "O"
                 (UnixTime "1996-03-13") (UnixTime "1996-02-12") (UnixTime "1996-03-22")
                 "DELIVER IN PERSON" "TRUCK"   "egular courts above the")
     (InsertInto Lineitem 1 674  75 2 36 5668812 009 006 "N" "O"
                 (UnixTime "1996-04-12") (UnixTime "1996-02-28") (UnixTime "1996-04-20")
                 "TAKE BACK RETURN"  "MAIL"    "ly final dependencies: slyly bold")
     (InsertInto Lineitem 1 637  38 3 8  1230104 010 002 "N" "O"
                 (UnixTime "1996-01-29") (UnixTime "1996-03-05") (UnixTime "1996-01-31")
                 "TAKE BACK RETURN"  "REG AIR" "riously. regular express dep")

     (CreateTable Partsupp PARTKEY SUPPKEY SUPPLYCOST AVAILQTY COMMENT)
     (InsertInto Partsupp 1 2 400.00 1111 "carefully ironic deposits use against the carefully unusual accounts. slyly silent platelets nag quickly even")
     (InsertInto Partsupp 1 2502 702.00 3999 "slyly regular accounts serve carefully. asymptotes after the slyly even instructions cajole quickly ironic requests. pending dugouts about the slyly ")
     (InsertInto Partsupp 1 5002 383.00 7411 "carefully special ideas are slyly. slyly ironic epitaphs use pending pending foxes. furiously express pinto beans lose quiet even requests: special final packages ar")
     ; TODO: floats (see above)
     ; (InsertInto Partsupp 1 2 400.7500 1111 "carefully ironic deposits use against the carefully unusual accounts. slyly silent platelets nag quickly even")
     ; (InsertInto Partsupp 1 2502 702.6100 3999 "slyly regular accounts serve carefully. asymptotes after the slyly even instructions cajole quickly ironic requests. pending dugouts about the slyly ")
     ; (InsertInto Partsupp 1 5002 383.9500 7411 "carefully special ideas are slyly. slyly ironic epitaphs use pending pending foxes. furiously express pinto beans lose quiet even requests: special final packages ar")

     (CreateTable Orders ORDERDATE ORDERKEY CUSTKEY ORDERPRIORITY SHIPPRIORITY CLERK ORDERSTATUS TOTALPRICE COMMENT)
     (InsertInto Orders "1995-04-19" 1 73100 "4-NOT SPECIFIED" 0 "Clerk#000000916" "P" 203198.00 "final packages sleep blithely packa")
     (InsertInto Orders "1996-11-04" 2 92861 "1-URGENT" 0 "Clerk#000000373" "O" 317719.00 "final excuses about the ironic even deposits detect express request")
     (InsertInto Orders "1992-02-15" 3 44875 "1-URGENT" 0 "Clerk#000000485" "F" 146674.00 "final final deposits cajole foxes. blithely pendin")
     ; (InsertInto Orders "1995-04-19" 1 73100 "4-NOT SPECIFIED" 0 "Clerk#000000916" "P" 203198.5600 "final packages sleep blithely packa")
     ; (InsertInto Orders "1996-11-04" 2 92861 "1-URGENT" 0 "Clerk#000000373" "O" 317719.9900 "final excuses about the ironic even deposits detect express request")
     ; (InsertInto Orders "1992-02-15" 3 44875 "1-URGENT" 0 "Clerk#000000485" "F" 146674.9800 "final final deposits cajole foxes. blithely pendin")

     (CreateTable Nation NATIONKEY NAME REGIONKEY COMMENT)
     (InsertInto Nation 0 "ALGERIA" 0 "slyly express pinto beans cajole idly. deposits use blithely unusual packages? fluffily final accounts x-r")
     (InsertInto Nation 1 "ARGENTINA" 1 "instructions detect blithely stealthily pending packages")
     (InsertInto Nation 2 "BRAZIL" 1 "blithely unusual deposits are quickly--")
     )))