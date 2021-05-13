(Top
 (Group
  (Join
   (Project
    (Select
     'LINEITEM
     (Where (Equals 'L_RETURNFLAG "R"))
     )
    (As 'expr1013 (Multiply 'L_EXTENDEDPRICE (- 1 'L_DISCOUNT)))
    )
   (Join
    (Select
     'ORDERS
     (Where
      (Between
       (Date 'O_ORDERDATE)
       (Date "1993-10-01")
       (Date "1994-01-01")
       )
      )
     )
    (Join
     'NATION 'CUSTOMER
     (Where (Equal 'N_NATIONKEY 'C_NATIONKEY))
     )
    (Where (Equal 'O_CUSTKEY 'C_CUSTKEY))
    )
   (Where (Equal 'L_ORDERKEY 'O_ORDERKEY))
   )
  (By 'C_CUSTKEY 'C_NAME 'C_ACCTBAL 'C_PHONE 'N_NAME 'C_ADDRESS)
  (Sum 'expr1013)
  )
 (By 'expr1013)
 20
 )
