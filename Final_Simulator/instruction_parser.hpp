#ifndef INSTRUCTION_PARSER_HPP
#define INSTRUCTION_PARSER_HPP

#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <regex>
#include "pipeline.hpp"
#include <string.h>


class InstructionParser {
public:
static Instruction parseInstruction(const std::string &raw, int coreId) {
int id;
Instruction inst;
    //memset(&inst, 0, sizeof(inst));
inst.raw = raw;
inst.coreId = coreId;


std::istringstream iss(raw);
std::string opcode;
iss >> opcode;
    std::cout << "[Parser] raw opcode token = '" << opcode << "'\n";

inst.opcode = opcode;

if (opcode == "add" || opcode == "sub" || opcode == "slt" || opcode == "mul") {
    inst.isArithmetic = true;
    parseRTypeInstruction(inst, iss);
}
else if (opcode == "addi") {
    inst.isArithmetic = true;
    parseITypeInstruction(inst, iss);
}
else if (opcode == "lw" || opcode == "lw_spm") {
    if (opcode=="lw") {
        std::cout << "[Parser] → dispatching to LW parser\n";
    }
    inst.isMemory = true;
    parseLoadInstruction(inst, iss);
    if (opcode == "lw_spm") {
        inst.isSPM = true;
    }

}
else if (opcode == "sw" || opcode == "sw_spm") {
    inst.isMemory = true;
    parseStoreInstruction(inst, iss);
    if (opcode == "sw_spm") {
        inst.isSPM = true;
    }
}
else if (opcode == "bne" || opcode == "blt" || opcode == "beq") {
    inst.isBranch = true;
    parseBranchInstruction(inst, iss);
}
else if (opcode == "jal") {
    inst.isJump = true;
    parseJumpInstruction(inst, iss);
}
else if (opcode == "la") {
    std::cout << "[Parser] → dispatching to LA parser\n";
    parseLoadAddressInstruction(inst, iss);
}
else if (opcode == "sync") {
    inst.opcode = "sync";
    inst.isSync = true;
    inst.shouldExecute = true;  // ensure sync is not skipped

    std::cout << "[Parser] Parsed SYNC instruction\n";
}
else if (opcode == "invld1") {
    Instruction inst;
    inst.opcode = "invld1";
    inst.isInvalidateL1D = true;  // add this boolean to Instruction struct
    return inst;
}

return inst;
}
private:
static void parseRTypeInstruction(Instruction &inst, std::istringstream &iss) {
std::string rest;
std::getline(iss, rest);
auto operands = parseOperands(rest);


if (operands.size() >= 3) {
    inst.rd = parseRegister(operands[0]);
    inst.rs1 = parseRegister(operands[1]);
    inst.rs2 = parseRegister(operands[2]);
}
}

static void parseITypeInstruction(Instruction &inst, std::istringstream &iss) {
std::string rest;
std::getline(iss, rest);
auto operands = parseOperands(rest);


if (operands.size() >= 3) {
    inst.rd = parseRegister(operands[0]);
    inst.rs1 = parseRegister(operands[1]);
    inst.immediate = std::stoi(operands[2]);
}
}

static void parseLoadInstruction(Instruction &inst, std::istringstream &iss) {
    inst.rd        = inst.rs1 = inst.rs2 = -1;
    inst.immediate = 0;
std::string rest;
std::getline(iss, rest);
auto operands = parseOperands(rest);


if (operands.size() >= 2) {
    inst.rd = parseRegister(operands[0]);

    std::string offsetBase = operands[1];
    size_t openParen = offsetBase.find('(');
    size_t closeParen = offsetBase.find(')');

    if (openParen != std::string::npos && closeParen != std::string::npos) {
        std::string offsetStr = offsetBase.substr(0, openParen);
        std::string baseReg = offsetBase.substr(openParen + 1, closeParen - openParen - 1);

        inst.immediate = offsetStr.empty() ? 0 : std::stoi(offsetStr);
        inst.rs1 = parseRegister(baseReg);
    }
}
}

static void parseStoreInstruction(Instruction &inst, std::istringstream &iss) {
std::string rest;
std::getline(iss, rest);
auto operands = parseOperands(rest);


if (operands.size() >= 2) {
    inst.rs2 = parseRegister(operands[0]); 

    std::string offsetBase = operands[1];
    size_t openParen = offsetBase.find('(');
    size_t closeParen = offsetBase.find(')');

    if (openParen != std::string::npos && closeParen != std::string::npos) {
        std::string offsetStr = offsetBase.substr(0, openParen);
        std::string baseReg = offsetBase.substr(openParen + 1, closeParen - openParen - 1);

        inst.immediate = offsetStr.empty() ? 0 : std::stoi(offsetStr);
        inst.rs1 = parseRegister(baseReg);
    }
}
}

static void parseBranchInstruction(Instruction &inst, std::istringstream &iss) {
std::string rest;
std::getline(iss, rest);
auto operands = parseOperands(rest);
if (operands.size() >= 3) {
inst.rs1 = parseRegister(operands[0]);
if (inst.opcode == "beq") {
if (inst.rs1 == 31) {
inst.useCID = true;
inst.rs2 = std::stoi(operands[1]);
}
else {
inst.rs2 = parseRegister(operands[1]);
}
}
else {
inst.rs2 = parseRegister(operands[1]);
}
inst.label = operands[2];
inst.targetPC = -1;
}
}

static void parseJumpInstruction(Instruction &inst, std::istringstream &iss) {
std::string rest;
std::getline(iss, rest);
rest = trim(rest);
auto operands = parseOperands(rest);
if (operands.size() == 1) {


    inst.rd = -1; 
    std::string lab = operands[0];
    if (!lab.empty() && lab[0] == '.')
        lab = lab.substr(1);
    inst.label = lab;
    inst.targetPC = -1; 
    std::cout << "[InstructionParser] Parsed jal (no link) for \"" << inst.raw
            << "\" as label=\"" << inst.label << "\"\n";
}
else if (operands.size() >= 2) {
    
    inst.rd = parseRegister(operands[0]);
    std::string lab = operands[1];
    if (!lab.empty() && lab[0] == '.')
        lab = lab.substr(1);
    inst.label = lab;
    inst.targetPC = -1; 
    std::cout << "[InstructionParser] Parsed jal for \"" << inst.raw
            << "\" as rd=" << inst.rd << ", label=\"" << inst.label << "\"\n";
}
else {
    std::cerr << "[InstructionParser] Error: could not parse operands from \""
            << rest << "\" in instruction \"" << inst.raw << "\"\n";
}
}

    static void parseLoadAddressInstruction(Instruction &inst, std::istringstream &iss) {
    inst.rd        = -1;
    inst.rs1       = inst.rs2 = -1;
    inst.immediate = 0;
    inst.label.clear();
    std::string rest;
    std::getline(iss, rest);
    auto operands = parseOperands(rest);

    // Strip any trailing commas:
    auto stripComma = [&](std::string &s) {
        if (!s.empty() && s.back()==',') s.pop_back();
    };
    for (auto &op : operands) stripComma(op);

    // Debug: show exactly what you got
    std::cout << "[Decode] la operands:";
    for (auto &op : operands) std::cout << " \"" << op << "\"";
    std::cout << "\n";

    if (operands.size() >= 2) {
        // operands[0] → destination register (e.g. "x2")
        inst.rd = parseRegister(operands[0]);
        // operands[1] → label (e.g. "len")
        std::string lab = operands[1];
        if (!lab.empty() && lab[0]=='.') lab = lab.substr(1);
        inst.label = lab;
        std::cout << "[InstructionParser] Parsed la rd = x"
                  << inst.rd << ", label = \"" << inst.label
                  << "\" for \"" << inst.raw << "\"\n";
    }
}


static int parseRegister(const std::string &reg) {
if (reg.size() >= 2 && reg[0] == 'x') {
return std::stoi(reg.substr(1));
}
return -1;
}

static std::vector<std::string> parseOperands(const std::string &rest) {
std::string operandsStr = rest;
size_t commentPos = operandsStr.find('#');
if (commentPos != std::string::npos) {
operandsStr = operandsStr.substr(0, commentPos);
}


std::vector<std::string> operands;
std::regex reg(",\\s*");
std::sregex_token_iterator iter(operandsStr.begin(), operandsStr.end(), reg, -1);
std::sregex_token_iterator end;

while (iter != end) {
    std::string operand = iter->str();
    operand = trim(operand);
    if (!operand.empty()) {
        operands.push_back(operand);
    }
    ++iter;
}

return operands;
}

static std::string trim(const std::string &str) {
size_t first = str.find_first_not_of(" \t\n\r");
if (first == std::string::npos)
return "";
size_t last = str.find_last_not_of(" \t\n\r");
return str.substr(first, (last - first + 1));
}
};

#endif // INSTRUCTION_PARSER_HPP