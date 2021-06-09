import string

# This file generates the data sets used by the compile time benchmarks
with open(f"Compile1Partition.csv", "w") as f:
    f.write(",".join(list(string.ascii_uppercase)))
    f.write('\n')
    f.write(",".join(map(str, list(range(0, 26)))))

with open(f"Compile5Partition.csv", "w") as f:
    f.write(",".join(list(string.ascii_uppercase)))
    for symbolNumber in range(0, 5):
        f.write('\n')
        f.write(",".join(map(str, list(range(0, 25)))))
        f.write(f",Ssym{symbolNumber}")

with open(f"Compile50Partition.csv", "w") as f:
    f.write(",".join(list(string.ascii_uppercase)))
    for symbolNumber in range(0, 50):
        f.write('\n')
        f.write(",".join(map(str, list(range(0, 25)))))
        f.write(f",Ssym{symbolNumber}")

with open(f"Compile100Partition.csv", "w") as f:
    f.write(",".join(list(string.ascii_uppercase)))
    for symbolNumber in range(0, 100):
        f.write('\n')
        f.write(",".join(map(str, list(range(0, 25)))))
        f.write(f",Ssym{symbolNumber}")

with open(f"Compile150Partition.csv", "w") as f:
    f.write(",".join(list(string.ascii_uppercase)))
    for symbolNumber in range(0, 150):
        f.write('\n')
        f.write(",".join(map(str, list(range(0, 25)))))
        f.write(f",Ssym{symbolNumber}")