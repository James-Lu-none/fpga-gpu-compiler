#include "fpgagpu_runtime.h"

// Kernel that queries hardware IDs into output array
void test_s2r(uint32_t *output) {
    uint32_t tid_x = get_thread_id_x();
    uint32_t tid_y = get_thread_id_y();
    uint32_t bid_x = get_block_id_x();
    uint32_t bid_y = get_block_id_y();

    gpu_store32(&output[0], tid_x);
    gpu_store32(&output[1], tid_y);
    gpu_store32(&output[2], bid_x);
    gpu_store32(&output[3], bid_y);

    gpu_exit();
}
