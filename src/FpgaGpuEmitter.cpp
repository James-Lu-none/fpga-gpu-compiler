#include "FpgaGpuEmitter.h"
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace fpgagpu {

CodeEmitter::CodeEmitter(const std::vector<BasicBlockCode> &blocks)
    : basicBlocks(blocks) {}

std::vector<MachineInstruction> CodeEmitter::resolveBranchOffsets() {
    std::unordered_map<std::string, size_t> blockToPC;
    size_t pc = 0;

    // First pass: Record PC for each basic block label
    for (const auto &bb : basicBlocks) {
        blockToPC[bb.name] = pc;
        pc += bb.instructions.size();
    }

    std::vector<MachineInstruction> resolved;
    resolved.reserve(pc);

    // Second pass: Calculate relative branch offsets
    size_t currentPC = 0;
    for (const auto &bb : basicBlocks) {
        for (auto mi : bb.instructions) {
            if (mi.op == Opcode::BR && !mi.labelTarget.empty()) {
                if (auto it = blockToPC.find(mi.labelTarget); it != blockToPC.end()) {
                    int32_t offset = static_cast<int32_t>(it->second) - static_cast<int32_t>(currentPC);
                    mi.imm = offset;
                }
            }
            resolved.push_back(mi);
            currentPC++;
        }
    }

    return resolved;
}

static std::string condToString(uint8_t cond) {
    switch (cond) {
        case BranchCond::EQ: return "Z";
        case BranchCond::NE: return "NP";
        case BranchCond::LT: return "N";
        case BranchCond::LE: return "NZ";
        case BranchCond::GT: return "P";
        case BranchCond::GE: return "ZP";
        case BranchCond::ALWAYS: return "NZP";
        default: return "NZP";
    }
}

static std::string sysRegToString(uint16_t sr) {
    switch (static_cast<SysReg>(sr)) {
        case SysReg::TID_X: return "SR_TID.X";
        case SysReg::TID_Y: return "SR_TID.Y";
        case SysReg::BID_X: return "SR_BID.X";
        case SysReg::BID_Y: return "SR_BID.Y";
        default: return "SR_TID.X";
    }
}

void CodeEmitter::emitAssembly(std::ostream &os) {
    for (const auto &bb : basicBlocks) {
        os << bb.name << ":\n";
        for (const auto &mi : bb.instructions) {
            os << "    ";
            switch (mi.op) {
                case Opcode::ADD:
                    os << "ADD R" << (int)mi.rd << ", R" << (int)mi.rs1 << ", R" << (int)mi.rs2;
                    break;
                case Opcode::SUB:
                    os << "SUB R" << (int)mi.rd << ", R" << (int)mi.rs1 << ", R" << (int)mi.rs2;
                    break;
                case Opcode::MUL:
                    os << "MUL R" << (int)mi.rd << ", R" << (int)mi.rs1 << ", R" << (int)mi.rs2;
                    break;
                case Opcode::CMP:
                    os << "CMP R" << (int)mi.rs1 << ", R" << (int)mi.rs2;
                    break;
                case Opcode::ADDI:
                    os << "ADDI R" << (int)mi.rd << ", R" << (int)mi.rs1 << ", " << mi.imm;
                    break;
                case Opcode::LDR:
                    os << "LDR R" << (int)mi.rd << ", [R" << (int)mi.rs1 << "]";
                    break;
                case Opcode::STR:
                    os << "STR R" << (int)mi.rs2 << ", [R" << (int)mi.rs1 << "]";
                    break;
                case Opcode::S2R:
                    os << "S2R R" << (int)mi.rd << ", " << sysRegToString(mi.imm);
                    break;
                case Opcode::BR:
                    os << "BR " << condToString(mi.cond) << ", "
                       << (mi.labelTarget.empty() ? std::to_string(mi.imm) : mi.labelTarget);
                    break;
                case Opcode::SYNC:
                    os << "SYNC";
                    break;
                case Opcode::EXIT:
                    os << "EXIT";
                    break;
            }
            if (!mi.comment.empty()) {
                os << " ; " << mi.comment;
            }
            os << "\n";
        }
    }
}

void CodeEmitter::emitHex(std::ostream &os) {
    auto resolved = resolveBranchOffsets();
    for (const auto &mi : resolved) {
        uint32_t word = mi.encode();
        os << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << word << "\n";
    }
}

void CodeEmitter::emitBinary(std::ostream &os) {
    auto resolved = resolveBranchOffsets();
    for (const auto &mi : resolved) {
        uint32_t word = mi.encode();
        // Little-endian
        char bytes[4];
        bytes[0] = static_cast<char>(word & 0xFF);
        bytes[1] = static_cast<char>((word >> 8) & 0xFF);
        bytes[2] = static_cast<char>((word >> 16) & 0xFF);
        bytes[3] = static_cast<char>((word >> 24) & 0xFF);
        os.write(bytes, 4);
    }
}

} // namespace fpgagpu
