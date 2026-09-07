#include "FpgaGpuBackend.h"
#include "FpgaGpuISel.h"
#include "FpgaGpuRegAlloc.h"
#include "FpgaGpuEmitter.h"

#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Scalar/DCE.h>

#include <fstream>
#include <iostream>

namespace fpgagpu {

CompilerBackend::CompilerBackend() : context(std::make_unique<llvm::LLVMContext>()) {}
CompilerBackend::~CompilerBackend() = default;

bool CompilerBackend::compileFile(const std::string &inputFilename,
                                  const std::string &outputFilename,
                                  OutputFormat format,
                                  bool optimize) {
    llvm::SMDiagnostic err;
    std::unique_ptr<llvm::Module> module = llvm::parseIRFile(inputFilename, err, *context);

    if (!module) {
        std::cerr << "Error parsing IR file: " << inputFilename << "\n";
        err.print("fpgagpu-llc", llvm::errs());
        return false;
    }

    return compileModule(*module, outputFilename, format, optimize);
}

bool CompilerBackend::compileModule(llvm::Module &M,
                                    const std::string &outputFilename,
                                    OutputFormat format,
                                    bool optimize) {
    if (optimize) {
        // Run standard LLVM optimization passes
        llvm::PassBuilder PB;
        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;

        PB.registerModuleAnalyses(MAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

        llvm::FunctionPassManager FPM;
        FPM.addPass(llvm::PromotePass()); // mem2reg
        FPM.addPass(llvm::InstCombinePass());
        FPM.addPass(llvm::SimplifyCFGPass());
        FPM.addPass(llvm::DCEPass());

        for (auto &F : M) {
            if (!F.isDeclaration()) {
                FPM.run(F, FAM);
            }
        }
    }

    // Collect all basic blocks across non-declaration functions
    std::vector<BasicBlockCode> allBlocks;

    for (llvm::Function &F : M) {
        if (F.isDeclaration()) continue;

        InstructionSelector isel(F);
        auto funcBlocks = isel.selectInstructions();
        allBlocks.insert(allBlocks.end(),
                         std::make_move_iterator(funcBlocks.begin()),
                         std::make_move_iterator(funcBlocks.end()));
    }

    if (allBlocks.empty()) {
        std::cerr << "Warning: No code generated (module contains no non-declaration functions)\n";
        return false;
    }

    // Allocate physical registers
    RegisterAllocator regAlloc;
    if (!regAlloc.allocate(allBlocks)) {
        std::cerr << "Error: Register allocation failed\n";
        return false;
    }

    // Emit output file
    std::ofstream outFile;
    if (format == OutputFormat::BINARY) {
        outFile.open(outputFilename, std::ios::out | std::ios::binary);
    } else {
        outFile.open(outputFilename, std::ios::out);
    }

    if (!outFile.is_open()) {
        std::cerr << "Error opening output file: " << outputFilename << "\n";
        return false;
    }

    CodeEmitter emitter(allBlocks);
    switch (format) {
        case OutputFormat::ASSEMBLY:
            emitter.emitAssembly(outFile);
            break;
        case OutputFormat::HEX:
            emitter.emitHex(outFile);
            break;
        case OutputFormat::BINARY:
            emitter.emitBinary(outFile);
            break;
    }

    outFile.close();
    return true;
}

} // namespace fpgagpu
