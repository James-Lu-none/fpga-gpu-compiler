//===-- FpgaGpu.td - Target Description for FPGA-GPU -----------*- tablegen -*-===//
//
// Defines the Target Architecture for the FPGA-GPU Softcore.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

include "FpgaGpuRegisterInfo.td"
include "FpgaGpuInstrInfo.td"

def FpgaGpuInstrInfo : InstrInfo;

def FpgaGpu : Target {
    let InstructionSet = FpgaGpuInstrInfo;
}
