#ifndef FPGA_GPU_BACKEND_H
#define FPGA_GPU_BACKEND_H

#include "FpgaGpu.h"
#include <string>
#include <vector>
#include <memory>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace fpgagpu {

enum class OutputFormat {
    ASSEMBLY, // .s
    HEX,      // .hex
    BINARY    // .bin
};

class CompilerBackend {
public:
    CompilerBackend();
    ~CompilerBackend();

    // Compiles an LLVM IR file (.ll or .bc) into the specified output format
    bool compileFile(const std::string &inputFilename,
                     const std::string &outputFilename,
                     OutputFormat format,
                     bool optimize = true);

    // Compiles an in-memory LLVM Module
    bool compileModule(llvm::Module &M,
                       const std::string &outputFilename,
                       OutputFormat format,
                       bool optimize = true);

private:
    std::unique_ptr<llvm::LLVMContext> context;
};

} // namespace fpgagpu

#endif // FPGA_GPU_BACKEND_H
