#pragma once
#ifdef GPUINTERFACE
#include "../Engine.hpp"
#include <CL/cl2.hpp>
#include <unordered_map>

namespace boss::engines::GPU {

struct DB { // Default constructor is valid DB with no data.
  struct Column {
    Column(cl::Buffer s, AtomicExpression t, size_t ts) : storage(s), type(t), type_size(ts) {}
    cl::Buffer storage;
    AtomicExpression type;
    size_t type_size;
  };
  std::vector<Column> columns;
  std::unordered_map<std::string, size_t> columns_by_name;
  cl::Buffer symbol_storage;
  size_t capacity;
  size_t size;

  void resize(size_t new_capacity);
};

struct EngineImplementation {
private:
  cl::Device device;
  cl::Context ctx;
  cl::CommandQueue cqueue;
  bool cl2;
  cl::Buffer allocate_memory(size_t mem_size, cl_mem_flags flags = CL_MEM_READ_WRITE);
  cl::Buffer allocate_memory(size_t mem_size, const cl::Buffer& other, cl_mem_flags flags = CL_MEM_READ_WRITE);
  template <typename T>
  cl::Buffer allocate_memory(size_t num_elems, T* begin, cl_mem_flags = CL_MEM_READ_WRITE);

  cl::Kernel accu_int, accu_float;

  std::unordered_map<std::string, DB> databases;
  void resize_db(DB& db, size_t new_size);

public:
  constexpr static int CL_WG_SIZE = 32;
  constexpr static int DEFAULT_COL_ALLOC = 2;

  EngineImplementation();
  static cl::Device get_cl_device();
  std::variant<int, float> plus(const std::vector<Expression>&);
  void create_database(std::string name);
  void create_database_column(std::string db_name, std::string col_name, std::string col_type_id);
  void insert_database_element(std::string db_name, const std::vector<Expression>& value);
  std::vector<Expression> get_column(std::string db_name, std::string col_name);
};

class Engine : public boss::Engine {
private:
  EngineImplementation impl;

public:
  Engine(Engine&) = delete;
  Engine& operator=(Engine&) = delete;
  Engine(Engine&&) = default;
  Engine& operator=(Engine&&) = default;
  Engine();
  Expression evaluate(Expression const& e);
  ~Engine(){};
  friend class EngineImplementation;
};
} // namespace boss::engines::GPU

#endif // GPUINTERFACE
