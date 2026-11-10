#include "FpgaGpuRegAlloc.h"
#include "FpgaGpuAssembler.h"
#include "FpgaGpuEmitter.h"
#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

using namespace fpgagpu;

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static MachineInstruction inst(Opcode op, uint8_t rd = 0, uint8_t a = 0,
                               uint8_t b = 0, int32_t imm = 0) {
    MachineInstruction mi;
    mi.op = op; mi.rd = rd; mi.rs1 = a; mi.rs2 = b; mi.imm = imm;
    return mi;
}

// Compare observable stores before/after allocation; asymmetric SUB operands
// expose accidental operand swapping and scratch-register clobbering.
static std::unordered_map<uint32_t, uint32_t> run(const std::vector<BasicBlockCode> &blocks) {
    std::array<uint32_t, 256> regs{};
    std::unordered_map<uint32_t, uint32_t> memory;
    for (const auto &bb : blocks) for (const auto &mi : bb.instructions) {
        switch (mi.op) {
            case Opcode::ADDI: regs[mi.rd] = regs[mi.rs1] + mi.imm; break;
            case Opcode::SUB: regs[mi.rd] = regs[mi.rs1] - regs[mi.rs2]; break;
            case Opcode::MUL: regs[mi.rd] = regs[mi.rs1] * regs[mi.rs2]; break;
            case Opcode::STR: memory[regs[mi.rs1]] = regs[mi.rs2]; break;
            default: break;
        }
        regs[0] = 0;
    }
    return memory;
}

static void allocationAndRepair() {
    // a/b, b/c, c/a form an odd cycle: a fixed bank assignment cannot satisfy
    // all three pairs, so at least one copy is necessary.
    std::vector<BasicBlockCode> blocks{{"entry", {
        inst(Opcode::ADDI, 1, 0, 0, 7),
        inst(Opcode::ADDI, 2, 0, 0, 3),
        inst(Opcode::ADDI, 3, 0, 0, 2),
        inst(Opcode::SUB, 4, 1, 2),
        inst(Opcode::SUB, 5, 2, 3),
        inst(Opcode::SUB, 6, 3, 1),
        inst(Opcode::ADDI, 7, 0, 0, 64),
        inst(Opcode::STR, 0, 7, 4),
        inst(Opcode::ADDI, 7, 7, 0, 4),
        inst(Opcode::STR, 0, 7, 5),
        inst(Opcode::ADDI, 7, 7, 0, 4),
        inst(Opcode::STR, 0, 7, 6)
    }}};
    const auto expected = run(blocks);
    const size_t originalSize = blocks[0].instructions.size();
    RegisterAllocator allocator;
    require(allocator.allocate(blocks), "bank-aware allocation failed");
    require(RegisterAllocator::verifyBanks(blocks), "unrepaired conflict");
    require(blocks[0].instructions.size() > originalSize, "odd cycle did not insert a copy");
    require(run(blocks) == expected, "repair changed SUB/store semantics");
    require(expected.at(64) == 4 && expected.at(68) == 1 &&
            expected.at(72) == uint32_t(-5), "bad test reference");

    // Added copies must be counted by the subsequent branch/SSY relocation.
    auto branch = inst(Opcode::BR);
    branch.labelTarget = "end";
    blocks.insert(blocks.begin(), {"start", {branch}});
    blocks.push_back({"end", {inst(Opcode::EXIT)}});
    CodeEmitter emitter(blocks);
    const auto resolved = emitter.resolveBranchOffsets();
    require(resolved[0].imm == int32_t(resolved.size() - 1), "branch offset ignored bank copies");
}

static void registerPressure() {
    std::vector<BasicBlockCode> blocks{{"pressure", {}}};
    for (uint8_t r = 1; r <= 29; ++r)
        blocks[0].instructions.push_back(inst(Opcode::ADDI, r, 0, 0, r));
    for (uint8_t r = 2; r <= 29; ++r) {
        blocks[0].instructions.push_back(inst(Opcode::STR, 0, 1, r));
        blocks[0].instructions.push_back(inst(Opcode::ADDI, 1, 1, 0, 4));
    }
    const auto expected = run(blocks);
    RegisterAllocator allocator;
    require(allocator.allocate(blocks) && run(blocks) == expected,
            "scratch repair clobbered a live value");

    blocks = {{"overflow", {}}};
    for (uint8_t r = 1; r <= 30; ++r)
        blocks[0].instructions.push_back(inst(Opcode::ADDI, r, 0, 0, r));
    for (uint8_t r = 2; r <= 30; ++r)
        blocks[0].instructions.push_back(inst(Opcode::STR, 0, 1, r));
    require(!allocator.allocate(blocks), "register overflow reused repair scratch");
}

static bool assemble(const char *source) {
    Assembler assembler;
    std::istringstream input(source);
    std::vector<uint32_t> words;
    return assembler.assemble(input, words);
}

int main() {
    try {
        allocationAndRepair();
        registerPressure();
        require(!assemble("SUB R1, R2, R4\n"), "accepted even/even conflict");
        require(!assemble("MUL R2, R1, R3\n"), "accepted odd/odd conflict");
        require(!assemble("STR R3, [R1]\n"), "accepted store conflict");
        require(!assemble("SETCC.Z R1, R2, R4\n"), "accepted compare conflict");
        require(assemble("ADD R1, R2, R3\nSUB R1, R3, R2\n"), "rejected opposite banks");
        require(assemble("MUL R1, R2, R2\nADD R1, R0, R2\nADD R1, R2, R0\n"), "rejected broadcast/R0");
        require(assemble("ADDI R1, R2, 8191\nBR R2, 7\nLDR R1, [R2]\n"), "decoded immediate/predicate as second source");
        require(!RegisterAllocator::verifyBanks({{"bad", {inst(Opcode::SUB, 1, 2, 4)}}}),
                "verifier accepted conflicting operands");
        std::cout << "PASS odd/even bank allocation, repair semantics, pressure, relocation and verifier\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
