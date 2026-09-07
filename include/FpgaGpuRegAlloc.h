#ifndef FPGA_GPU_REGALLOC_H
#define FPGA_GPU_REGALLOC_H

#include "FpgaGpu.h"
#include "FpgaGpuISel.h"
#include <unordered_map>
#include <vector>

namespace fpgagpu {

class RegisterAllocator {
public:
    RegisterAllocator() = default;

    // Allocates physical registers R1..R31 for all virtual registers
    // across the list of basic blocks. R0 is reserved as the zero register.
    bool allocate(std::vector<BasicBlockCode> &blocks);

private:
    std::unordered_map<VReg, uint8_t> vregToPhys;

    // Simple Linear Scan / Greedy allocator
    uint8_t getPhysReg(VReg vreg);
};

} // namespace fpgagpu

#endif // FPGA_GPU_REGALLOC_H
