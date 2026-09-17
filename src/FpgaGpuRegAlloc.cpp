#include "FpgaGpuRegAlloc.h"
#include <iostream>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <vector>

namespace fpgagpu {

bool RegisterAllocator::allocate(std::vector<BasicBlockCode> &blocks) {
    vregToPhys.clear();

    struct LiveRange {
        uint32_t birth{UINT32_MAX};
        uint32_t death{0};
    };

    std::unordered_map<VReg, LiveRange> intervals;

    // Step 1: Assign global instruction index and compute live ranges (birth & death)
    uint32_t globalIdx = 0;
    for (const auto &bb : blocks) {
        for (const auto &mi : bb.instructions) {
            auto recordUse = [&](VReg vreg) {
                if (vreg == VREG_ZERO) return;
                auto &range = intervals[vreg];
                range.birth = std::min(range.birth, globalIdx);
                range.death = std::max(range.death, globalIdx);
            };

            recordUse(mi.rd);
            recordUse(mi.rs1);
            recordUse(mi.rs2);

            globalIdx++;
        }
    }

    // Step 2: Sort virtual registers by birth index
    std::vector<std::pair<VReg, LiveRange>> sortedRegs;
    sortedRegs.reserve(intervals.size());
    for (const auto &kv : intervals) {
        sortedRegs.push_back(kv);
    }
    std::sort(sortedRegs.begin(), sortedRegs.end(), [](const auto &a, const auto &b) {
        if (a.second.birth != b.second.birth) {
            return a.second.birth < b.second.birth;
        }
        return a.second.death < b.second.death;
    });

    // Step 3: Linear scan allocation using a free physical register pool (R1..R31)
    std::set<uint8_t> freePool;
    for (uint8_t r = 1; r < NUM_PHYSICAL_REGS; r++) {
        freePool.insert(r);
    }

    // Active allocations: list of (deathIdx, vreg)
    std::vector<std::pair<uint32_t, VReg>> active;

    for (const auto &[vreg, range] : sortedRegs) {
        // Expire old intervals whose death < current range's birth
        auto it = active.begin();
        while (it != active.end()) {
            if (it->first < range.birth) {
                uint8_t freedPhys = vregToPhys[it->second];
                freePool.insert(freedPhys);
                it = active.erase(it);
            } else {
                ++it;
            }
        }

        if (freePool.empty()) {
            std::cerr << "Error: Register pressure too high, ran out of physical registers!\n";
            return false;
        }

        // Allocate smallest available physical register
        uint8_t phys = *freePool.begin();
        freePool.erase(freePool.begin());

        vregToPhys[vreg] = phys;
        active.emplace_back(range.death, vreg);
    }

    // Step 4: Replace virtual registers with assigned physical registers
    for (auto &bb : blocks) {
        for (auto &mi : bb.instructions) {
            if (mi.rd != 0) {
                mi.rd = vregToPhys[mi.rd];
            }
            if (mi.rs1 != 0) {
                mi.rs1 = vregToPhys[mi.rs1];
            }
            if (mi.rs2 != 0) {
                mi.rs2 = vregToPhys[mi.rs2];
            }
        }
    }

    return true;
}

uint8_t RegisterAllocator::getPhysReg(VReg vreg) {
    if (vreg == VREG_ZERO) return 0;
    auto it = vregToPhys.find(vreg);
    if (it != vregToPhys.end()) return it->second;
    return 1;
}

} // namespace fpgagpu
