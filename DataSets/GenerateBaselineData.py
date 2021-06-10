from random import randint
from random import uniform

# Integer Dataset
for x in [5, 50, 500, 5000, 50000, 100000, 500000, 1000000, 5000000]:
    with open(f"IntegerDataset{x}.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, x):
            f.write(f"{randint(-500,500)},{randint(-500,500)}\n")

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


# Integer Dataset varying number of add symbols
for x in [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]:
    with open(f"VaryingAdd500k-{x * 100}.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, 500000):
            if uniform(0, 1) < x:
                f.write(f"ePlus {randint(-500,500)} {randint(-500,500)},{randint(-500,500)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(-500,500)}\n")

# Integer Dataset varying number of add symbols
for x in [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]:
    with open(f"VaryingAdd500k-{x * 100}.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, 500000):
            if uniform(0, 1) < x:
                f.write(f"ePlus {randint(-500,500)} {randint(-500,500)},{randint(-500,500)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(-500,500)}\n")

# Integer Dataset varying number of nextvalue symbols
for x in [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]:
    with open(f"VaryingNextValue500k-{x * 100}.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, 250000):
            if uniform(0, 1) < x:
                f.write(f"eNextValue 1 SInt,{randint(-500,500)}\n")
                f.write(f"{randint(-500,500)},{randint(-500,500)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(-500,500)}\n")
                f.write(f"{randint(-500,500)},{randint(-500,500)}\n")

# Integer Dataset varying number of undefined symbols
for x in [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]:
    with open(f"VaryingUndef500k-{x * 100}.csv", "w") as f:
        f.write("A,B\n")
        for i in range(0, 250000):
            if uniform(0, 1) < x:
                f.write(f"Sundef,{randint(-500,500)}\n")
                f.write(f"{randint(-500,500)},{randint(-500,500)}\n")
            else:
                f.write(f"{randint(-500,500)},{randint(-500,500)}\n")
                f.write(f"{randint(-500,500)},{randint(-500,500)}\n")
