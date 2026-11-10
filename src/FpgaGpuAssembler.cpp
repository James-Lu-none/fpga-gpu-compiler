#include "FpgaGpuAssembler.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>

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

    // Pass 2: Resolve branch and reconvergence labels relative to this instruction.
    for (size_t pc = 0; pc < instructions.size(); pc++) {
        auto &mi = instructions[pc];
        // Resolve BR and SSY label targets into PC-relative offsets:
        // mi.imm = target_pc - current_pc.
        // so program can jump: target_pc = current_pc + immediate
        if ((mi.op == Opcode::BR || mi.op == Opcode::SSY) && !mi.labelTarget.empty()) {
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
        } else if (mnemonic == "AND") {
            mi.op = Opcode::AND;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "OR") {
            mi.op = Opcode::OR;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "XOR") {
            mi.op = Opcode::XOR;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic.rfind("SETCC", 0) == 0) {
            mi.op = Opcode::SETCC;
            auto dotPos = mnemonic.find('.');
            if (dotPos != std::string::npos) {
                mi.cond = parseCondition(mnemonic.substr(dotPos + 1));
            } else if (tokens.size() > 4) {
                mi.cond = parseCondition(tokens[4]);
            } else {
                mi.cond = BranchCond::LT;
            }
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "SLT") {
            mi.op = Opcode::SETCC;
            mi.cond = BranchCond::LT;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "SEQ") {
            mi.op = Opcode::SETCC;
            mi.cond = BranchCond::EQ;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "SNE") {
            mi.op = Opcode::SETCC;
            mi.cond = BranchCond::NE;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "SGT") {
            mi.op = Opcode::SETCC;
            mi.cond = BranchCond::GT;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "SGE") {
            mi.op = Opcode::SETCC;
            mi.cond = BranchCond::GE;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
        } else if (mnemonic == "SLE") {
            mi.op = Opcode::SETCC;
            mi.cond = BranchCond::LE;
            mi.rd = parseRegister(tokens[1]);
            mi.rs1 = parseRegister(tokens[2]);
            mi.rs2 = parseRegister(tokens[3]);
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
            if (tokens.size() == 2) {
                // BR <target> (unconditional branch, rd = 0)
                mi.rd = 0;
                try {
                    mi.imm = std::stoi(tokens[1], nullptr, 0);
                } catch (...) {
                    mi.labelTarget = tokens[1];
                }
            } else if (tokens.size() >= 3) {
                // Conditional branch: BR R<rd>, <target>
                // Or legacy syntax: BR <cond>, <target>
                if (tokens[1].size() > 1 && (tokens[1][0] == 'R' || tokens[1][0] == 'r') && std::isdigit(tokens[1][1])) {
                    mi.rd = parseRegister(tokens[1]);
                } else {
                    // Legacy condition (e.g. ALWAYS, NZP): treat as unconditional rd = 0
                    mi.rd = 0;
                }
                try {
                    mi.imm = std::stoi(tokens[2], nullptr, 0);
                } catch (...) {
                    mi.labelTarget = tokens[2];
                }
            } else {
                throw std::runtime_error("BR requires at least one target argument");
            }
        } else if (mnemonic == "SYNC") {
            mi.op = Opcode::SYNC;
        } else if (mnemonic == "SSY") {
            mi.op = Opcode::SSY;
            if (tokens.size() != 2) {
                throw std::runtime_error("SSY requires one offset or label target");
            }
            // Match BR's PC-relative semantics, including signed/hex offsets.
            size_t consumed = 0;
            try {
                mi.imm = std::stoi(tokens[1], &consumed, 0);
            } catch (const std::invalid_argument &) {
                mi.labelTarget = tokens[1];
            }
            if (mi.labelTarget.empty() && consumed != tokens[1].size()) {
                throw std::runtime_error("Invalid SSY offset: " + tokens[1]);
            }
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
        case Opcode::AND:  oss << "AND R" << (int)rd << ", R" << (int)rs1 << ", R" << (int)rs2; break;
        case Opcode::OR:   oss << "OR R" << (int)rd << ", R" << (int)rs1 << ", R" << (int)rs2; break;
        case Opcode::XOR:  oss << "XOR R" << (int)rd << ", R" << (int)rs1 << ", R" << (int)rs2; break;
        case Opcode::SETCC: {
            uint8_t c = (word >> 6) & 0x7;
            std::string cStr;
            if (c & BranchCond::N) cStr += "N";
            if (c & BranchCond::Z) cStr += "Z";
            if (c & BranchCond::P) cStr += "P";
            if (cStr.empty()) cStr = "NEVER";
            oss << "SETCC." << cStr << " R" << (int)rd << ", R" << (int)rs1 << ", R" << (int)rs2;
            break;
        }
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
            if (rd == 0) {
                oss << "BR " << imm19;
            } else {
                oss << "BR R" << (int)rd << ", " << imm19;
            }
            break;
        }
        case Opcode::SYNC: oss << "SYNC"; break;
        case Opcode::SSY:  oss << "SSY " << imm19; break;
        case Opcode::EXIT: oss << "EXIT"; break;
        default: oss << ".word 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << word; break;
    }
    return oss.str();
}

} // namespace fpgagpu
