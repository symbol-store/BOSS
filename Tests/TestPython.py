#!/usr/bin/env python3

import sys
sys.path.insert(0, '.')
sys.path.insert(0, './build')
sys.path.insert(0, './Debug')
sys.path.insert(0, './Release')

import faulthandler
faulthandler.enable()

import BOSS
from BOSS import evaluate
from BOSS import ComplexExpression as Expr
Symbol = BOSS.Expression.Symbol


def TestExpressions(engine):
    BOSS.setEngine(engine)

    print(Symbol("bla"))
    print(Expr("bla2", []).getHead())

    expr = Expr("bla2", ["arg1", "arg2"])
    print(expr)

    expr2 = Expr("bla3", [1, True, 'blabla'])
    args = expr2.getArguments()
    print(args)

    print(Symbol("A") + Symbol("B"))
    print(evaluate(Symbol("A") + Symbol("B")))

    print(expr + expr2)
    print(evaluate(expr + expr2))

    print(Expr("Plus", [1, 2]).evaluate())

    exprToEval = Expr("Plus", [Symbol("A"), Expr("Plus", [1,2,3,4,5,6,7,8,9,10])])
    print(exprToEval)
    print(exprToEval.evaluate())
    
    print(evaluate(1))
    print(evaluate("Test!"))

    listExpr = Expr("List", [10,11,Expr("Plus", [10, 2])])
    print(listExpr.evaluate())
    print("1: " + str(Expr("Extract", [listExpr, 1]).evaluate()))
    print("2: " + str(Expr("Extract", [listExpr, 2]).evaluate()))
    print("3: " + str(Expr("Extract", [listExpr, 3]).evaluate()))

def main():
    print("using Wolfram engine...")
    TestExpressions(BOSS.WolframEngine)
    print("")
    print("using Bulk engine...")
    TestExpressions(BOSS.BulkEngine)
    print("")

if __name__ == "__main__":
    main()
