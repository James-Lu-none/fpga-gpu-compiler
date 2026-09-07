#include "FpgaGpuAssembler.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace fpgagpu {

static inline std::string trim(const std::string &s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static inline std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : s) {
        if (c == ',' || std::isspace(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

static inline uint8_t parseRegister(const std::string &token) {
    if (token.empty() || (token[0] != 'R' && token[0] != 'r')) {
        throw std::runtime_error("Invalid register: " + token);
    }
    int num = std::stoi(token.substr(1));
    if (num < 0 || num >= static_cast<int>(NUM_PHYSICAL_REGS)) {
        throw std::runtime_error("Register out of range: " + token);
    }
    return static_cast<uint8_t>(num);
}

static inline uint8_t parseCondition(const std::string &condStr) {
    std::string upper = condStr;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    uint8_t cond = 0;
    if (upper.find('N') != std::string::npos) cond |= BranchCond::N;
    if (upper.find('Z') != std::string::npos) cond |= BranchCond::Z;
    if (upper.find('P') != std::string::npos) cond |= BranchCond::P;
    return cond;
}

bool Assembler::assemble(std::istream &is, std::vector<uint32_t> &machineCode) {
    machineCode.clear();
    std::vector<MachineInstruction> instructions;
    std::unordered_map<std::string, size_t> labels;
    std::string line;
    size_t lineNum = 0;

    // Pass 1: Parse instructions & collect labels
    while (std::getline(is, line)) {
        lineNum++;
        if (!parseLine(line, lineNum, instructions, labels)) {
            return false;
        }
    }

    // Pass 2: Resolve branch target labels to relative offsets
    for (size_t pc = 0; pc < instructions.size(); pc++) {
        auto &mi = instructions[pc];
        if (mi.op == Opcode::BR && !mi.labelTarget.empty()) {
            if (auto it = labels.find(mi.labelTarget); it != labels.end()) {
                mi.imm = static_cast<int32_t>(it->second) - static_cast<int32_t>(pc);
            } else {
                std::cerr << "Error: Undefined label target: " << mi.labelTarget << "\n";
                return false;
            }
        }
        machineCode.push_back(mi.encode());
    }

    return true;
}

bool Assembler::parseLine(const std::string &rawLine, size_t lineNum,
                          std::vector<MachineInstruction> &instructions,
                          std::unordered_map<std::string, size_t> &labels) {
    // Strip comments
    std::string line = rawLine;
    auto commentPos = line.find(';');
    if (commentPos != std::string::npos) {
        line = line.substr(0, commentPos);
    }
    line = trim(line);
    if (line.empty()) return true;

    // Check for label definition: "label:"
    auto colonPos = line.find(':');
    if (colonPos != std::string::npos) {
        std::string labelName = trim(line.substr(0, colonPos));
        labels[labelName] = instructions.size();
        line = trim(line.substr(colonPos + 1));
        if (line.empty()) return true;
    }

    auto tokens = split(line);
    if (tokens.empty()) return true;

    std::string mnemonic = tokens[0];
    std::transform(mnemonic.begin(), mnemonic.end(), mnemonic.begin(), ::toupper);

    try {
        MachineInstruction mi;
        if (mnemonic == "ADD") {
            mi.op = Opcode::ADD;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "SUB") {
            mi.op = Opcode::SUB;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "MUL") {
            mi.op = Opcode::MUL;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "CMP") {
            mi.op = Opcode::CMP;
            mi.rs1 = parseRegister(tokens[1]);
            mi.rs2 = parseRegister(tokens[2]);
        } else if (mnemonic == "ADDI") {
            mi.op = Opcode::ADDI;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.imm = std::stoi(tokens[3], nullptr, 0) & 0x3FFF;
        } else if (mnemonic == "LDR") {
            mi.op = Opcode::LDR;
            mi.rd = parseRegister(tokens[1]);
            std::string ptrStr = tokens[2];
            ptrStr.erase(std::remove(ptrStr.begin(), ptrStr.end(), '['), ptrStr.end());
            ptrStr.erase(std::remove(ptrStr.begin(), ptrStr.end(), ']'), ptrStr.end());
            mi.rs1 = parseRegister(ptrStr);
        } else if (mnemonic == "STR") {
            mi.op = Opcode::STR;
            mi.rs2 = parseRegister(tokens[1]); // Data
            std::string ptrStr = tokens[2];
            ptrStr.erase(std::remove(ptrStr.begin(), ptrStr.end(), '['), ptrStr.end());
            ptrStr.erase(std::remove(ptrStr.begin(), ptrStr.end(), ']'), ptrStr.end());
            mi.rs1 = parseRegister(ptrStr); // Addr
        } else if (mnemonic == "S2R") {
            mi.op = Opcode::S2R;
            mi.rd = parseRegister(tokens[1]);
            std::string srStr = tokens[2];
            std::transform(srStr.begin(), srStr.end(), srStr.begin(), ::toupper);
            if (srStr == "SR_TID.X") mi.imm = static_cast<int32_t>(SysReg::TID_X);
            else if (srStr == "SR_TID.Y") mi.imm = static_cast<int32_t>(SysReg::TID_Y);
            else if (srStr == "SR_BID.X") mi.imm = static_cast<int32_t>(SysReg::BID_X);
            else if (srStr == "SR_BID.Y") mi.imm = static_cast<int32_t>(SysReg::BID_Y);
            else mi.imm = std::stoi(tokens[2], nullptr, 0) & 0x3FFF;
        } else if (mnemonic == "BR") {
            mi.op = Opcode::BR;
            mi.cond = parseCondition(tokens[1]);
            // Can be label target or raw integer offset
            try {
                mi.imm = std::stoi(tokens[2], nullptr, 0);
            } catch (...) {
                mi.labelTarget = tokens[2];
            }
        } else if (mnemonic == "SYNC") {
            mi.op = Opcode::SYNC;
        } else if (mnemonic == "EXIT") {
            mi.op = Opcode::EXIT;
        } else {
            std::cerr << "Line " << lineNum << ": Unknown mnemonic '" << mnemonic << "'\n";
            return false;
        }

        instructions.push_back(mi);
    } catch (const std::exception &e) {
        std::cerr << "Error assembling line " << lineNum << ": " << e.what() << "\n";
        return false;
    }

    return true;
}

std::string Assembler::disassemble(uint32_t word) {
    uint8_t op = (word >> 24) & 0xFF;
    uint8_t rd = (word >> 19) & 0x1F;
    uint8_t rs1 = (word >> 14) & 0x1F;
    uint8_t rs2 = (word >> 9) & 0x1F;
    int32_t imm14 = word & 0x3FFF;
    if (imm14 & 0x2000) imm14 |= ~0x3FFF; // Sign extend 14-bit
    uint8_t cond = (word >> 19) & 0x7;
    int32_t imm19 = word & 0x7FFFF;
    if (imm19 & 0x40000) imm19 |= ~0x7FFFF; // Sign extend 19-bit

    std::ostringstream oss;
    switch (static_cast<Opcode>(op)) {
        case Opcode::ADD:  oss << "ADD R" << (int)rd << ", R" << (int)rs1 << ", R" << (int)rs2; break;
        case Opcode::SUB:  oss << "SUB R" << (int)rd << ", R" << (int)rs1 << ", R" << (int)rs2; break;
        case Opcode::MUL:  oss << "MUL R" << (int)rd << ", R" << (int)rs1 << ", R" << (int)rs2; break;
        case Opcode::CMP:  oss << "CMP R" << (int)rs1 << ", R" << (int)rs2; break;
        case Opcode::ADDI: oss << "ADDI R" << (int)rd << ", R" << (int)rs1 << ", " << imm14; break;
        case Opcode::LDR:  oss << "LDR R" << (int)rd << ", [R" << (int)rs1 << "]"; break;
        case Opcode::STR:  oss << "STR R" << (int)rs2 << ", [R" << (int)rs1 << "]"; break;
        case Opcode::S2R: {
            oss << "S2R R" << (int)rd << ", ";
            switch (static_cast<SysReg>(imm14)) {
                case SysReg::TID_X: oss << "SR_TID.X"; break;
                case SysReg::TID_Y: oss << "SR_TID.Y"; break;
                case SysReg::BID_X: oss << "SR_BID.X"; break;
                case SysReg::BID_Y: oss << "SR_BID.Y"; break;
                default: oss << imm14; break;
            }
            break;
        }
        case Opcode::BR: {
            oss << "BR ";
            if (cond & BranchCond::N) oss << "N";
            if (cond & BranchCond::Z) oss << "Z";
            if (cond & BranchCond::P) oss << "P";
            if (cond == 0) oss << "NEVER";
            oss << ", " << imm19;
            break;
        }
        case Opcode::SYNC: oss << "SYNC"; break;
        case Opcode::EXIT: oss << "EXIT"; break;
        default: oss << ".word 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << word; break;
    }
    return oss.str();
}

} // namespace fpgagpu
