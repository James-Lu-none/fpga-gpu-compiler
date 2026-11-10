#include "FpgaGpuRegAlloc.h"
#include <iostream>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <vector>
#include <limits>

namespace fpgagpu {

bool RegisterAllocator::allocate(std::vector<BasicBlockCode> &blocks) {
    vregToPhys.clear();

    struct LiveRange {
        uint32_t birth{UINT32_MAX};
        uint32_t death{0};
    };

    std::unordered_map<VReg, LiveRange> intervals;
    std::unordered_map<VReg, std::unordered_map<VReg, unsigned>> partners;

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
            if (mi.readsBothSources() && mi.rs1 != 0 && mi.rs2 != 0 && mi.rs1 != mi.rs2) {
                ++partners[mi.rs1][mi.rs2];
                ++partners[mi.rs2][mi.rs1];
            }

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
        if (a.second.death != b.second.death) return a.second.death < b.second.death;
        return a.first < b.first;
    });

    // Step 3: Prefer a bank opposite already-assigned source partners.
    // Keep one scratch in each bank for non-bipartite constraints / pressure.
    std::set<uint8_t> freePool;
    for (uint8_t r = 1; r < NUM_PHYSICAL_REGS - 2; r++) {
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
            std::cerr << "Error: Register pressure exceeds R1..R29; R30/R31 are reserved for bank repair (spilling is not supported).\n";
            return false;
        }

        // Ties retain the lowest physical index for deterministic output.
        uint8_t phys = *freePool.begin();
        unsigned bestCost = std::numeric_limits<unsigned>::max();
        for (uint8_t candidate : freePool) {
            unsigned cost = 0;
            for (const auto &[other, weight] : partners[vreg]) {
                auto assigned = vregToPhys.find(other);
                if (assigned != vregToPhys.end() && ((assigned->second ^ candidate) & 1) == 0)
                    cost += weight;
            }
            if (cost < bestCost) { bestCost = cost; phys = candidate; }
        }
        freePool.erase(phys);

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

    // Step 5: Resolve remaining conflicts before label/PC resolution. ADDI
    // copies preserve active masks and do not change the arithmetic operands'
    // order (essential for SUB, comparisons and stores).
    for (auto &bb : blocks) {
        std::vector<MachineInstruction> repaired;
        for (auto mi : bb.instructions) {
            if (mi.hasRegisterBankConflict()) {
                MachineInstruction copy;
                copy.op = Opcode::ADDI;
                copy.rd = (mi.rs1 & 1) ? 30 : 31;
                copy.rs1 = mi.rs2;
                copy.imm = 0;
                copy.comment = "odd/even bank repair";
                mi.rs2 = copy.rd;
                repaired.push_back(copy);
            }
            repaired.push_back(mi);
        }
        bb.instructions = std::move(repaired);
    }
    return verifyBanks(blocks);
}

bool RegisterAllocator::verifyBanks(const std::vector<BasicBlockCode> &blocks) {
    for (const auto &bb : blocks) {
        for (size_t i = 0; i < bb.instructions.size(); ++i) {
            if (bb.instructions[i].hasRegisterBankConflict()) {
                std::cerr << "Error: register bank conflict in " << bb.name
                          << " instruction " << i << '\n';
                return false;
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
