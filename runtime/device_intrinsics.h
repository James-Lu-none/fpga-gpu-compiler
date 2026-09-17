#ifndef OPENCL_DEVICE_H
#define OPENCL_DEVICE_H

#include <stdint.h>

#define __kernel
#define __global
#define __constant
#define __local
#define __private

#ifdef __cplusplus
extern "C" {
#endif

// System Register intrinsics
__attribute__((always_inline)) static inline uint32_t get_local_id(uint32_t dim) {
    uint32_t val;
    if (dim == 0) {
        __asm__ volatile ("s2r %0, SR_TID.X" : "=r"(val));
    } else {
        __asm__ volatile ("s2r %0, SR_TID.Y" : "=r"(val));
    }
    return val;
}

__attribute__((always_inline)) static inline uint32_t get_group_id(uint32_t dim) {
    uint32_t val;
    if (dim == 0) {
        __asm__ volatile ("s2r %0, SR_BID.X" : "=r"(val));
    } else {
        __asm__ volatile ("s2r %0, SR_BID.Y" : "=r"(val));
    }
    return val;
}

// Global ID = group_id * block_dim + local_id
// Hardware configuration: 2 threads per block in base SIMD lane configuration
__attribute__((always_inline)) static inline uint32_t get_global_id(uint32_t dim) {
    uint32_t tid, bid;
    if (dim == 0) {
        __asm__ volatile ("s2r %0, SR_TID.X" : "=r"(tid));
        __asm__ volatile ("s2r %0, SR_BID.X" : "=r"(bid));
        return (bid + bid) + tid;
    } else {
        uint32_t tid, bid;
        __asm__ volatile ("s2r %0, SR_TID.Y" : "=r"(tid));
        __asm__ volatile ("s2r %0, SR_BID.Y" : "=r"(bid));
        return bid + tid;
    }
}

__attribute__((always_inline)) static inline void barrier(uint32_t flags) {
    (void)flags;
    __asm__ volatile ("sync");
}

__attribute__((always_inline)) static inline uint32_t gpu_load32(const void *ptr) {
    uint32_t val;
    __asm__ volatile ("ldr %0, [%1]" : "=r"(val) : "r"(ptr));
    return val;
}

__attribute__((always_inline)) static inline void gpu_store32(void *ptr, uint32_t val) {
    __asm__ volatile ("str %0, [%1]" : : "r"(val), "r"(ptr));
}

#ifdef __cplusplus
}
#endif

#endif // OPENCL_DEVICE_H
