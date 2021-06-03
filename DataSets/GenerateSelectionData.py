from random import randint
from random import uniform

# Integer Dataset, selectivity 1%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}Selectivity1.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.01:
                f.write(f"{0},{randint(50,100)}\n")
            else:
                f.write(f"{randint(1,100)},{randint(50,100)}\n")

# Integer Dataset, selectivity 50%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}Selectivity50.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.5:
                f.write(f"{0},{randint(50,100)}\n")
            else:
                f.write(f"{randint(1,100)},{randint(50,100)}\n")

# Integer Dataset, selectivity 50%, sorted
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}Selectivity50Sorted.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if i < (x / 2):
                f.write(f"{0},{randint(50,100)}\n")
            else:
                f.write(f"{randint(1,100)},{randint(50,100)}\n")

# Integer Dataset, selectivity 5%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}Selectivity5.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.05:
                f.write(f"{0},{randint(50,100)}\n")
            else:
                f.write(f"{randint(1,100)},{randint(50,100)}\n")

# Integer Dataset, selectivity 95%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}Selectivity95.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.95:
                f.write(f"{0},{randint(50,100)}\n")
            else:
                f.write(f"{randint(1,100)},{randint(50,100)}\n")