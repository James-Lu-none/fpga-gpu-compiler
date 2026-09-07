#include "fpgagpu_runtime.h"

// Conditional branch and reconvergence test
void test_branch(uint32_t *data) {
    uint32_t tid = get_thread_id_x();
    uint32_t val = gpu_load32(&data[tid]);

    if (val > 10) {
        val = val * 2;
    } else {
        val = val + 5;
    }

    syncthreads(); // Reconverge divergent warps
    gpu_store32(&data[tid], val);
    gpu_exit();
}
