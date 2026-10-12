#include "fpgagpu_runtime.h"

// Conditional branch and reconvergence test
void test_branch(uint32_t *data) {
    uint32_t tid = get_thread_id_x();
    uint32_t val1 = gpu_load32(&data[tid]);
    uint32_t val2 = gpu_load32(&data[tid+1]);
    if (val1 > 10) {
        gpu_store32(&data[tid], val1);
    } else {
        gpu_store32(&data[tid], val2);
    }
}
