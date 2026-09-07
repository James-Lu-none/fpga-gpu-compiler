#include "FpgaGpuISel.h"
#include <llvm/IR/Module.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/Support/raw_ostream.h>
#include <cctype>
#include <algorithm>

namespace fpgagpu {

InstructionSelector::InstructionSelector(llvm::Function &F) : func(F) {}

VReg InstructionSelector::getOrCreateVReg(const llvm::Value *val) {
    if (auto it = valueToVReg.find(val); it != valueToVReg.end()) {
        return it->second;
    }
    VReg reg = allocateVReg();
    valueToVReg[val] = reg;
    return reg;
}

VReg InstructionSelector::allocateVReg() {
    return nextVReg++;
}

std::vector<BasicBlockCode> InstructionSelector::selectInstructions() {
    std::vector<BasicBlockCode> code;

    for (llvm::BasicBlock &BB : func) {
        BasicBlockCode bbCode;
        bbCode.name = BB.getName().str();
        if (bbCode.name.empty()) {
            bbCode.name = "bb." + std::to_string(code.size());
        }
        selectBasicBlock(BB, bbCode);
        code.push_back(std::move(bbCode));
    }

    return code;
}

void InstructionSelector::selectBasicBlock(llvm::BasicBlock &BB, BasicBlockCode &bbCode) {
    for (llvm::Instruction &I : BB) {
        selectInstruction(I, bbCode);
    }
}

void InstructionSelector::selectInstruction(llvm::Instruction &I, BasicBlockCode &bbCode) {
    if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
        selectBinaryOp(*BO, bbCode);
    } else if (auto *CI = llvm::dyn_cast<llvm::ICmpInst>(&I)) {
        selectCmp(*CI, bbCode);
    } else if (auto *BI = llvm::dyn_cast<llvm::BranchInst>(&I)) {
        selectBranch(*BI, bbCode);
    } else if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
        selectLoad(*LI, bbCode);
    } else if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
        selectStore(*SI, bbCode);
    } else if (auto *CallI = llvm::dyn_cast<llvm::CallInst>(&I)) {
        selectCall(*CallI, bbCode);
    } else if (auto *RI = llvm::dyn_cast<llvm::ReturnInst>(&I)) {
        selectReturn(*RI, bbCode);
    } else if (auto *AllocaI = llvm::dyn_cast<llvm::AllocaInst>(&I)) {
        // We assign a virtual register for stack/local address
        (void)getOrCreateVReg(AllocaI);
    } else if (auto *CastI = llvm::dyn_cast<llvm::CastInst>(&I)) {
        // Bitcast, PtrToInt, IntToPtr, ZExt, SExt simply reuse or copy operand
        VReg src = getOrCreateVReg(CastI->getOperand(0));
        VReg dst = getOrCreateVReg(CastI);
        // ADDI dst, src, 0 (register move)
        MachineInstruction mi;
        mi.op = Opcode::ADDI;
        mi.rd = dst;
        mi.rs1 = src;
        mi.imm = 0;
        mi.comment = "cast/move";
        bbCode.instructions.push_back(mi);
    } else if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(&I)) {
        // Simple pointer arithmetic: base + offset * elem_size
        VReg baseReg = getOrCreateVReg(GEP->getPointerOperand());
        VReg dstReg = getOrCreateVReg(GEP);
        
        // Compute offset if variable or constant
        if (GEP->hasAllConstantIndices()) {
            llvm::APInt offset(64, 0);
            const auto &DL = func.getParent()->getDataLayout();
            if (GEP->accumulateConstantOffset(DL, offset)) {
                int64_t offVal = offset.getSExtValue();
                MachineInstruction mi;
                mi.op = Opcode::ADDI;
                mi.rd = dstReg;
                mi.rs1 = baseReg;
                mi.imm = static_cast<int32_t>(offVal);
                mi.comment = "gep constant offset";
                bbCode.instructions.push_back(mi);
                return;
            }
        }
        
        // For non-constant indices, add the index operand directly
        VReg idxReg = getOrCreateVReg(GEP->getOperand(GEP->getNumOperands() - 1));
        MachineInstruction mi;
        mi.op = Opcode::ADD;
        mi.rd = dstReg;
        mi.rs1 = baseReg;
        mi.rs2 = idxReg;
        mi.comment = "gep dynamic offset";
        bbCode.instructions.push_back(mi);
    }
}

void InstructionSelector::selectBinaryOp(llvm::BinaryOperator &BO, BasicBlockCode &bbCode) {
    VReg rd = getOrCreateVReg(&BO);
    llvm::Value *op0 = BO.getOperand(0);
    llvm::Value *op1 = BO.getOperand(1);

    MachineInstruction mi;
    mi.rd = rd;

    switch (BO.getOpcode()) {
        case llvm::Instruction::Add: {
            if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(op1)) {
                int64_t val = CI->getSExtValue();
                if (val >= -8192 && val <= 8191) {
                    mi.op = Opcode::ADDI;
                    mi.rs1 = getOrCreateVReg(op0);
                    mi.imm = static_cast<int32_t>(val);
                    mi.comment = "addi";
                    bbCode.instructions.push_back(mi);
                    return;
                }
            }
            mi.op = Opcode::ADD;
            mi.rs1 = getOrCreateVReg(op0);
            mi.rs2 = getOrCreateVReg(op1);
            mi.comment = "add";
            break;
        }
        case llvm::Instruction::Sub: {
            mi.op = Opcode::SUB;
            mi.rs1 = getOrCreateVReg(op0);
            mi.rs2 = getOrCreateVReg(op1);
            mi.comment = "sub";
            break;
        }
        case llvm::Instruction::Mul: {
            mi.op = Opcode::MUL;
            mi.rs1 = getOrCreateVReg(op0);
            mi.rs2 = getOrCreateVReg(op1);
            mi.comment = "mul";
            break;
        }
        default:
            // Fallback to ADD
            mi.op = Opcode::ADD;
            mi.rs1 = getOrCreateVReg(op0);
            mi.rs2 = getOrCreateVReg(op1);
            break;
    }

    bbCode.instructions.push_back(mi);
}

void InstructionSelector::selectCmp(llvm::ICmpInst &CI, BasicBlockCode &bbCode) {
    // CMP rs1, rs2 (updates NZP condition codes in ALU)
    MachineInstruction mi;
    mi.op = Opcode::CMP;
    mi.rs1 = getOrCreateVReg(CI.getOperand(0));
    mi.rs2 = getOrCreateVReg(CI.getOperand(1));
    mi.comment = "cmp";
    bbCode.instructions.push_back(mi);
}

void InstructionSelector::selectBranch(llvm::BranchInst &BI, BasicBlockCode &bbCode) {
    if (BI.isUnconditional()) {
        MachineInstruction mi;
        mi.op = Opcode::BR;
        mi.cond = BranchCond::ALWAYS;
        mi.labelTarget = BI.getSuccessor(0)->getName().str();
        if (mi.labelTarget.empty()) mi.labelTarget = "bb." + std::to_string(reinterpret_cast<uintptr_t>(BI.getSuccessor(0)));
        mi.comment = "unconditional branch";
        bbCode.instructions.push_back(mi);
    } else {
        // Conditional Branch
        llvm::Value *condVal = BI.getCondition();
        uint8_t branchCond = BranchCond::NE; // default != 0

        if (auto *CI = llvm::dyn_cast<llvm::ICmpInst>(condVal)) {
            switch (CI->getPredicate()) {
                case llvm::CmpInst::ICMP_EQ:  branchCond = BranchCond::EQ; break;
                case llvm::CmpInst::ICMP_NE:  branchCond = BranchCond::NE; break;
                case llvm::CmpInst::ICMP_SLT:
                case llvm::CmpInst::ICMP_ULT: branchCond = BranchCond::LT; break;
                case llvm::CmpInst::ICMP_SLE:
                case llvm::CmpInst::ICMP_ULE: branchCond = BranchCond::LE; break;
                case llvm::CmpInst::ICMP_SGT:
                case llvm::CmpInst::ICMP_UGT: branchCond = BranchCond::GT; break;
                case llvm::CmpInst::ICMP_SGE:
                case llvm::CmpInst::ICMP_UGE: branchCond = BranchCond::GE; break;
                default:                      branchCond = BranchCond::NE; break;
            }
        }

        // Branch to True target if condition holds
        MachineInstruction miTrue;
        miTrue.op = Opcode::BR;
        miTrue.cond = branchCond;
        miTrue.labelTarget = BI.getSuccessor(0)->getName().str();
        if (miTrue.labelTarget.empty()) miTrue.labelTarget = "bb." + std::to_string(reinterpret_cast<uintptr_t>(BI.getSuccessor(0)));
        miTrue.comment = "branch taken";
        bbCode.instructions.push_back(miTrue);

        // Followed by Unconditional Branch to False target
        MachineInstruction miFalse;
        miFalse.op = Opcode::BR;
        miFalse.cond = BranchCond::ALWAYS;
        miFalse.labelTarget = BI.getSuccessor(1)->getName().str();
        if (miFalse.labelTarget.empty()) miFalse.labelTarget = "bb." + std::to_string(reinterpret_cast<uintptr_t>(BI.getSuccessor(1)));
        miFalse.comment = "branch fallthrough/else";
        bbCode.instructions.push_back(miFalse);
    }
}

void InstructionSelector::selectLoad(llvm::LoadInst &LI, BasicBlockCode &bbCode) {
    VReg rd = getOrCreateVReg(&LI);
    VReg rs1 = getOrCreateVReg(LI.getPointerOperand());

    MachineInstruction mi;
    mi.op = Opcode::LDR;
    mi.rd = rd;
    mi.rs1 = rs1;
    mi.comment = "ldr";
    bbCode.instructions.push_back(mi);
}

void InstructionSelector::selectStore(llvm::StoreInst &SI, BasicBlockCode &bbCode) {
    VReg val = getOrCreateVReg(SI.getValueOperand());
    VReg ptr = getOrCreateVReg(SI.getPointerOperand());

    MachineInstruction mi;
    mi.op = Opcode::STR;
    mi.rs1 = ptr; // Address
    mi.rs2 = val; // Data
    mi.comment = "str";
    bbCode.instructions.push_back(mi);
}

void InstructionSelector::selectCall(llvm::CallInst &CI, BasicBlockCode &bbCode) {
    if (CI.isInlineAsm()) {
        auto *IA = llvm::cast<llvm::InlineAsm>(CI.getCalledOperand());
        std::string asmStr = IA->getAsmString();
        std::transform(asmStr.begin(), asmStr.end(), asmStr.begin(), ::tolower);

        if (asmStr.find("s2r") != std::string::npos) {
            MachineInstruction mi;
            mi.op = Opcode::S2R;
            mi.rd = getOrCreateVReg(&CI);
            if (asmStr.find("tid.x") != std::string::npos) mi.imm = static_cast<int32_t>(SysReg::TID_X);
            else if (asmStr.find("tid.y") != std::string::npos) mi.imm = static_cast<int32_t>(SysReg::TID_Y);
            else if (asmStr.find("bid.x") != std::string::npos) mi.imm = static_cast<int32_t>(SysReg::BID_X);
            else if (asmStr.find("bid.y") != std::string::npos) mi.imm = static_cast<int32_t>(SysReg::BID_Y);
            mi.comment = "s2r inline asm";
            bbCode.instructions.push_back(mi);
            return;
        } else if (asmStr.find("sync") != std::string::npos) {
            MachineInstruction mi;
            mi.op = Opcode::SYNC;
            mi.comment = "sync inline asm";
            bbCode.instructions.push_back(mi);
            return;
        } else if (asmStr.find("exit") != std::string::npos) {
            MachineInstruction mi;
            mi.op = Opcode::EXIT;
            mi.comment = "exit inline asm";
            bbCode.instructions.push_back(mi);
            return;
        } else if (asmStr.find("ldr") != std::string::npos) {
            MachineInstruction mi;
            mi.op = Opcode::LDR;
            mi.rd = getOrCreateVReg(&CI);
            mi.rs1 = getOrCreateVReg(CI.getArgOperand(0));
            mi.comment = "ldr inline asm";
            bbCode.instructions.push_back(mi);
            return;
        } else if (asmStr.find("str") != std::string::npos) {
            MachineInstruction mi;
            mi.op = Opcode::STR;
            mi.rs2 = getOrCreateVReg(CI.getArgOperand(0)); // data
            mi.rs1 = getOrCreateVReg(CI.getArgOperand(1)); // addr
            mi.comment = "str inline asm";
            bbCode.instructions.push_back(mi);
            return;
        }
    }

    // Check for standard runtime function names
    if (llvm::Function *callee = CI.getCalledFunction()) {
        llvm::StringRef name = callee->getName();
        if (name == "get_thread_id_x" || name.contains("tid_x")) {
            MachineInstruction mi;
            mi.op = Opcode::S2R;
            mi.rd = getOrCreateVReg(&CI);
            mi.imm = static_cast<int32_t>(SysReg::TID_X);
            mi.comment = "s2r TID.X";
            bbCode.instructions.push_back(mi);
        } else if (name == "get_thread_id_y" || name.contains("tid_y")) {
            MachineInstruction mi;
            mi.op = Opcode::S2R;
            mi.rd = getOrCreateVReg(&CI);
            mi.imm = static_cast<int32_t>(SysReg::TID_Y);
            mi.comment = "s2r TID.Y";
            bbCode.instructions.push_back(mi);
        } else if (name == "get_block_id_x" || name.contains("bid_x")) {
            MachineInstruction mi;
            mi.op = Opcode::S2R;
            mi.rd = getOrCreateVReg(&CI);
            mi.imm = static_cast<int32_t>(SysReg::BID_X);
            mi.comment = "s2r BID.X";
            bbCode.instructions.push_back(mi);
        } else if (name == "get_block_id_y" || name.contains("bid_y")) {
            MachineInstruction mi;
            mi.op = Opcode::S2R;
            mi.rd = getOrCreateVReg(&CI);
            mi.imm = static_cast<int32_t>(SysReg::BID_Y);
            mi.comment = "s2r BID.Y";
            bbCode.instructions.push_back(mi);
        } else if (name == "syncthreads" || name.contains("sync")) {
            MachineInstruction mi;
            mi.op = Opcode::SYNC;
            mi.comment = "sync";
            bbCode.instructions.push_back(mi);
        } else if (name == "gpu_exit" || name.contains("exit")) {
            MachineInstruction mi;
            mi.op = Opcode::EXIT;
            mi.comment = "exit";
            bbCode.instructions.push_back(mi);
        }
    }
}

void InstructionSelector::selectReturn(llvm::ReturnInst &RI, BasicBlockCode &bbCode) {
    (void)RI;
    MachineInstruction mi;
    mi.op = Opcode::EXIT;
    mi.comment = "return -> EXIT";
    bbCode.instructions.push_back(mi);
}

} // namespace fpgagpu
