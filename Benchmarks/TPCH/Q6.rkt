(Group
 (Project
  (Select LINEITEM
          (Where (And (Greater L_QUANTITY 25)
                      (Greater L_DISCOUNT 0.03)
                      (Greater 0.10 L_DISCOUNT)
                      (Greater (UnixTime "1998-01-01") SHIPDATE)
                      (Greater SHIPDATE (UnixTime "1996-03-08"))
                      )))
  (As revenue (Times L_EXTENDEDPRICE L_DISCOUNT)))
 (Sum revenue)
 )
