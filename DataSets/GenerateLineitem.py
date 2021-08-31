import time
import datetime
import math
import random

for size in [16777216]:
    with open("lineitem.tbl", "r") as f:
        with open("lineitem-csv-" + str(size) + ".csv", "w+") as g:
            header = "LINEITEM,L_ORDERKEY,L_PARTKEY,L_SUPPKEY,L_LINENUMBER,L_QUANTITY,L_EXTENDEDPRICE,L_DISCOUNT,L_TAX,L_RETURNFLAG,L_LINESTATUS,L_SHIPDATE,L_COMMITDATE,L_RECEIPTDATE,L_SHIPINSTRUCT,L_SHIPMODE,L_COMMENT,DummyField\n"

            g.write("LINEITEM,L_ORDERKEY,L_PARTKEY,L_SUPPKEY,L_LINENUMBER,L_QUANTITY,L_EXTENDEDPRICE,L_DISCOUNT,L_TAX,L_RETURNFLAG,L_LINESTATUS,L_SHIPDATE,L_COMMITDATE,L_RECEIPTDATE,L_SHIPINSTRUCT,L_SHIPMODE,L_COMMENT,DummyField\n")
            for i in range(size):
                line = f.readline().rstrip()
                splitline = line.split("|")

                splitline = [item.replace(",", "") for item in splitline]

                splitline.append("1\n")

                splitline[5] = str(math.floor(float(splitline[5])))
                splitline[6] = str(math.floor(float(splitline[6])))
                splitline[7] = str(math.floor(100 * float(splitline[7])))

                splitline[8] = "s" + splitline[8]
                splitline[9] = "s" + splitline[9]

                splitline[10] = str(int(time.mktime(datetime.datetime.strptime(splitline[10], "%Y-%m-%d").timetuple())))
                splitline[11] = str(int(time.mktime(datetime.datetime.strptime(splitline[11], "%Y-%m-%d").timetuple())))
                splitline[12] = str(int(time.mktime(datetime.datetime.strptime(splitline[12], "%Y-%m-%d").timetuple())))

                splitline[13] = "s" + splitline[13]
                splitline[14] = "s" + splitline[14]
                splitline[15] = "s" + splitline[15]

                newline = ",".join(splitline)
                g.write(newline)
