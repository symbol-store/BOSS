(Top
 (Group
  (Project
   (Join
    (Select
     'CUSTOMER
     (Where
      (Equal 'C_MKTSEGMENT "BUILDING"))
     )
    (Join
     (Select
      'ORDERS
      (Where
       (Greater
        (Date "1995-03-15")
        'O_ORDERDATE)
       )
      )
     (Select
      'LINEITEM
      (Where
       (Greater
        'L_SHIPDATE
        (Date "1995-03-22")
        )
       )
      )
     (Where (Equal 'O_ORDERKEY 'L_ORDERKEY))
     
     )
    (Where (Equal 'C_CUSTKEY 'O_CUSTKEY))
    )
   (As 'Expr1009 (Multiply 'L_EXTENDEDPRICE  (Subtract 1 'L_DISCOUNT))
       'L_ORDERKEY 'L_ORDERKEY
       'O_ORDERDATE 'O_ORDERDATE
       'O_SHIPPRIORITY 'O_SHIPPRIORITY
       )
   )
  (By 'L_ORDERKEY 'O_ORDERDATE 'O_SHIPPRIORITY)
  (Sum 'Expr1009)
  )
 (By 'Expr1009)
 10
 )
