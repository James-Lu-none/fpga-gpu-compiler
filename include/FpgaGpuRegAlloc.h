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

    // R0 is zero; R30/R31 are reserved for even/odd bank repair copies.
    // Allocate R1..R29, then repair all real two-source bank conflicts.
    bool allocate(std::vector<BasicBlockCode> &blocks);
    static bool verifyBanks(const std::vector<BasicBlockCode> &blocks);

private:
    std::unordered_map<VReg, uint8_t> vregToPhys;

    // Simple Linear Scan / Greedy allocator
    uint8_t getPhysReg(VReg vreg);
};

} // namespace fpgagpu

#endif // FPGA_GPU_REGALLOC_H
