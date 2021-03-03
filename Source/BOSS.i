%module BOSS

%include "std_string.i"
%include stl.i

%{
  #include "Source/Expression.hpp"
  #include "Source/SwigHelpers.hpp"
  #include "Source/Utilities.hpp"

  #define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#if defined(SWIGMZSCHEME)
  #include "Source/Shims/RacketMacros.cpp"
#elif defined(SWIGPYTHON)
  #include "numpy/arrayobject.h"
  #include "numpy/ndarraytypes.h"
#endif

  #include <sstream>
  #include <functional>

  using boss::utilities::operator""_;
%}

%init %{
#if defined(SWIGMZSCHEME)
  scheme_eval_string_all(getRacketMacroShims().c_str(), menv, 1);
  scheme_set_type_printer(swig_type, [](Scheme_Object* v, int dis, Scheme_Print_Params* pp) {
    auto ttv = reinterpret_cast<struct swig_mz_proxy*>(v);
    std::stringstream out;
    if(ttv->type == &_swigt__p_Symbol) {
      out << reinterpret_cast<Symbol*>(ttv->object);
    } else if(ttv->type == &_swigt__p_ComplexExpression) {
      out << *reinterpret_cast<ComplexExpression*>(ttv->object);
    } else if(ttv->type == &_swigt__p_Expression) {
      out << *reinterpret_cast<Expression*>(ttv->object);
    } else if(ttv->type == &_swigt__p_ExpressionArguments) {
      auto arguments = *reinterpret_cast<ExpressionArguments*>(ttv->object);
      std::stringstream out;
      auto it = arguments.begin();
      out << *it;
      for(++it; it != arguments.end(); ++it) {
        out << ", " << *it;
      }
    } else if(ttv->type == &_swigt__p_std__vectorT_Expression_t) {
      auto arguments = *reinterpret_cast<std::vector<Expression>*>(ttv->object);
      std::stringstream out;
      auto it = arguments.begin();
      out << *it;
      for(++it; it != arguments.end(); ++it) {
        out << ", " << *it;
      }
    } else {
      out << std::string("unknown type: ") << ttv->type->name;
    }
    scheme_print_bytes(pp, out.str().c_str(), 0, out.str().length());
  });
#elif defined(SWIGPYTHON)
  import_array();
#endif
%}

%{
using boss::Symbol;
using boss::Expression;
using boss::Symbol;
using boss::AtomicExpression;
using boss::ComplexExpression;
using boss::ExpressionArguments;
%}

class Symbol {
public:
  Symbol(std::string);
};

#if defined(SWIGMZSCHEME)
namespace std {%template(ExpressionArguments) vector<Expression>;}
%typemap(in) Symbol const& {
  if(SCHEME_SYMBOLP($input)){
    $1 = new Symbol(std::string(SCHEME_SYM_VAL($input)));
  } else if(SCHEME_BYTE_STRINGP($input)) {
    $1 = new Symbol(std::string(SCHEME_BYTE_STR_VAL($input)));
  } else if(SCHEME_TYPE($input) == swig_type) {
    $1 = static_cast<Symbol*>(reinterpret_cast<struct swig_mz_proxy*>($input)->object);
  } else {
    throw std::logic_error("trying to construct symbol from unknown type");
  }
}
%typemap(out) Expression {
  std::function<Scheme_Object*(Expression const&)> convert =
      [&](Expression const& expression) -> Scheme_Object* {
    return std::visit(
        boss::utilities::overload(
            [&](bool a) { return a?scheme_make_true():scheme_make_false(); },
            [&](int a) { return scheme_make_integer(a); },
            [&](float a) { return scheme_make_float(a); },
            [&](char const* a) { return scheme_make_string(a); },
            [&](Symbol const& a) { return scheme_make_symbol(a.getName().c_str()); },
            [&](std::string const& a) { return scheme_make_string(a.c_str()); },
            [&](ComplexExpression const& expression) {
              std::vector<Scheme_Object*> arguments;
              arguments.push_back(convert(expression.getHead()));
              for(auto const& arg : expression.getArguments())
                arguments.push_back(convert(arg));
              return scheme_apply(scheme_builtin_value("list"), arguments.size(),
                                  arguments.data());
            }),
        expression);
  };
  $result = convert($1);
}
#elif defined(SWIGPYTHON)
%inline %{
    PyObject * createPythonPointerObj(PyObject * self, Expression expression, 
                swig_type_info * expressionDesc, 
                swig_type_info * complexExpressionDesc) {
      return std::visit([&self, &expressionDesc, &complexExpressionDesc](auto const& arg) -> PyObject * {
        using argType = std::decay_t<decltype(arg)>;
        if constexpr(std::is_same_v<argType, ComplexExpression>) {
          return SWIG_Python_NewPointerObj(self, (void*)new ComplexExpression(arg), complexExpressionDesc, SWIG_POINTER_OWN);
        } else if constexpr(std::is_same_v<argType, boss::Symbol>) {
          return SWIG_Python_NewPointerObj(self, (void*)new ComplexExpression("Symbol"_, {arg.getName()}), complexExpressionDesc, SWIG_POINTER_OWN);
        } else {
          return SWIG_Python_NewPointerObj(self, (void*)new Expression(arg), expressionDesc, SWIG_POINTER_OWN);
        }
      }, expression);
    }
%}

%typemap(out) Expression {
  Expression expression = Expression($1);
  if(auto * boolVal = std::get_if<bool>(&expression)) {
    $result = PyBool_FromLong((long)*boolVal);
  } else if(auto * intVal = std::get_if<int>(&expression)) {
    $result = PyInt_FromLong((long)*intVal);
  } else if(auto * floatVal = std::get_if<float>(&expression)) {
    $result = PyFloat_FromDouble((double)*floatVal);
  } else if(auto * strVal = std::get_if<std::string>(&expression)) {
    $result = PyString_FromString(strVal->c_str());
  } else {
    // handle any other type as a pointer to underline c object
    $result = createPythonPointerObj($self, expression, $descriptor(Expression *), $descriptor(ComplexExpression *));
  }
}

%typemap(in) Expression const & {
  if (PyBool_Check($input)) {
    $1 = new Expression((bool)PyObject_IsTrue($input));
  } else if (PyInt_Check($input)) {
    $1 = new Expression((int)PyInt_AsLong($input));
  } else if (PyFloat_Check($input)) {
    $1 = new Expression((float)PyFloat_AsDouble($input));
  } else if (PyString_Check($input)) {
    $1 = new Expression(PyString_AsString($input));
  } else if  (PyUnicode_Check($input)) {
    PyObject * encoded = PyUnicode_AsEncodedString($input, "utf-8", "Error ~");
    const char * bytes = PyBytes_AsString(encoded);
    $1 = new Expression(std::string(bytes));
  } else {
    void * rawPtr = NULL; 
    if (SWIG_IsOK(SWIG_ConvertPtr($input, &rawPtr, $descriptor(ComplexExpression *), 0))) {
      $1 = new Expression(*(ComplexExpression*)rawPtr);
    } else  if (SWIG_IsOK(SWIG_ConvertPtr($input, &rawPtr, $descriptor(Symbol *), 0))) {
      $1 = new Expression(*(Symbol*)rawPtr);
    } else {
      PyObject * repr = PyObject_Str($input);
      PyObject * str = PyUnicode_AsEncodedString(repr, "utf-8", "Error ~");
      const char * bytes = PyBytes_AsString(str);

      PyErr_Format(PyExc_TypeError, "unsupported type as argument: %s", bytes);

      Py_XDECREF(repr);
      Py_XDECREF(str);

      return NULL;
    }
  }
}
%typemap(freearg) Expression const & {
   delete $1;
}

%typemap(out) EngineImplementation & {
   $result = PyInt_FromLong((long)*$1);
}

%typemap(out) ExpressionArguments const & {
  auto size = std::distance($1->begin(), $1->end());
  $result = PyList_New(size);
  int index = 0;
  for(auto it = $1->begin(); it != $1->end(); ++it, ++index) {
    if(auto * boolVal = std::get_if<bool>(&*it)) {
      PyObject *o = PyBool_FromLong((long)*boolVal);
      PyList_SetItem($result,index,o);
    } else if(auto * intVal = std::get_if<int>(&*it)) {
      PyObject *o = PyInt_FromLong((long)*intVal);
      PyList_SetItem($result,index,o);
    } else if(auto * floatVal = std::get_if<float>(&*it)) {
      PyObject *o = PyFloat_FromDouble((double)*floatVal);
      PyList_SetItem($result,index,o);
    } else if(auto * strVal = std::get_if<std::string>(&*it)) {
      PyObject *o = PyString_FromString(strVal->c_str());
      PyList_SetItem($result,index,o);
    } else {
      // handle any other type as a pointer to underline c object
      PyObject *o = createPythonPointerObj($self, *it, $descriptor(Expression *), $descriptor(ComplexExpression *));
      PyList_SetItem($result,index,o);
    }
  }
}

%typemap(in) ExpressionArguments const & {
  if (!PySequence_Check($input)) { // TODO handle PyList too?
    PyErr_SetString(PyExc_ValueError,"Expected a sequence");
    SWIG_fail;
  }

  $1 = new ExpressionArguments();
  auto & args = *$1;
  auto size = PySequence_Length($input);
  args.resize(size);
  int index = 0;
  for (auto & arg : args) {
    PyObject *o = PySequence_GetItem($input, index++);
    if(PyBool_Check(o)) {
      arg.emplace<bool>(PyObject_IsTrue(o));
    } else if(PyInt_Check(o)) {
      arg.emplace<int>(PyInt_AsLong(o));
    } else if(PyFloat_Check(o)) {
      arg.emplace<float>(PyFloat_AsDouble(o));
    } else if(PyString_Check(o)) {
      arg.emplace<std::string>(PyString_AsString(o));
    } else if(PyUnicode_Check(o)) {
      PyObject * encoded = PyUnicode_AsEncodedString(o, "utf-8", "Error ~");
      const char * bytes = PyBytes_AsString(encoded);
      arg.emplace<std::string>(bytes);
    } else {
      void * rawPtr = NULL;
      if(SWIG_IsOK(SWIG_ConvertPtr(o, &rawPtr, $descriptor(ComplexExpression *), 0))) {
        arg.emplace<ComplexExpression>(*(ComplexExpression*)rawPtr);
      } else if(SWIG_IsOK(SWIG_ConvertPtr(o, &rawPtr, $descriptor(Symbol *), 0))) {
        arg.emplace<Symbol>(*(Symbol*)rawPtr);
      } else {
        PyObject * repr = PyObject_Str(o);
        PyObject * str = PyUnicode_AsEncodedString(repr, "utf-8", "Error ~");
        const char * bytes = PyBytes_AsString(str);

        PyErr_Format(PyExc_TypeError,
          "unsupported type in the #%i element of the list: %s",
          index, bytes);

        Py_XDECREF(repr);
        Py_XDECREF(str);

        SWIG_fail;
      }
    }
  }
}
%typemap(freearg) ExpressionArguments const & {
   delete $1;
}
#endif

class Expression {
public:  
  Expression(bool);
  Expression(int);
  Expression(float);
  Expression(std::string);
  Expression(Symbol const&);
  Expression(ComplexExpression);
};

class ComplexExpression {
public:
  ComplexExpression(Symbol const& head, std::vector<Expression> const & arguments);
  ExpressionArguments const& getArguments() const;
  std::string const& getHead() const;
};

%include "SwigHelpers.hpp"

%feature("python:slot", "tp_repr", functype="reprfunc") Symbol::__str__;
%extend Symbol {
  std::string __str__() {
    std::ostringstream oss;
    oss << *$self;
    return oss.str();
  }
};

%feature("python:slot", "tp_repr", functype="reprfunc") Expression::__str__;
%extend Expression {
  std::string __str__() {
    std::ostringstream oss;
    oss << *$self;
    return oss.str();
  }

  static Expression Symbol(std::string const& name) {
    return boss::Symbol(name);
  }
};

#ifdef SWIGPYTHON
%feature("python:slot", "tp_repr", functype="reprfunc") ComplexExpression::__str__;
%extend ComplexExpression {
  std::string __str__() {
    std::ostringstream oss;
    oss << *$self;
    return oss.str();
  }

  ComplexExpression(std::string const& head, ExpressionArguments const& arguments) {
    ComplexExpression * e = new ComplexExpression(boss::Symbol(head), arguments);
    return e;
  }

  Expression evaluate() {
    return evaluate(*$self);
  }
  
  Expression operator+(Expression const& other) {
    return "Plus"_(*$self, other);
  }
  Expression operator-(Expression const& other) {
    return "Minus"_(*$self, other);
  }
  Expression operator*(Expression const& other) {
    return "Times"_(*$self, other);
  }
  Expression operator/(Expression const& other) {
    return "Divide"_(*$self, other);
  }
  Expression operator-() {
    return "Negation"_(*$self);
  }

  Expression operator==(Expression const& other) {
    return "Equal"_(*$self, other);
  }
  Expression operator!=(Expression const& other) {
    return "NotEqual"_(*$self, other);
  }
  Expression operator<(Expression const& other) {
    return "Less"_(*$self, other);
  }
  Expression operator>(Expression const& other) {
    return "Greater"_(*$self, other);
  }
  Expression operator<=(Expression const& other) {
    return "LessEqual"_(*$self, other);
  }
  Expression operator>=(Expression const& other) {
    return "GreaterEqual"_(*$self, other);
  }

  Expression operator&(Expression const& other) {
    return "And"_(*$self, other);
  }
  Expression operator|(Expression const& other) {
    return "Or"_(*$self, other);
  }
  Expression operator~() {
    return "Not"_(*$self);
  }

  Expression stringJoin(Expression const& other) {
    return "StringJoin"_(*$self, other);
  }
}
#endif
