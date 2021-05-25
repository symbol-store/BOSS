(load "Setup.rkt")

(println

(Order
	(Group
		(Project
			(Select
				Lineitem
				(Where (Greater (UnixTime "1998-12-01") SHIPDATE)))
			(As
				"l_returnflag" RETURNFLAG
				"l_linestatus" LINESTATUS
				"qty" QUANTITY
				"base_price" EXTENDEDPRICE
				"disc_price" (Times EXTENDEDPRICE (Plus 1 (Times -1 DISCOUNT)))
				"sum_charge" (Times EXTENDEDPRICE (Plus 1 (Times -1 DISCOUNT) (Plus 1 TAX)))
				"disc" DISCOUNT))
		(By "l_returnflag" "l_linestatus")
		(Total "qty")
		(Total "base_price")
		(Total "disc_price")
		(Total "sum_charge")
		(Mean "qty")
		(Mean "base_price")
		(Mean "disc")
		Count)
	(By "l_returnflag" "l_linestatus"))

)