from random import randint
from random import uniform
from random import choice
import string

strings = ["Hello", "World", "Foo", "Bar", "Baz"]

for x in [5, 50, 500, 5000, 50000, 500000]:
    stringLen = 6
    with open(f"StringDataset{x}.csv", "w") as f:
        f.write("A,B,C\n")
        for i in range(0, x):
            randString = strings[randint(0, len(strings) - 1)]
            f.write(f"{randint(-500,500)},{randint(-1000,1000)},s{randString}\n")
