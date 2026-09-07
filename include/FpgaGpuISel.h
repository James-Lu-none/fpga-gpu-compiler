#ifndef FPGA_GPU_ISEL_H
#define FPGA_GPU_ISEL_H

#include "FpgaGpu.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <unordered_map>
#include <vector>
#include <string>

namespace fpgagpu {

// Virtual Register representation during ISel
using VReg = uint32_t;
constexpr VReg VREG_ZERO = 0; // Fixed physical register R0

struct BasicBlockCode {
    std::string name;
    std::vector<MachineInstruction> instructions;
};

class InstructionSelector {
public:
    explicit InstructionSelector(llvm::Function &F);

    // Runs instruction selection across all basic blocks in the function
    std::vector<BasicBlockCode> selectInstructions();

private:
    llvm::Function &func;
    VReg nextVReg{1};
    std::unordered_map<const llvm::Value*, VReg> valueToVReg;

    VReg getOrCreateVReg(const llvm::Value *val);
    VReg allocateVReg();

    void selectBasicBlock(llvm::BasicBlock &BB, BasicBlockCode &bbCode);
    void selectInstruction(llvm::Instruction &I, BasicBlockCode &bbCode);

    // Handlers for specific instruction types
    void selectBinaryOp(llvm::BinaryOperator &BO, BasicBlockCode &bbCode);
    void selectCmp(llvm::ICmpInst &CI, BasicBlockCode &bbCode);
    void selectBranch(llvm::BranchInst &BI, BasicBlockCode &bbCode);
    void selectLoad(llvm::LoadInst &LI, BasicBlockCode &bbCode);
    void selectStore(llvm::StoreInst &SI, BasicBlockCode &bbCode);
    void selectCall(llvm::CallInst &CI, BasicBlockCode &bbCode);
    void selectReturn(llvm::ReturnInst &RI, BasicBlockCode &bbCode);
};

} // namespace fpgagpu

#endif // FPGA_GPU_ISEL_H
