from random import randint
from random import uniform
from random import choice
import string
import csv

days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]

covid_cases = []

# read real covid data
with open("covid-data.csv", "r") as f:
    reader = csv.reader(f, delimiter=",")
    next(reader, None)
    for row in reader:
        covid_cases.append(int(row[4]))

covid_cases = list(reversed(covid_cases))


# Write the dataset with symbols
with open(f"DiseaseDataset.csv", "w") as f:
    f.write("DaysSinceStart,DayOfWeek,NewCasesToday,CumulativeCases\n")
    cases_total = 0
    for i in range(0, 365):
        dayOfWeek = days[i % 7]
        casesToday = covid_cases[i]
        casesTotal = 0

        if dayOfWeek == "Sat":
            f.write(f"{i},s{dayOfWeek},SsaturdayVal,{cases_total}\n")
        elif dayOfWeek == "Sun":
            f.write(f"{i},s{dayOfWeek},SsundayVal,{cases_total}\n")
        else:
            cases_total += casesToday
            f.write(f"{i},s{dayOfWeek},{casesToday},{cases_total}\n")

with open(f"DiseaseDatasetNoSyms.csv", "w") as f:
    f.write("DaysSinceStart,DayOfWeek,NewCasesToday,CumulativeCases\n")
    cases_total = 0
    for i in range(0, 365):
        dayOfWeek = days[i % 7]
        casesToday = covid_cases[i]
        casesTotal = 0

        if dayOfWeek == "Sat":
            f.write(f"{i},s{dayOfWeek},0,{cases_total}\n")
        elif dayOfWeek == "Sun":
            f.write(f"{i},s{dayOfWeek},0,{cases_total}\n")
        else:
            cases_total += casesToday
            f.write(f"{i},s{dayOfWeek},{casesToday},{cases_total}\n")
