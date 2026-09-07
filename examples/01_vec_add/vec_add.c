#include "fpgagpu_runtime.h"

// Vector Addition Kernel: C[tid] = A[tid] + B[tid]
void vector_add(const uint32_t *A, const uint32_t *B, uint32_t *C, uint32_t n) {
    uint32_t tid = get_thread_id_x();
    uint32_t bid = get_block_id_x();
    
    // Compute global thread index
    uint32_t idx = tid + (bid * 32);

    if (idx < n) {
        uint32_t a_val = gpu_load32(&A[idx]);
        uint32_t b_val = gpu_load32(&B[idx]);
        uint32_t c_val = a_val + b_val;
        gpu_store32(&C[idx], c_val);
    }

    gpu_exit();
}
