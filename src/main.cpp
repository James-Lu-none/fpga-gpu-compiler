#include "FpgaGpuBackend.h"
#include "FpgaGpuAssembler.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

void printHelp(const char *progName) {
    std::cout << "Usage: " << progName << " [options] <input-file>\n\n"
              << "Options:\n"
              << "  -o <file>          Write output to <file>\n"
              << "  -S, --emit-asm     Emit human-readable assembly (.s)\n"
              << "  -x, --emit-hex     Emit hexadecimal machine code (.hex, default)\n"
              << "  -b, --emit-bin     Emit raw binary machine code (.bin)\n"
              << "  -O0                Disable LLVM IR optimizations\n"
              << "  -O2                Enable LLVM IR optimizations (default)\n"
              << "  -a, --assemble     Assemble an assembly file (.s) into machine code\n"
              << "  -d, --disasm       Disassemble a hex file into assembly\n"
              << "  -h, --help         Display this help message\n";
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printHelp(argv[0]);
        return 1;
    }

    std::string inputFile;
    std::string outputFile;
    fpgagpu::OutputFormat format = fpgagpu::OutputFormat::HEX;
    bool optimize = true;
    bool assembleMode = false;
    bool disasmMode = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return 0;
        } else if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "-S" || arg == "--emit-asm") {
            format = fpgagpu::OutputFormat::ASSEMBLY;
        } else if (arg == "-x" || arg == "--emit-hex") {
            format = fpgagpu::OutputFormat::HEX;
        } else if (arg == "-b" || arg == "--emit-bin") {
            format = fpgagpu::OutputFormat::BINARY;
        } else if (arg == "-O0") {
            optimize = false;
        } else if (arg == "-O2" || arg == "-O1" || arg == "-O3") {
            optimize = true;
        } else if (arg == "-a" || arg == "--assemble") {
            assembleMode = true;
        } else if (arg == "-d" || arg == "--disasm") {
            disasmMode = true;
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        } else {
            inputFile = arg;
        }
    }

    if (inputFile.empty()) {
        std::cerr << "Error: No input file specified.\n";
        return 1;
    }

    // Default output file name if not provided
    if (outputFile.empty()) {
        auto dotPos = inputFile.find_last_of('.');
        std::string base = (dotPos != std::string::npos) ? inputFile.substr(0, dotPos) : inputFile;
        if (format == fpgagpu::OutputFormat::ASSEMBLY) {
            outputFile = base + ".s";
        } else if (format == fpgagpu::OutputFormat::BINARY) {
            outputFile = base + ".bin";
        } else {
            outputFile = base + ".hex";
        }
    }

    // Mode 1: Assemble .s to .hex / .bin
    if (assembleMode) {
        std::ifstream inFile(inputFile);
        if (!inFile.is_open()) {
            std::cerr << "Error: Unable to open assembly file: " << inputFile << "\n";
            return 1;
        }
        fpgagpu::Assembler as;
        std::vector<uint32_t> code;
        if (!as.assemble(inFile, code)) {
            std::cerr << "Assembly failed.\n";
            return 1;
        }

        std::ofstream outFile;
        if (format == fpgagpu::OutputFormat::BINARY) {
            outFile.open(outputFile, std::ios::out | std::ios::binary);
            for (uint32_t word : code) {
                char bytes[4] = {
                    static_cast<char>(word & 0xFF),
                    static_cast<char>((word >> 8) & 0xFF),
                    static_cast<char>((word >> 16) & 0xFF),
                    static_cast<char>((word >> 24) & 0xFF)
                };
                outFile.write(bytes, 4);
            }
        } else {
            outFile.open(outputFile, std::ios::out);
            for (uint32_t word : code) {
                outFile << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << word << "\n";
            }
        }
        std::cout << "Successfully assembled " << code.size() << " instructions to " << outputFile << "\n";
        return 0;
    }

    // Mode 2: Disassemble .hex to .s
    if (disasmMode) {
        std::ifstream inFile(inputFile);
        if (!inFile.is_open()) {
            std::cerr << "Error: Unable to open hex file: " << inputFile << "\n";
            return 1;
        }
        std::ofstream outFile(outputFile);
        if (!outFile.is_open()) {
            std::cerr << "Error: Unable to open output file: " << outputFile << "\n";
            return 1;
        }

        std::string line;
        while (std::getline(inFile, line)) {
            if (line.empty()) continue;
            try {
                uint32_t word = std::stoul(line, nullptr, 16);
                outFile << fpgagpu::Assembler::disassemble(word) << "\n";
            } catch (...) {
                outFile << "; Invalid hex line: " << line << "\n";
            }
        }
        std::cout << "Successfully disassembled " << inputFile << " to " << outputFile << "\n";
        return 0;
    }

    // Mode 3: Compile LLVM IR (.ll or .bc) -> Target
    fpgagpu::CompilerBackend backend;
    if (!backend.compileFile(inputFile, outputFile, format, optimize)) {
        std::cerr << "Compilation failed.\n";
        return 1;
    }

    std::cout << "Successfully compiled " << inputFile << " -> " << outputFile << "\n";
    return 0;
}
