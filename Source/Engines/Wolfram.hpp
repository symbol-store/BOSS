#ifdef WSINTERFACE
#include "../Expression.hpp"
#include <wstp.h>

namespace boss::engines::wolfram {
class Engine {
private:
  WSENV environment = {};
  WSLINK link = {};
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;

public:
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = default;
  Engine();
  Expression::ReturnType evaluate(Expression const& e);
  ~Engine();
  friend class EngineImplementation;

};
} // namespace boss::engines::wolfram
#endif // WSINTERFACE
