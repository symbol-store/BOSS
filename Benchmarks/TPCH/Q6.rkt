(Group
 (Project
  (Select LINEITEM
          (Where (And (Greater L_QUANTITY 25)
                      (Greater L_DISCOUNT 0.03)
                      (Greater 0.10 L_DISCOUNT)
                      (Greater (DateObject "1998-01-01") L_SHIPDATE)
                      (Greater L_SHIPDATE (DateObject "1996-03-08"))
                      )))
  (As revenue (Times L_EXTENDEDPRICE L_DISCOUNT)))
 (Sum revenue)
 )
