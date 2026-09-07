#ifndef FPGA_GPU_EMITTER_H
#define FPGA_GPU_EMITTER_H

#include "FpgaGpu.h"
#include "FpgaGpuISel.h"
#include <string>
#include <vector>
#include <ostream>

namespace fpgagpu {

class CodeEmitter {
public:
    explicit CodeEmitter(const std::vector<BasicBlockCode> &blocks);

    // Resolves branch labels and computes relative PC offsets (imm19)
    std::vector<MachineInstruction> resolveBranchOffsets();

    // Emits human-readable assembly (.s)
    void emitAssembly(std::ostream &os);

    // Emits hexadecimal machine code (.hex, 8 hex digits per line)
    void emitHex(std::ostream &os);

    // Emits raw binary (.bin, 4 bytes per word, little-endian)
    void emitBinary(std::ostream &os);

private:
    const std::vector<BasicBlockCode> &basicBlocks;
};

} // namespace fpgagpu

#endif // FPGA_GPU_EMITTER_H
