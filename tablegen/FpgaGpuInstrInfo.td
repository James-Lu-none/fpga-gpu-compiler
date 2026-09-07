//===-- FpgaGpuInstrInfo.td - FPGA-GPU Instruction Formats ------*- tablegen -*-===//
//
// Defines instruction formats, opcodes, and patterns for FPGA-GPU.
//
//===----------------------------------------------------------------------===//

// Instruction Encodings
class FpgaGpuInst<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
    let Namespace = "FpgaGpu";
    let Size = 4;
    dag OutOperandList = outs;
    dag InOperandList = ins;
    let AsmString = asmstr;
    let Pattern = pattern;
}

// R-Type Instructions: [31:24]=Op, [23:19]=Rd, [18:14]=Rs1, [13:9]=Rs2
class InstR<bits<8> op, string opcodestr, list<dag> pattern>
    : FpgaGpuInst<(outs VRF:$rd), (ins VRF:$rs1, VRF:$rs2),
                  !strconcat(opcodestr, " $rd, $rs1, $rs2"), pattern> {
    bits<5> rd;
    bits<5> rs1;
    bits<5> rs2;

    let Inst{31-24} = op;
    let Inst{23-19} = rd;
    let Inst{18-14} = rs1;
    let Inst{13-9}  = rs2;
    let Inst{8-0}   = 0;
}

// I-Type Instructions: [31:24]=Op, [23:19]=Rd, [18:14]=Rs1, [13:0]=Imm14
class InstI<bits<8> op, string opcodestr, list<dag> pattern>
    : FpgaGpuInst<(outs VRF:$rd), (ins VRF:$rs1, i32imm:$imm),
                  !strconcat(opcodestr, " $rd, $rs1, $imm"), pattern> {
    bits<5> rd;
    bits<5> rs1;
    bits<14> imm;

    let Inst{31-24} = op;
    let Inst{23-19} = rd;
    let Inst{18-14} = rs1;
    let Inst{13-0}  = imm;
}

// Concrete Instructions
def ADD  : InstR<0x01, "ADD",  [(set VRF:$rd, (add VRF:$rs1, VRF:$rs2))]>;
def SUB  : InstR<0x02, "SUB",  [(set VRF:$rd, (sub VRF:$rs1, VRF:$rs2))]>;
def MUL  : InstR<0x03, "MUL",  [(set VRF:$rd, (mul VRF:$rs1, VRF:$rs2))]>;
def ADDI : InstI<0x81, "ADDI", [(set VRF:$rd, (add VRF:$rs1, simm14:$imm))]>;

// Load & Store
def LDR  : FpgaGpuInst<(outs VRF:$rd), (ins VRF:$rs1),
                      "LDR $rd, [$rs1]", [(set VRF:$rd, (load VRF:$rs1))]>;
def STR  : FpgaGpuInst<(outs), (ins VRF:$rs1, VRF:$rs2),
                      "STR $rs2, [$rs1]", [(store VRF:$rs2, VRF:$rs1)]>;

// Special Registers
def S2R  : FpgaGpuInst<(outs VRF:$rd), (ins i32imm:$imm),
                      "S2R $rd, $imm", []>;

// Branch & Control
def BR   : FpgaGpuInst<(outs), (ins i32imm:$cond, i32imm:$target),
                      "BR $cond, $target", []>;
def SYNC : FpgaGpuInst<(outs), (ins), "SYNC", []>;
def EXIT : FpgaGpuInst<(outs), (ins), "EXIT", []>;
