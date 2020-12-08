#!/usr/bin/env python

import sys
sys.path.insert(0, './build')

import BOSS
from BOSS import evaluate
from BOSS import Expression as Expr
from BOSS import Symbol

def TestExpressions():
    print(BOSS.currentEngine())
    BOSS.setEngine(BOSS.WolframEngine)
    print(BOSS.currentEngine())
    BOSS.setEngine(BOSS.BulkEngine)
    print(BOSS.currentEngine())

    print(Symbol("bla").getName())
    print(Symbol("bla"))
    print(Expr("bla2", []).getHead())

    expr = Expr("bla2", ["arg1", "arg2"])
    print(expr)

    expr2 = Expr("bla3", [1, True, 'blabla', Symbol("symb")])
    args = expr2.getArguments()
    print(args)

    expr3 = Expr("Plus", [expr, expr2])
    print(expr3)

    print(Expr("Plus", [Expr("bla2", ["arg1", "arg2"]), Expr("bla3", [1, True, 'blabla', Symbol("symb")])]))

    print(Expr("Plus", [1, 2]).evaluate())

    exprToEval = Expr("Plus", [Symbol("A"), Expr("Plus", [1,2,3,4,5,6,7,8,9,10])])
    print(exprToEval)

    print(evaluate(exprToEval))
    print(evaluate(1))
    print(evaluate("Ah!"))

def TestDatabase():
    data_path = "Data/hyai/"
    schema_file = "tables_schema.json"
    table_list = ["weather", "balancing", "constraints"]
    table_extension = ".csv"

    # test schema loading
    BOSS.loadDatabaseSchema(data_path + schema_file)

    # test table loading
    for table in table_list:
        print("load table '" + table + "'...")
        BOSS.loadTableData(table, data_path + table + table_extension)
        BOSS.evaluate(Expr("Print", [Symbol(table)]))

    # test insertion
    BOSS.evaluate(Expr("Insert", [Symbol("region"), Expr("list", [5, "BLA", "bla bla bla."])]))
    BOSS.evaluate(Expr("Print", [Symbol("region")]))

TestExpressions()
#TestDatabase()
