(load "Setup.rkt")

(println

(Order
	(Group
		(Project
			(Join
				(Join
					(Join
						(Join
							(Join
								Supplier Lineitem
								(Where (Equals S_SUPPKEY L_SUPPKEY)))
							Partsupp
							(Where (And (Equals PS_SUPPKEY L_SUPPKEY) (Equals PS_PARTKEY L_PARTKEY))))
						Part
						(Where (And (Equals P_PARTKEY L_PARTKEY) (StringMatchesQ P_NAME "green"))))
					Orders
					(Where (Equals O_ORDERKEY L_ORDERKEY)))
				Nation
				(Where (Equals S_NATIONKEY N_NATIONKEY)))
			(As
				"nation" N_NAME
				"o_year" (Date O_ORDERDATE)
				"amount" (Plus (Multiply L_EXTENDEDPRICE (Plus 1 (Multiply -1 L_DISCOUNT))) (Multiply -1 PS_SUPPLYCOST L_QUANTITY))))
		(By "nation" "o_year")
		(Total "amount"))
	(By "nation" "o_year"))

)