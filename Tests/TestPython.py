#!/usr/bin/env python3

import sys
sys.path.insert(0, '.')
sys.path.insert(0, './build')
sys.path.insert(0, './Debug')
sys.path.insert(0, './Release')

import faulthandler;
faulthandler.enable()

import BOSS
from BOSS import evaluate
from BOSS import ComplexExpression as Expr
Symbol = BOSS.Expression.Symbol


def TestExpressions():
    # uncomment to switch to a specific engine (otherwise use default)
    #BOSS.setEngine(BOSS.BulkEngine)
    #BOSS.setEngine(BOSS.WolframEngine)

    print(Symbol("bla"))
    print(Expr("bla2", []).getHead())

    expr = Expr("bla2", ["arg1", "arg2"])
    print(expr)

    expr2 = Expr("bla3", [1, True, 'blabla'])
    args = expr2.getArguments()
    print(args)

    print(Symbol("A") + Symbol("B"))
    print((expr + expr2).evaluate())

    print(Expr("bla2", ["arg1", "arg2"]) + Expr("bla3", [1, True, 'blabla', Symbol("symb")]))

    print(Expr("Plus", [1, 2]).evaluate())

    exprToEval = Expr("Plus", [Symbol("A"), Expr("Plus", [1,2,3,4,5,6,7,8,9,10])])
    print(exprToEval)
    print(exprToEval.evaluate())
    
    print(evaluate(1))
    print(evaluate("Test!"))

TestExpressions()
