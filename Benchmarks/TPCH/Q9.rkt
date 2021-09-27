(Order
 (Group
  (Project
   (Join
    'NATION
    (Join
     (Join
      'SUPPLIER
      (Join
       (Join
        (Select 'PART
                (Where (StringContains 'P_NAME "green")))
        'PARTSUPP
        (Where (Equal 'P_PARTKEY 'PS_PARTKEY))
        )
       'LINEITEM
       (Where
        (Equal (List 'PS_PARTKEY 'PS_SUPPKEY) (List 'L_PARTKEY 'L_SUPPKEY)))
       )
      (Where (Equal 'S_SUPPKEY 'L_SUPPKEY))
      )
     'ORDERS
     (Where (Equal 'L_ORDERKEY 'O_ORDERKEY))
     )
    (Where (Equal 'N_NATIONKEY 'S_NATIONKEY))
    )
   (As 'nation 'N_NAME
       'o_year (Year 'O_ORDERDATE)
       'amount
       (Subtract (Multiply 'L_EXTENDEDPRICE (Subtract 1 l_discount))
                 (Multiply 'PS_SUPPLYCOST 'L_QUANTITY))
       )
   )
  (By 'nation 'o_year)
  (Sum 'amount)
  )
 (By 'nation 'o_year)
 )
