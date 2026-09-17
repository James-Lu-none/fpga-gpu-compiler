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
#include <set>

#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InlineAsm.h>

namespace fpgagpu {

static void runDivergencePass(llvm::Function &F) {
    if (F.isDeclaration()) return;

    // 1. Name all blocks
    uint32_t bbIdx = 0;
    for (auto &BB : F) {
        if (!BB.hasName()) {
            BB.setName("bb." + std::to_string(bbIdx));
        }
        bbIdx++;
    }

    llvm::PostDominatorTree PDT(F);
    llvm::DominatorTree DT(F);

    std::vector<llvm::BranchInst*> condBranches;
    for (auto &BB : F) {
        if (auto *BI = llvm::dyn_cast<llvm::BranchInst>(BB.getTerminator())) {
            if (BI->isConditional()) {
                condBranches.push_back(BI);
            }
        }
    }

    llvm::LLVMContext &ctx = F.getContext();
    llvm::FunctionType *fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false);

    for (auto *BI : condBranches) {
        llvm::BasicBlock *BB = BI->getParent();
        llvm::BasicBlock *IPD = nullptr;
        if (auto *node = PDT.getNode(BB)) {
            if (auto *idom = node->getIDom()) {
                IPD = idom->getBlock();
            }
        }

        if (!IPD) continue; // No reconvergence point

        // Insert SSY IPD before branch
        std::string ssyAsm = "ssy " + IPD->getName().str();
        auto *ssyInlineAsm = llvm::InlineAsm::get(fty, ssyAsm, "", true);
        llvm::CallInst::Create(fty, ssyInlineAsm, "", BI);

        // Find predecessors of IPD dominated by BB
        std::vector<llvm::BasicBlock*> preds;
        for (auto *pred : llvm::predecessors(IPD)) {
            if (DT.dominates(BB, pred)) {
                preds.push_back(pred);
            }
        }

        for (auto *pred : preds) {
            llvm::BasicBlock *SyncBB = llvm::BasicBlock::Create(ctx, pred->getName() + ".sync", &F);
            llvm::BranchInst::Create(IPD, SyncBB);
            auto *syncInlineAsm = llvm::InlineAsm::get(fty, "sync", "", true);
            llvm::CallInst::Create(fty, syncInlineAsm, "", SyncBB->getTerminator());

            llvm::Instruction *term = pred->getTerminator();
            for (unsigned i = 0; i < term->getNumSuccessors(); i++) {
                if (term->getSuccessor(i) == IPD) {
                    term->setSuccessor(i, SyncBB);
                }
            }
        }
    }
}

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

    // Run our custom Divergence Pass to insert SSY and SYNC
    for (llvm::Function &F : M) {
        runDivergencePass(F);
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
