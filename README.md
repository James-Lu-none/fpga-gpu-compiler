# FPGA-GPU LLVM Compiler Backend & Toolchain

This repository implements the LLVM Compiler Infrastructure & Backend for the FPGA-GPU accelerator. It provides a complete toolchain to compile C/C++ and CUDA-like device kernels into 32-bit machine code binary (`.bin`) and hex (`.hex`) formats directly executable on the on-chip Streaming Multiprocessors (SMs).

---

## Architecture & Features

```
fpga-gpu-compiler/
├── CMakeLists.txt              # CMake build configuration (linking LLVM 18)
├── README.md                   # This documentation
├── include/                    # C++ Header declarations
│   ├── FpgaGpu.h               # Hardware ISA opcodes, registers, and 32-bit binary formats
│   ├── FpgaGpuBackend.h        # Module-level compilation coordinator
│   ├── FpgaGpuISel.h           # Instruction Selection (LLVM IR -> FpgaGpu IR)
│   ├── FpgaGpuRegAlloc.h       # Bank-aware allocator (R1..R29, R30/R31 repair)
│   ├── FpgaGpuEmitter.h        # Assembly (.s), Hex (.hex), and Binary (.bin) code emitter
│   └── FpgaGpuAssembler.h      # Standalone Assembler and Disassembler
├── src/                        # C++ Implementation
│   ├── main.cpp                # CLI entry point (fpgagpu-llc)
│   ├── FpgaGpuBackend.cpp      # LLVM IR pass manager and pipeline driver
│   ├── FpgaGpuISel.cpp         # ISel lowering rules
│   ├── FpgaGpuRegAlloc.cpp     # Physical register assignment
│   ├── FpgaGpuEmitter.cpp      # File output generation
│   └── FpgaGpuAssembler.cpp    # Parser and disassembler logic
├── scripts/
│   └── fpgagpu-clang           # High-level compiler driver (C/C++ -> Machine Code)
├── runtime/
│   └── fpgagpu_runtime.h       # Device runtime header (TID, BID, syncthreads, exit)
├── tablegen/                   # Formal LLVM Target definitions (.td)
│   ├── FpgaGpu.td              # Target description
│   ├── FpgaGpuRegisterInfo.td  # 32 vector registers (R0..R31)
│   └── FpgaGpuInstrInfo.td     # Instruction patterns and formats
└── examples/                   # Sample kernels
    ├── 01_vec_add/             # Vector Addition kernel
    ├── 02_s2r/                 # System Register (TID/BID) query kernel
    └── 03_branch/              # Conditional branching & SIMT stack reconvergence
```

---

## Hardware ISA Mapping

The compiler targets the custom 32-bit RISC ISA implemented in `rtl/sm/fetch_decode.sv` and `rtl/sm/alu.sv`:

| Opcode | Mnemonic | Type | Operands | Action |
| :--- | :--- | :--- | :--- | :--- |
| `0x01` | `ADD` | R | `rd, rs1, rs2` | `VRF[rd] = VRF[rs1] + VRF[rs2]` |
| `0x02` | `SUB` | R | `rd, rs1, rs2` | `VRF[rd] = VRF[rs1] - VRF[rs2]` |
| `0x03` | `MUL` | R | `rd, rs1, rs2` | `VRF[rd] = VRF[rs1] * VRF[rs2]` (low 32-bit) |
| `0x04` | `CMP` | R | `rs1, rs2` | Comparison |
| `0x05` | `AND` | R | `rd, rs1, rs2` | `VRF[rd] = VRF[rs1] & VRF[rs2]` |
| `0x06` | `OR` | R | `rd, rs1, rs2` | `VRF[rd] = VRF[rs1] \| VRF[rs2]` |
| `0x07` | `XOR` | R | `rd, rs1, rs2` | `VRF[rd] = VRF[rs1] ^ VRF[rs2]` |
| `0x08` | `SETCC`| R | `rd, rs1, rs2, cond` | `VRF[rd] = eval(cond, rs1, rs2) ? 1 : 0` |
| `0x81` | `ADDI` | I | `rd, rs1, imm14` | `VRF[rd] = VRF[rs1] + imm14` |
| `0xA0` | `LDR` | I | `rd, [rs1]` | Load 64-bit word from memory into `rd` |
| `0xA1` | `STR` | R | `rs2, [rs1]` | Store 64-bit word `rs2` to address `rs1` |
| `0xB0` | `S2R` | I | `rd, SR_NAME` | Read hardware system register (`TID.X`, `TID.Y`, `BID.X`, `BID.Y`) |
| `0xC0` | `BR` | J | `[rd,] target` | Branch if `rd == 0` (unconditional) or `VRF[rd] != 0` (conditional) |
| `0xE0` | `SYNC` | J | - | Pop SIMT divergence stack for reconvergence |
| `0xE1` | `SSY` | J | `target` | Push reconvergence PC target onto SIMT stack |
| `0xFF` | `EXIT` | J | - | Terminate warp execution |

---

## Building the Compiler

The odd/even VRF requires distinct nonzero register sources to be in opposite
banks for ADD/SUB/MUL/CMP/AND/OR/XOR/SETCC and STR. Equal sources broadcast;
R0 and single-source instructions do not impose a bank constraint. BR reads
its predicate from rd, while immediate bits are not a second register source.

The custom allocator in `src/FpgaGpuRegAlloc.cpp` prefers opposite banks and
inserts `ADDI scratch, source, 0` for remaining conflicts before branch/SSY
offset resolution. R30 (even) and R31 (odd) are reserved for these copies;
R1..R29 remain allocatable. Spilling is not supported, and excessive pressure
is reported explicitly. Final instructions are verified and programs exceeding
1,024 IRAM words are rejected. The assembler rejects conflicting hand-written
source pairs rather than silently modifying explicit physical registers.

Rebuild the compiler and recompile kernels for banked hardware. Old machine
code can contain unsupported bank conflicts; the RTL simulation detects these
and the hardware has no conflict replay path.

### Prerequisites
- Ubuntu 22.04 / 24.04 or compatible Linux
- `cmake` (>= 3.20)
- `clang-18`
- `llvm-18-dev`
- `build-essential`, `zlib1g-dev`, `libzstd-dev`

```bash
sudo apt-get update
sudo apt-get install -y cmake clang-18 llvm-18-dev zlib1g-dev libzstd-dev build-essential
```

### Build Steps

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The resulting compiler binary `fpgagpu-llc` will be generated in `build/`.

---

## Usage Guide

### 1. Compile C/C++ Kernel directly to Hex / Binary (`fpgagpu-clang`)

Use the driver script to compile C device code directly to `.hex` (for Verilog testbenches / simulation) or `.bin` (for PCIe driver loading):

```bash
# Compile C kernel to .hex
./scripts/fpgagpu-clang -O2 examples/01_vec_add/vec_add.c -o vec_add.hex

# Compile C kernel to assembly (.s)
./scripts/fpgagpu-clang -S -O2 examples/01_vec_add/vec_add.c -o vec_add.s

# Compile C kernel to raw binary (.bin)
./scripts/fpgagpu-clang -b -O2 examples/01_vec_add/vec_add.c -o vec_add.bin
```

### 2. Standalone Code Generator (`fpgagpu-llc`)

You can compile directly from an LLVM IR file (`.ll` or `.bc`):

```bash
# Compile LLVM IR to hex machine code
./build/fpgagpu-llc -x kernel.ll -o kernel.hex

# Compile LLVM IR to human-readable assembly
./build/fpgagpu-llc -S kernel.ll -o kernel.s
```

### 3. Assembler & Disassembler

```bash
# Assemble an assembly file (.s) to hex
./build/fpgagpu-llc -a kernel.s -o kernel.hex

# Disassemble a hex file back to assembly
./build/fpgagpu-llc -d kernel.hex -o kernel_disasm.s
```

---

## Control Flow & Branch Lowering (SIMT Divergence)

The compiler backend features an integrated SIMT divergence analysis pass (`runDivergencePass`):
1. **Immediate Post-Dominator (IPD) Analysis**: Identifies branch reconvergence targets via Post-Dominator Tree (`llvm::PostDominatorTree`).
2. **SSY & SYNC Insertion**: Automatically places `SSY <IPD>` before conditional branches, and `SYNC` instructions before the reconvergence point (`IPD`) on diverging paths.
3. **Register-Based Predicates & Boolean Evaluation (`SETCC`, `AND`, `OR`, `XOR`)**: The FPGA-GPU architecture supports vector bitwise logic (`AND`, `OR`, `XOR`) and condition evaluation into vector registers (`SETCC rd, rs1, rs2, cond`). All comparisons evaluate directly into registers (producing 0 or 1), and compound boolean predicates (`if (a && b && c ...)`) naturally compose in dataflow via `AND`/`OR` instructions. A conditional branch simply evaluates `condReg != 0`, triggering exactly one branch and at most one SIMT stack divergence event, eliminating multi-stage branch serialization.

---

## Writing Device Kernels

Include `"fpgagpu_runtime.h"` in your C source file to access device intrinsics:

```c
#include "fpgagpu_runtime.h"

void vector_add(const uint32_t *A, const uint32_t *B, uint32_t *C, uint32_t n) {
    uint32_t tid = get_thread_id_x();
    uint32_t bid = get_block_id_x();
    uint32_t idx = tid + (bid * 32);

    if (idx < n) {
        uint32_t a = gpu_load32(&A[idx]);
        uint32_t b = gpu_load32(&B[idx]);
        gpu_store32(&C[idx], a + b);
    }

    gpu_exit();
}
```

---

## End-to-End Hardware Integration

1. The compiled `kernel.hex` or `kernel.bin` is transferred by the host application through PCIe XDMA into the GPC I-RAM Broadcast Window at `0x1000_1000 ~ 0x1000_1FFF`.
2. The FPGA hardware broadcasts the instruction words to all on-chip Streaming Multiprocessors.
3. The on-chip PicoRV32 Command Processor launches the hardware thread block scheduler via `REG_DOORBELL = 1`.

Use `scripts/fpgagpu-clang --save-temps -x kernel.c -o kernel.hex` to also
write `kernel.ll`: the Clang-generated LLVM IR passed into the GPU backend,
before backend optimization and SIMT reconvergence insertion.
