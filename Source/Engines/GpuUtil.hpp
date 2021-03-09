#ifndef BOSS_GPUUTIL_HPP
#define BOSS_GPUUTIL_HPP

#include <stdexcept>
#include <sstream>

namespace boss::engines::GPU {

std::string metric_size(unsigned int size) {
  std::ostringstream os;
  auto sz = static_cast<float>(size);
  auto sizes = {"", "K", "M", "G", "T", "P"};
  const float kilo_size = 1024;
  auto count = 0U;
  while(sz > kilo_size && count < (sizes.size() - 1)) {
    count++;
    sz /= kilo_size;
  }
  os << sz << sizes.begin()[count];
  return os.str();
}

// Base case
template <int> void setArgsHelper(cl::Kernel&) {}

template <int idx, typename Arg, typename... Args>
void setArgsHelper(cl::Kernel& kernel, Arg a, Args... args) {
  kernel.setArg(idx, a);
  setArgsHelper<idx + 1>(kernel, args...);
}

template <typename... Args> void setKernelArgs(cl::Kernel& kernel, Args... args) {
  setArgsHelper<0>(kernel, args...);
}

boss::AtomicExpression get_type_from_string(std::string type) {
  if(type == "number") {
    return boss::AtomicExpression{int(0)};
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

bool get_cl2(const cl::Device& device) {
  return device.getInfo<CL_DEVICE_OPENCL_C_VERSION>()[9] >= '2'; // "OpenCL C X.x (vendor info)"
}

} // namespace boss::engines::GPU

#endif // BOSS_GPUUTIL_HPP
