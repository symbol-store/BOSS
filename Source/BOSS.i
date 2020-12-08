%module BOSS

%include "std_string.i"

%{
  #include "Source/Expression.hpp"
%}

%{
typedef Expression Expression;
typedef Expression::Symbol Symbol;
typedef Expression::ArgumentList ArgumentList;
typedef Expression::ArgumentType ArgumentType;
typedef Expression::ReturnType ReturnType;
%}

class ArgumentType {
public:  
  explicit ArgumentType(bool);
  explicit ArgumentType(int);
  explicit ArgumentType(float);
  explicit ArgumentType(std::string);
  explicit ArgumentType(Expression);
  explicit ArgumentType(Symbol);
};

class ReturnType {
public:  
  explicit ReturnType(bool);
  explicit ReturnType(int);
  explicit ReturnType(float);
  explicit ReturnType(std::string);
  explicit ReturnType(Expression);
  explicit ReturnType(Symbol);
};

%typemap(out) ReturnType {
  ReturnType returnType = ReturnType($1);
  if(auto * boolVal = std::get_if<bool>(&returnType)) {
    $result = PyBool_FromLong((long)*boolVal);
  } else if(auto * intVal = std::get_if<int>(&returnType)) {
    $result = PyLong_FromLong((long)*intVal);
  } else if(auto * floatVal = std::get_if<float>(&returnType)) {
    $result = PyFloat_FromDouble((double)*floatVal);
  } else if(auto * strVal = std::get_if<std::string>(&returnType)) {
    $result = PyString_FromString(strVal->c_str());
  } else {
    // handle any other type as a pointer to underline c object
    std::visit([&self, &$result](auto const& arg) {
      using argType = std::decay_t<decltype(arg)>;
      if constexpr(std::is_same_v<argType, Expression>) {
        Expression * ptr = new Expression(arg);
        $result = SWIG_NewPointerObj((void*)ptr, $descriptor(Expression *), SWIG_POINTER_OWN);
      } else if constexpr(std::is_same_v<argType, Symbol>) {
        $result = SWIG_NewPointerObj((void*)new Symbol(arg), $descriptor(Symbol *), SWIG_POINTER_OWN);
      }
      else {
        $result = SWIG_NewPointerObj((void*)new argType(arg), $descriptor(ArgumentType *), SWIG_POINTER_OWN);
      }
    }, returnType);
  }
}

%typemap(in) ArgumentType const & {
  if (PyBool_Check($input)) {
    $1 = new ArgumentType((bool)PyObject_IsTrue($input));
  } else if (PyInt_Check($input)) {
    $1 = new ArgumentType((int)PyInt_AsLong($input));
  } else if (PyFloat_Check($input)) {
    $1 = new ArgumentType((float)PyFloat_AsDouble($input));
  } else if (PyString_Check($input)) {
    $1 = new ArgumentType(PyString_AsString($input));
  } else if  (PyUnicode_Check($input)) {
    PyObject * encoded = PyUnicode_AsEncodedString($input, "utf-8", "Error ~");
    const char * bytes = PyBytes_AsString(encoded);
    $1 = new ArgumentType(std::string(bytes));
  } else {
    void * rawPtr = NULL; 
    if (SWIG_IsOK(SWIG_ConvertPtr($input, &rawPtr, $descriptor(Expression *), 0))) {
      $1 = new ArgumentType(*(Expression*)rawPtr);
    } else if (SWIG_IsOK(SWIG_ConvertPtr($input, &rawPtr, $descriptor(Symbol *), 0))) {
      $1 = new ArgumentType(*(Symbol*)rawPtr);
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
%typemap(freearg) ArgumentType const & {
   delete $1;
}

%typemap(out) EngineImplementation & {
   $result = PyLong_FromLong((long)*$1);
}

%{
  #include "Source/Helpers.hpp"
%}

%include "Helpers.hpp"

%ignore to_string(ArgumentType const& argumentType);
%ignore to_string(ArgumentList const& argumentList);
%ignore to_string(Expression const& expression);
%inline %{
  namespace std {
    std::string to_string_dispatch(ArgumentType const& argumentType);
    std::string to_string(ArgumentList const& argumentList) {
      std::string str;
      bool first = true;
      for(auto const& arg : argumentList) {
        if(!first) {
          str += ", ";
        }
        str += to_string_dispatch(arg);
        first = false;
      }
      return str;
    }

    std::string to_string(Expression const& expression) {
      return expression.getHead() + "(" + to_string(expression.getArguments()) + ")";
    }

    std::string to_string(Symbol const& symbol) {
      return "Symbol(" + symbol.getName() + ")";
    }

    std::string to_string_dispatch(ArgumentType const& argumentType) {
      return std::visit([](auto && arg) {
          using argType = std::decay_t<decltype(arg)>;
          if constexpr(std::is_same_v<argType, std::string>) {
            return arg;
          } else {
            return to_string(arg);
          }
        }
        , argumentType);
    }
  }
%}

class Symbol {
public:
  Symbol(std::string const&);
  std::string const& getName();
};

%feature("python:slot", "tp_repr", functype="reprfunc") Symbol::__str__;
%extend Symbol {
  std::string __str__() {
    return std::to_string(*self);
  }
}

%typemap(in) std::vector<ArgumentType> const & {
  if (!PySequence_Check($input)) { // TODO handle PyList too?
    PyErr_SetString(PyExc_ValueError,"Expected a sequence");
    return -1;
  }
  $1 = new std::vector<ArgumentType>();
  auto & temp = *$1;
  auto size = PySequence_Length($input);
  temp.reserve(size);
  for (int i = 0; i < size; i++) {
    PyObject *o = PySequence_GetItem($input,i);
    if (PyBool_Check(o)) {
      temp.emplace_back((bool)PyObject_IsTrue(o));
    } else if (PyInt_Check(o)) {
      temp.emplace_back((int)PyInt_AsLong(o));
    } else if (PyFloat_Check(o)) {
      temp.emplace_back((float)PyFloat_AsDouble(o));
    } else if (PyString_Check(o)) {
      temp.emplace_back(PyString_AsString(o));
    } else if  (PyUnicode_Check(o)) {
      PyObject * encoded = PyUnicode_AsEncodedString(o, "utf-8", "Error ~");
      const char * bytes = PyBytes_AsString(encoded);
      temp.emplace_back(std::string(bytes));
    } else {
      void * rawPtr = NULL;
      if (SWIG_IsOK(SWIG_ConvertPtr(o, &rawPtr, $descriptor(Expression *), 0))) {
        temp.emplace_back(*(Expression*)rawPtr);
      } else if (SWIG_IsOK(SWIG_ConvertPtr(o, &rawPtr, $descriptor(Symbol *), 0))) {
        temp.emplace_back(*(Symbol*)rawPtr);
      } else {
        PyObject * repr = PyObject_Str(o);
        PyObject * str = PyUnicode_AsEncodedString(repr, "utf-8", "Error ~");
        const char * bytes = PyBytes_AsString(str);

        PyErr_Format(PyExc_TypeError,
          "unsupported type in the #%i element of the list: %s",
          i, bytes);

        Py_XDECREF(repr);
        Py_XDECREF(str);

        delete $1;
        return -1;
      }
    }
  }
}
%typemap(freearg) std::vector<ArgumentType> const & {
   delete $1;
}

class ArgumentList {
};

%typemap(out) ArgumentList const & {
  auto size = std::distance($1->begin(), $1->end());
  $result = PyList_New(size);
  int index = 0;
  for(auto it = $1->begin(); it != $1->end(); ++it, ++index) {
    if(auto * boolVal = std::get_if<bool>(&*it)) {
      PyObject *o = PyBool_FromLong((long)*boolVal);
      PyList_SetItem($result,index,o);
    } else if(auto * intVal = std::get_if<int>(&*it)) {
      PyObject *o = PyLong_FromLong((long)*intVal);
      PyList_SetItem($result,index,o);
    } else if(auto * floatVal = std::get_if<float>(&*it)) {
      PyObject *o = PyFloat_FromDouble((double)*floatVal);
      PyList_SetItem($result,index,o);
    } else if(auto * strVal = std::get_if<std::string>(&*it)) {
      PyObject *o = PyString_FromString(strVal->c_str());
      PyList_SetItem($result,index,o);
    } else {
      // handle any other type as a pointer to underline c object
      PyObject *o = NULL;
      std::visit([&self, &o](auto const& arg) {
        using argType = std::decay_t<decltype(arg)>;
        if constexpr(std::is_same_v<argType, Expression>) {
          o = SWIG_NewPointerObj((void*)new Expression(arg), $descriptor(Expression *), SWIG_POINTER_OWN);
        } else if constexpr(std::is_same_v<argType, Symbol>) {
          o = SWIG_NewPointerObj((void*)new Symbol(arg), $descriptor(Symbol *), SWIG_POINTER_OWN);
        }
        else {
          o = SWIG_NewPointerObj((void*)new argType(arg), $descriptor(ArgumentType *), SWIG_POINTER_OWN);
        }
      }, *it);
      PyList_SetItem($result,index,o);
    }
  }
}

class Expression {
public:
  Expression(std::string const& head, std::vector<ArgumentType> const& arguments);
  ArgumentList const& getArguments() const;
  std::string const& getHead() const;
};

%feature("python:slot", "tp_repr", functype="reprfunc") Expression::__str__;
%extend Expression {
  std::string __str__() {
    return std::to_string(*self);
  }
};

%feature("python:slot", "tp_repr", functype="reprfunc") ArgumentList::__str__;
%extend ArgumentList {
  std::string __str__() {
    return std::to_string(*self);
  }
};

%extend Expression {
  ReturnType evaluate() {
    return evaluate(*self);
  }
}
