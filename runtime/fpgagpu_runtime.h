#ifndef FPGA_GPU_RUNTIME_H
#define FPGA_GPU_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// System Register access intrinsics (mapped to S2R instruction)
__attribute__((always_inline)) static inline uint32_t get_thread_id_x(void) {
    uint32_t val;
    __asm__ volatile ("s2r %0, SR_TID.X" : "=r"(val));
    return val;
}

__attribute__((always_inline)) static inline uint32_t get_thread_id_y(void) {
    uint32_t val;
    __asm__ volatile ("s2r %0, SR_TID.Y" : "=r"(val));
    return val;
}

__attribute__((always_inline)) static inline uint32_t get_block_id_x(void) {
    uint32_t val;
    __asm__ volatile ("s2r %0, SR_BID.X" : "=r"(val));
    return val;
}

__attribute__((always_inline)) static inline uint32_t get_block_id_y(void) {
    uint32_t val;
    __asm__ volatile ("s2r %0, SR_BID.Y" : "=r"(val));
    return val;
}

// SIMT Reconvergence Synchronization (mapped to SYNC instruction)
__attribute__((always_inline)) static inline void syncthreads(void) {
    __asm__ volatile ("sync");
}

// Warp termination (mapped to EXIT instruction)
__attribute__((always_inline)) static inline void gpu_exit(void) {
    __asm__ volatile ("exit");
}

// Global Memory Load / Store helpers
__attribute__((always_inline)) static inline uint32_t gpu_load32(const uint32_t *ptr) {
    uint32_t val;
    __asm__ volatile ("ldr %0, [%1]" : "=r"(val) : "r"(ptr));
    return val;
}

__attribute__((always_inline)) static inline void gpu_store32(uint32_t *ptr, uint32_t val) {
    __asm__ volatile ("str %0, [%1]" : : "r"(val), "r"(ptr));
}

#ifdef __cplusplus
}
#endif

#endif // FPGA_GPU_RUNTIME_H
