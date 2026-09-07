#ifndef FPGA_GPU_H
#define FPGA_GPU_H

#include <cstdint>
#include <string>
#include <vector>
#include <string_view>

namespace fpgagpu {

// Hardware Constants
constexpr uint32_t NUM_PHYSICAL_REGS = 32;
constexpr uint32_t IRAM_DEPTH = 1024; // 4KB (1024 words)
constexpr uint32_t VECTOR_LANES = 2;
constexpr uint32_t LANE_WIDTH_BITS = 32;

// Hardware Instruction Opcodes (8-bit)
enum class Opcode : uint8_t {
    ADD  = 0x01, // R-Type: VRF[rd] = VRF[rs1] + VRF[rs2]
    SUB  = 0x02, // R-Type: VRF[rd] = VRF[rs1] - VRF[rs2]
    MUL  = 0x03, // R-Type: VRF[rd] = VRF[rs1] * VRF[rs2]
    CMP  = 0x04, // R-Type: NZP = VRF[rs1] - VRF[rs2]
    ADDI = 0x81, // I-Type: VRF[rd] = VRF[rs1] + imm14
    LDR  = 0xA0, // I-Type: VRF[rd] = Mem[VRF[rs1]]
    STR  = 0xA1, // R-Type: Mem[VRF[rs1]] = VRF[rs2]
    S2R  = 0xB0, // I-Type: VRF[rd] = SysReg[imm]
    BR   = 0xC0, // J-Type: if (NZP & cond) PC += imm19
    SYNC = 0xE0, // J-Type: Pop SIMT Stack (Reconvergence)
    EXIT = 0xFF  // J-Type: Terminate Warp
};

// System Registers for S2R Instruction
enum class SysReg : uint16_t {
    TID_X = 0, // Thread ID X
    TID_Y = 1, // Thread ID Y
    BID_X = 2, // Block ID X
    BID_Y = 3  // Block ID Y
};

// Branch Condition Codes (NZP 3-bit mask)
namespace BranchCond {
    constexpr uint8_t NEVER  = 0x0;
    constexpr uint8_t P      = 0x1; // Positive (> 0)
    constexpr uint8_t Z      = 0x2; // Zero (== 0)
    constexpr uint8_t ZP     = 0x3; // Zero or Positive (>= 0)
    constexpr uint8_t N      = 0x4; // Negative (< 0)
    constexpr uint8_t NP     = 0x5; // Not Zero (!= 0)
    constexpr uint8_t NZ     = 0x6; // Negative or Zero (<= 0)
    constexpr uint8_t ALWAYS = 0x7; // Always taken

    // Aliases
    constexpr uint8_t EQ = Z;
    constexpr uint8_t NE = NP;
    constexpr uint8_t LT = N;
    constexpr uint8_t LE = NZ;
    constexpr uint8_t GT = P;
    constexpr uint8_t GE = ZP;
}

// Instruction Format
enum class Format {
    R_TYPE, // [31:24]=Op, [23:19]=Rd, [18:14]=Rs1, [13:9]=Rs2
    I_TYPE, // [31:24]=Op, [23:19]=Rd, [18:14]=Rs1, [13:0]=Imm14
    J_TYPE  // [31:24]=Op, [21:19]=Cond, [18:0]=Imm19
};

// Internal Machine Instruction Representation
struct MachineInstruction {
    Opcode op;
    uint8_t rd{0};
    uint8_t rs1{0};
    uint8_t rs2{0};
    int32_t imm{0};
    uint8_t cond{BranchCond::ALWAYS};
    std::string labelTarget; // Label for branch targets before resolution
    std::string comment;

    // Encodes instruction into 32-bit hardware machine word
    uint32_t encode() const {
        uint32_t opcodeVal = static_cast<uint32_t>(op);
        switch (op) {
            case Opcode::ADD:
            case Opcode::SUB:
            case Opcode::MUL:
                return (opcodeVal << 24) |
                       ((static_cast<uint32_t>(rd) & 0x1F) << 19) |
                       ((static_cast<uint32_t>(rs1) & 0x1F) << 14) |
                       ((static_cast<uint32_t>(rs2) & 0x1F) << 9);

            case Opcode::CMP:
                return (opcodeVal << 24) |
                       ((static_cast<uint32_t>(rs1) & 0x1F) << 14) |
                       ((static_cast<uint32_t>(rs2) & 0x1F) << 9);

            case Opcode::STR:
                // STR rs2(data), [rs1(addr)]
                return (opcodeVal << 24) |
                       ((static_cast<uint32_t>(rs1) & 0x1F) << 14) |
                       ((static_cast<uint32_t>(rs2) & 0x1F) << 9);

            case Opcode::ADDI:
                return (opcodeVal << 24) |
                       ((static_cast<uint32_t>(rd) & 0x1F) << 19) |
                       ((static_cast<uint32_t>(rs1) & 0x1F) << 14) |
                       (static_cast<uint32_t>(imm) & 0x3FFF);

            case Opcode::LDR:
                // LDR rd, [rs1]
                return (opcodeVal << 24) |
                       ((static_cast<uint32_t>(rd) & 0x1F) << 19) |
                       ((static_cast<uint32_t>(rs1) & 0x1F) << 14);

            case Opcode::S2R:
                return (opcodeVal << 24) |
                       ((static_cast<uint32_t>(rd) & 0x1F) << 19) |
                       (static_cast<uint32_t>(imm) & 0x3FFF);

            case Opcode::BR:
                return (opcodeVal << 24) |
                       ((static_cast<uint32_t>(cond) & 0x7) << 19) |
                       (static_cast<uint32_t>(imm) & 0x7FFFF);

            case Opcode::SYNC:
            case Opcode::EXIT:
                return (opcodeVal << 24);

            default:
                return 0;
        }
    }
};

} // namespace fpgagpu

#endif // FPGA_GPU_H
