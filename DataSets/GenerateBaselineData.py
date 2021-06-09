from random import randint
from random import uniform

# Integer Dataset
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            f.write(f"{randint(-500,500)},{randint(50,100)}\n")

# Dataset for benchmark queries
for x in [5, 50, 500, 5000, 50000, 500000, 1000000]:
    with open(f"ComparisonBenchmark{x}.csv", "w") as f:
        f.write("A,B,C\n")
        for i in range(0, x):
            f.write(f"{randint(-500,500)},{randint(-100,100)},{randint(-100,100)}\n")

# Dataset for benchmark queries
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"ComparisonBenchmarkQ3-{x}.csv", "w") as f:
        f.write("A,B,C\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.25:
                f.write(f"{randint(-500,500)},Sx,{randint(-100,100)}\n")
            elif uniform(0, 1) < 0.5:
                f.write(f"{randint(-500,500)},Sy,{randint(-100,100)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(-100,100)},{randint(-100,100)}\n")

# Integer Dataset with Addition 5%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}-0.05Add.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.05:
                f.write(f"ePlus {randint(-500,500)} {randint(-500,500)},{randint(50,100)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")

# Integer Dataset with addition 95%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}-0.95Add.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.95:
                f.write(f"ePlus {randint(-500,500)} {randint(-500,500)},{randint(50,100)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")

# Integer Dataset with unknown symbols 95%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}-0.95UnknownSymbol.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.95:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")
                f.write(f"Sunknown,{randint(50,100)}\n")
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")

# Integer Dataset with unknown symbols 5%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}-0.05UnknownSymbol.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.05:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")
                f.write(f"Sunknown,{randint(50,100)}\n")
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")


# Integer Dataset with NextValue symbols 95%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}-0.95NextValue.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.95:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")
                f.write(f"eNextValue 1 SInt,{randint(50,100)}\n")
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")

# Integer Dataset with unknown symbols 5%
for x in [5, 50, 500, 5000, 50000, 500000]:
    with open(f"IntegerDataset{x}-0.05NextValue.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            if uniform(0, 1) < 0.05:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")
                f.write(f"eNextValue 1 SInt,{randint(50,100)}\n")
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(50,100)}\n")