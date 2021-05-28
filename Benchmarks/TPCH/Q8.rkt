(Order
 (Project
  (Group
   (Project
    (Join
     (ProjectAll 'NATION 'N2)
     (Join
      'SUPPLIER
      (Join
       (Select 'PART (Where (Equal 'P_TYPE "ECONOMY ANODIZED STEEL")))
       (Join
        (Join
         (Join
          (Join (Select 'REGION (Where (RNAME Equal "AMERICA")))
                'NATION
                (Where (Equal 'R_REGIONKEY 'N_REGIONKEY)))
          'CUSTOMER
          (Where (Equal 'C_NATIONKEY 'N_NATIONKEY))
          )
         (Project
          (Select 'ORDERS
                  (Where (Between
                          (Date 'O_ORDERDATE)
                          (Date "1995-01-01")
                          (Date "1996-12-31")))
                  )
          (As 'orderdate (Date 'O_ORDERDATE))
          )
         (Where (Equal 'C_CUSTKEY 'O_CUSTKEY)) )
        (Project 'LINEITEM
                 (As 'price (Multiply 'L_EXTENDEDPRICE (Minus 1 'L_DISCOUNT))))
        (Where (Equal 'O_ORDERKEY 'L_ORDERKEY))
        )
       (Where (Equal 'P_PARTKEY 'L_PARTKEY))
       )
      (Where (Equal 'S_SUPPKEY 'L_SUPPKEY))
      )
     (Where (Equal 'N2.N_NATIONKEY 'S_NATIONKEY))
     )
    (As 'brazil
        (If (Equal 'N2.N_NAME "BRAZIL") (Multiply 'L_EXTENDEDPRICE (Minus 1 'L_DISCOUNT)) 0))
    )
   (By 'orderdate)
   (Sum 'brazil)
   (Sum 'price)
   )
  (As
   'groups 'orderdate
   'result (Divide 'brazil 'price)
   )
  )
 (By 'groups)
 )
