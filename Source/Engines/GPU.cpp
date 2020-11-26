#ifdef GPUINTERFACE
#include "GPU.hpp"
#include "../Utilities.hpp"
#include "CL/cl2.hpp"
#include <exception>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

namespace boss::engines::GPU {

constexpr int CL_WG_SIZE = 32;
constexpr int DEFAULT_COL_ALLOC = 2;
const std::string kernel_src =
#include "kernels.cl"
    ;

// Helpers //

template <typename... Types> struct variant_cast_proxy {
  std::variant<Types...> v;
  template <typename... ToTypes> operator std::variant<ToTypes...>() const {
    return std::visit(
        [](auto&& arg) -> std::variant<ToTypes...> { return arg; }, v);
  }
};
template <typename... Types>
auto variant_cast(const std::variant<Types...>& v) -> variant_cast_proxy<Types...> {
  return {v};
}

std::string metric_size(unsigned int size) {
  std::ostringstream os;
  float sz = size;
  auto sizes = {"", "K", "M", "G", "T", "P"};
  int count = 0;
  while(sz > 1024 && count < (sizes.size() - 1)) {
    count++;
    sz /= 1024;
  }
  os << sz << sizes.begin()[count];
  return os.str();
}

template <int idx> void setArgsHelper(cl::Kernel& kernel) {}

template <int idx, typename Arg, typename... Args>
void setArgsHelper(cl::Kernel& kernel, Arg a, Args... args) {
  kernel.setArg(idx, a);
  setArgsHelper<idx + 1>(kernel, args...);
}

template <typename... Args> void setKernelArgs(cl::Kernel& kernel, Args... args) {
  setArgsHelper<0>(kernel, args...);
}

AtomicExpression get_type_from_string(std::string type) {
  if(type == "number") {
    return AtomicExpression{int(0)};
  }
  throw std::runtime_error("Unknown string type " + type);
}

std::string get_table_name(const Expression& arg) {
  const auto e = std::get<ComplexExpression>(arg);
  const auto& e_type = e.getHead();
  if(e_type.getName() != std::string("table"))
    throw std::runtime_error("Unknown table type " + e_type.getName());
  const auto tb_name = std::get<std::string>(e.getArguments()[0]);
  return tb_name;
}

// Implementation //

EngineImplementation::EngineImplementation()
    : device(get_cl_device()), ctx(device), cqueue(ctx, device) {
  auto cl_ver = device.getInfo<CL_DEVICE_OPENCL_C_VERSION>();
  std::cout << "CL version: " << cl_ver << "\n";
  cl2 = cl_ver[9] >= '2'; // "OpenCL C X.x (vendor info)"
  auto build_opts = cl2 ? "-cl-std=CL2.0" : "";
  cl::Program program(ctx, kernel_src);
  if(program.build({device}, build_opts) != CL_SUCCESS) {
    throw std::runtime_error("Failed to compile kernel");
  }
  accu_int = cl::Kernel(program, "accu_int");
}

cl::Buffer EngineImplementation::allocate_memory(size_t size, cl_mem_flags flags) {
  return cl::Buffer(ctx, CL_MEM_READ_WRITE, size);
}
template <typename T>
cl::Buffer EngineImplementation::allocate_memory(size_t num_Ts, T* begin, cl_mem_flags flags) {
  return cl::Buffer(ctx, begin, begin + num_Ts, /*readonly*/ flags & CL_MEM_READ_ONLY);
}

cl::Device EngineImplementation::get_cl_device() {
  std::vector<cl::Platform> all_platforms;
  cl::Platform::get(&all_platforms);
  if(all_platforms.size() == 0) {
    throw std::runtime_error("Failed to find OpenCL platforms");
  }
  auto platform = all_platforms[0];
  std::cout << "Using platform \"" << platform.getInfo<CL_PLATFORM_NAME>() << "\"\n";
  std::vector<cl::Device> all_devices;
  platform.getDevices(CL_DEVICE_TYPE_ALL, &all_devices);
  if(all_devices.size() == 0) {
    throw std::runtime_error("Failed to find device on platform");
  }
  auto device = all_devices[0];
  std::cout << "Using device \"" << device.getInfo<CL_DEVICE_NAME>() << "\"\n";
  std::cout << "Memory available: " << metric_size(device.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>())
            << "\n";
  return device;
}

std::variant<int, float> EngineImplementation::plus(const std::vector<Expression>& args) {
  // OpenCL only operates on memory, can't use C++ variants - need to cast to same type.
  std::variant<int, float> common_type = int{};
  auto common_type_lambda =
  std::for_each(args.begin(), args.end(), [&common_type] (const Expression& e) {
    return std::visit(
      boss::utilities::overload(
        [&common_type](float) {common_type = float{};},
        [&common_type](int)   {},
        [](auto ex) {
          std::cerr << "Couldn't cast expression with type " << typeid(ex).name()
                    << "to numeric type: " << ex << "\n";
          throw std::runtime_error("Can't cast expression to numeric type");
        }
      ), e);
    }
  );

  auto accu_with_type = [this, &args](auto common_type) {
    std::vector<decltype(common_type)> cast_args;
    cast_args.reserve(args.size());
    try {
      std::transform(args.begin(), args.end(), std::back_inserter(cast_args),
                    [](const Expression& e) -> decltype(common_type) {
                      auto e_int_ptr = std::get_if<int>(&e);
                      if (e_int_ptr) return *e_int_ptr;
                      return std::get<float>(e);
                    });
    } catch(std::bad_variant_access& e) {
      // Shouldn't be able to get here
      throw std::runtime_error("Bad variant, must have changed since initial check");
    }

    // Allocate memory
    const size_t num_wgs = ((args.size() - 1) / CL_WG_SIZE) + 1;
    cl::Buffer buf = allocate_memory(cast_args.size(), &cast_args[0], CL_MEM_READ_ONLY);
    cl::Buffer res = allocate_memory(sizeof(common_type) * num_wgs);
    cl::Event event;

    size_t local_mem_sz = CL_WG_SIZE * sizeof(common_type);
    setKernelArgs(accu_int,
      buf, int(args.size()), res, cl::Local(local_mem_sz));

    cqueue.enqueueNDRangeKernel(accu_int, cl::NullRange, num_wgs * CL_WG_SIZE,
                                cl::NDRange(CL_WG_SIZE), nullptr, &event);
    event.wait();
    std::vector<decltype(common_type)> results(num_wgs);
    cqueue.enqueueReadBuffer(res, true, 0, sizeof(common_type) * num_wgs, &results[0]);
    return std::accumulate(results.begin(), results.end(), 0);
  };
  if (std::get_if<int>(&common_type)) {
    return accu_with_type(int{});
  } else {
    return accu_with_type(float{});
  }
}

void EngineImplementation::create_database(std::string name) { databases[name] = DB(); }

void EngineImplementation::create_database_column(std::string db_name, std::string col_name,
                                                  std::string col_type) {
  auto& db = databases[db_name];
  // Allocate memory for column, add type info
  const auto type = get_type_from_string(col_type);
  const auto type_size = std::visit([](const auto& t) { return sizeof(t); }, type);
  const auto col_size = db.capacity > 0 ? db.capacity : DEFAULT_COL_ALLOC;
  const auto tag_size = 0; // sizeof(DB::Column::Tag) // Add tag information to each element.
  const auto col_storage = allocate_memory((type_size + tag_size) * DEFAULT_COL_ALLOC);

  db.columns_by_name[col_name] = db.columns.size();
  db.column_types.push_back(type);
  db.columns.push_back(col_storage);
  db.capacity = col_size;
}

void EngineImplementation::insert_database_element(
    std::string db_name, const std::vector<Expression>& value) {
  auto& db = databases[db_name];
  if(value.size() != db.columns.size())
    throw std::runtime_error("Error on GPU db_insert: need all cols specified");
  // For each col, add one element at db.size, increment size
  if(db.capacity <= db.size)
    throw std::runtime_error("Error on GPU db_insert: can't resize yet");
  // Needs to be tagged correctly too, so turn to POD and copy to GPU, then call kernel insert. (Not
  // done yet)
  const auto write_val = [this, &db](int col, int val) {
    cqueue.enqueueWriteBuffer(db.columns[col], /*block*/ true, /*offset*/ sizeof(int) * db.size,
                              sizeof(int), &val);
  };
  for(auto i = 0; i < value.size(); i++) {
    std::visit(
        boss::utilities::overload{[i, &write_val](int val) { write_val(i, val); },
                 [i, &write_val](const ComplexExpression& e) {
                   if(e.getHead().getName() == "number") {
                     write_val(i, std::get<int>(e.getArguments()[0]));
                   } else {
                     throw std::runtime_error(
                         "Error in GPU db_insert: can't evaluate expressions in insert yet");
                   }
                 },
                 [](const auto& e) { throw std::runtime_error("Can't handle type on GPU"); }},
        value[i]);
  }
  db.size++;
}
std::vector<Expression> EngineImplementation::get_column(std::string db_name,
                                                                     std::string col_name) {
  const auto& db = databases[db_name];
  const auto col_idx = db.columns_by_name.at(col_name);
  const auto& col = db.columns[col_idx];
  // Don't need to worry about tag for now
  std::vector<Expression> ret;
  ret.reserve(db.size);
  std::vector<int> gpu_ret(db.size);
  cqueue.enqueueReadBuffer(col, /*block*/ true, /*off*/ 0, sizeof(int) * db.size, &gpu_ret[0]);
  std::transform(gpu_ret.begin(), gpu_ret.end(), std::back_inserter(ret),
                 [](const auto& it) { return Expression(it); });
  return ret;
}

Engine::Engine() : impl(EngineImplementation()){};

Expression Engine::evaluate(Expression const& unk_expression) {
  const auto e_ptr = std::get_if<ComplexExpression>(&unk_expression);
  if (e_ptr == nullptr) return unk_expression; // Not a complex expression
  const auto e = *e_ptr;
  const auto expr_type = e.getHead().getName();
  std::cout << e << '\n';
  if(expr_type == "Plus") {
    // TODO: working on this, not really done yet.
    return variant_cast(impl.plus(e.getArguments()));
  } else if(expr_type == "create_table") {
    const auto name = std::get<std::string>(e.getArguments()[0]);
    impl.create_database(name);
    return true;
  } else if(expr_type == "create_column") {
    const auto table_name = get_table_name(e.getArguments()[0]);
    const auto col_name = std::get<std::string>(e.getArguments()[1]);
    const auto col_type_name = std::get<std::string>(e.getArguments()[2]);
    if(col_type_name != "number")
      throw std::runtime_error("Unknown column type " + col_type_name);
    impl.create_database_column(table_name, col_name, col_type_name);
    return true;
  } else if(expr_type == "insert") {
    const auto table_name = get_table_name(e.getArguments()[0]);
    const auto elem =
        std::vector<Expression>{e.getArguments().begin() + 1, e.getArguments().end()};
    impl.insert_database_element(table_name, elem);
    return true;
  } else if(expr_type == "select") {
    const auto table_name = get_table_name(e.getArguments()[0]);
    const auto col_name = std::get<std::string>(e.getArguments()[1]);
    const auto predicate = std::get<std::string>(e.getArguments()[2]);
    if(predicate != "*")
      throw std::runtime_error("Currently only supports predicate \"*\"");
    const std::vector<Expression> result = impl.get_column(table_name, col_name);
    return ComplexExpression{Symbol("vector"), impl.get_column(table_name, col_name)};
  } else {
    throw std::runtime_error("Expression \"" + expr_type
  + "\" not implemented for backend GPU");
  }
}

} // namespace boss::engines::GPU

#endif // GPUINTERFACE
