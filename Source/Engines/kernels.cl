R"(
__kernel void accu_int(__global const int* A, int num, __global int* output, __local int* target) {
  const size_t globalId = get_global_id(0);
  const size_t localId = get_local_id(0);
  const int res = globalId < num ? A[globalId] : 0;

//#if __OPENCL__C_VERSION__ < 200
  target[localId] = res;

  barrier(CLK_LOCAL_MEM_FENCE);
  size_t blockSize = get_local_size(0);
  size_t halfBlockSize = blockSize / 2;
  while (halfBlockSize>0) {
    if (localId<halfBlockSize) {
      target[localId] += target[localId + halfBlockSize];
      if ((halfBlockSize*2)<blockSize) { // uneven block division
        if (localId==0) { // when localID==0
          target[localId] += target[localId + (blockSize-1)];
        }
      }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    blockSize = halfBlockSize;
    halfBlockSize = blockSize / 2;
  }
  if (localId==0) {
    output[get_group_id(0)] = target[0];
  }
/*#else
  int sum_res = work_group_reduce_add(res);
  if (localId == 0) {
    output[get_group_id(0)] = sum_res;
  }
#endif*/
}
)"