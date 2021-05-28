(GroupBy
 (Project
  (Select Lineitem
          (Where (And (Greater QUANTITY 25)
                      (Greater DISCOUNT 3)
                      (Greater 10 DISCOUNT)
                      (Greater (UnixTime "1998-01-01") SHIPDATE)
                      (Greater SHIPDATE (UnixTime "1996-03-08"))
                      )))
  (As revenue (Times EXTENDEDPRICE DISCOUNT)))
 (Sum revenue)
 )
