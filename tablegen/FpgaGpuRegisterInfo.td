//===-- FpgaGpuRegisterInfo.td - FPGA-GPU Register Files --------*- tablegen -*-===//
//
// Defines the 32 64-bit Vector Registers (R0 - R31).
//
//===----------------------------------------------------------------------===//

class FpgaGpuReg<bits<5> Enc, string n> : Register<n> {
    let HWEncoding{4-0} = Enc;
    let Namespace = "FpgaGpu";
}

// 32 Architecture Vector Registers
foreach I = 0-31 in {
    def R#I : FpgaGpuReg<I, "R"#I>;
}

// Vector Register Class (64-bit)
def VRF : RegisterClass<"FpgaGpu", [v2i32, i64, i32], 64, (add
    R0, R1, R2, R3, R4, R5, R6, R7,
    R8, R9, R10, R11, R12, R13, R14, R15,
    R16, R17, R18, R19, R20, R21, R22, R23,
    R24, R25, R26, R27, R28, R29, R30, R31
)>;
