#include "FpgaGpuRegAlloc.h"
#include <iostream>
#include <algorithm>

namespace fpgagpu {

uint8_t RegisterAllocator::getPhysReg(VReg vreg) {
    if (vreg == VREG_ZERO) return 0;

    if (auto it = vregToPhys.find(vreg); it != vregToPhys.end()) {
        return it->second;
    }

    // Allocate next available physical register (R1..R31)
    uint8_t assigned = static_cast<uint8_t>(vregToPhys.size() + 1);
    if (assigned >= NUM_PHYSICAL_REGS) {
        // Simple wrap around / reuse modulo 31 (R1..R31)
        assigned = 1 + (assigned % (NUM_PHYSICAL_REGS - 1));
    }

    vregToPhys[vreg] = assigned;
    return assigned;
}

bool RegisterAllocator::allocate(std::vector<BasicBlockCode> &blocks) {
    vregToPhys.clear();

    for (auto &bb : blocks) {
        for (auto &mi : bb.instructions) {
            if (mi.rd != 0) {
                mi.rd = getPhysReg(mi.rd);
            }
            if (mi.rs1 != 0) {
                mi.rs1 = getPhysReg(mi.rs1);
            }
            if (mi.rs2 != 0) {
                mi.rs2 = getPhysReg(mi.rs2);
            }
        }
    }

    return true;
}

} // namespace fpgagpu
