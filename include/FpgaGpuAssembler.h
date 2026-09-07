#ifndef FPGA_GPU_ASSEMBLER_H
#define FPGA_GPU_ASSEMBLER_H

#include "FpgaGpu.h"
#include <string>
#include <vector>
#include <istream>
#include <ostream>
#include <unordered_map>

namespace fpgagpu {

class Assembler {
public:
    Assembler() = default;

    // Assembles human-readable assembly from an input stream into machine instructions
    bool assemble(std::istream &is, std::vector<uint32_t> &machineCode);

    // Disassembles a 32-bit machine word into human-readable assembly
    static std::string disassemble(uint32_t word);

private:
    bool parseLine(const std::string &line, size_t lineNum,
                   std::vector<MachineInstruction> &instructions,
                   std::unordered_map<std::string, size_t> &labels);
};

} // namespace fpgagpu

#endif // FPGA_GPU_ASSEMBLER_H
