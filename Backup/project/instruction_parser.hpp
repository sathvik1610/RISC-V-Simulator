#ifndef INSTRUCTION_PARSER_HPP
#define INSTRUCTION_PARSER_HPP

#include <string>
#include <sstream>
#include <iostream>
#include <regex>

// Structure to represent a parsed instruction
struct Instruction {
    int id;  // Unique ID for this instruction
    std::string opcode;
    int rd;        // Destination register
    int rs1;       // Source register 1
    int rs2;       // Source register 2
    int immediate; // Immediate value
    std::string label; // Branch/jump target label
    int targetPC;  // PC value for branch/jump target
    
    bool isArithmetic;
    bool isMemory;
    bool isBranch;
    bool isJump;
    bool isHalt;
    bool isSync;
    bool isSPM;  // Flag for scratchpad memory operations
    
    bool hasResult;
    int resultValue;
    bool shouldExecute;
    
    int executeLatency;
    int cyclesInExecute;
    
    Instruction() : id(0), rd(-1), rs1(-1), rs2(-1), immediate(0), targetPC(-1),
                    isArithmetic(false), isMemory(false), isBranch(false), isJump(false),
                    isHalt(false), isSync(false), isSPM(false), hasResult(false), resultValue(0),
                    shouldExecute(true), executeLatency(1), cyclesInExecute(0) {}
};

class InstructionParser {
public:
    static Instruction parseInstruction(const std::string& rawInst, int coreId) {
        Instruction inst;
        
        // Remove leading/trailing whitespace
        std::string instruction = rawInst;
        instruction.erase(0, instruction.find_first_not_of(" \t"));
        instruction.erase(instruction.find_last_not_of(" \t") + 1);
        
        // Skip empty instructions
        if (instruction.empty()) {
            return inst;
        }
        
        // Handle HALT instruction
        if (instruction == "halt") {
            inst.opcode = "halt";
            inst.isHalt = true;
            return inst;
        }
        
        // Handle SYNC instruction
        if (instruction == "sync") {
            inst.opcode = "sync";
            inst.isSync = true;
            return inst;
        }
        
        // Extract opcode
        std::istringstream iss(instruction);
        iss >> inst.opcode;
        
        // Categorize instruction
        if (inst.opcode == "add" || inst.opcode == "addi" || inst.opcode == "sub" || 
            inst.opcode == "mul" || inst.opcode == "slt") {
            inst.isArithmetic = true;
        } else if (inst.opcode == "lw" || inst.opcode == "sw") {
            inst.isMemory = true;
        } else if (inst.opcode == "lw_spm" || inst.opcode == "sw_spm") {
            inst.isMemory = true;
            inst.isSPM = true;
        } else if (inst.opcode == "beq" || inst.opcode == "bne" || inst.opcode == "blt" || inst.opcode == "bge") {
            inst.isBranch = true;
        } else if (inst.opcode == "j" || inst.opcode == "jal") {
            inst.isJump = true;
        }
        
        // Parse operands based on instruction format
        std::string argStr;
        std::getline(iss, argStr);
        
        // Remove any comments after instruction
        size_t commentPos = argStr.find('#');
        if (commentPos != std::string::npos) {
            argStr = argStr.substr(0, commentPos);
        }
        
        // Trim whitespace
        argStr.erase(0, argStr.find_first_not_of(" \t,"));
        argStr.erase(argStr.find_last_not_of(" \t,") + 1);
        
        if (inst.isArithmetic) {
            parseArithmeticInstruction(inst, argStr);
        } else if (inst.isMemory) {
            parseMemoryInstruction(inst, argStr);
        } else if (inst.isBranch) {
            parseBranchInstruction(inst, argStr, coreId);
        } else if (inst.isJump) {
            parseJumpInstruction(inst, argStr);
        } else if (inst.opcode == "la") {
            parseLaInstruction(inst, argStr);
        }
        
        return inst;
    }
    
private:
    static void parseArithmeticInstruction(Instruction& inst, const std::string& argStr) {
        std::istringstream args(argStr);
        std::string token;
        
        // Parse destination register
        if (std::getline(args, token, ',')) {
            trimWhitespace(token);
            inst.rd = parseRegister(token);
        }
        
        // Parse first source register
        if (std::getline(args, token, ',')) {
            trimWhitespace(token);
            inst.rs1 = parseRegister(token);
        }
        
        // Parse second source or immediate
        if (std::getline(args, token)) {
            trimWhitespace(token);
            if (inst.opcode == "addi") {
                inst.immediate = parseImmediate(token);
            } else {
                inst.rs2 = parseRegister(token);
            }
        }
    }
    
    static void parseMemoryInstruction(Instruction& inst, const std::string& argStr) {
        std::regex memoryPattern(R"(x(\d+),\s*(\-?\d+)\((x\d+)\))");
        std::smatch matches;
        
        if (std::regex_search(argStr, matches, memoryPattern)) {
            if (inst.opcode == "lw" || inst.opcode == "lw_spm") {
                // lw rd, imm(rs1)
                inst.rd = parseRegister("x" + matches[1].str());
                inst.rs1 = parseRegister(matches[3].str());
                inst.immediate = parseImmediate(matches[2].str());
            } else if (inst.opcode == "sw" || inst.opcode == "sw_spm") {
                // sw rs2, imm(rs1)
                inst.rs2 = parseRegister("x" + matches[1].str());
                inst.rs1 = parseRegister(matches[3].str());
                inst.immediate = parseImmediate(matches[2].str());
            }
        } else {
            std::cerr << "Failed to parse memory instruction: " << argStr << std::endl;
        }
    }
    
    static void parseBranchInstruction(Instruction& inst, const std::string& argStr, int coreId) {
        std::istringstream args(argStr);
        std::string token;
        
        // Parse first source register
        if (std::getline(args, token, ',')) {
            trimWhitespace(token);
            inst.rs1 = parseRegister(token);
        }
        
        // Parse second source register
        if (std::getline(args, token, ',')) {
            trimWhitespace(token);
            inst.rs2 = parseRegister(token);
        }
        
        // Parse branch target label
        if (std::getline(args, token)) {
            trimWhitespace(token);
            inst.label = token;
        }
        
        // Special handling for branches based on CID
        if (inst.opcode == "beq" && inst.rs2 == 31) {
            // beq cid, <value>, <label>
            // rs2 now needs to hold the CID value to compare against
            inst.rs2 = coreId;
        }
    }
    
    static void parseJumpInstruction(Instruction& inst, const std::string& argStr) {
        // Parse jump target label
        std::istringstream args(argStr);
        std::string token;
        
        if (std::getline(args, token)) {
            trimWhitespace(token);
            inst.label = token;
        }
        
        // For jal, parse destination register
        if (inst.opcode == "jal") {
            if (std::getline(args, token, ',')) {
                trimWhitespace(token);
                inst.rd = parseRegister(token);
            }
        }
    }
    
    static void parseLaInstruction(Instruction& inst, const std::string& argStr) {
        std::istringstream args(argStr);
        std::string token;
        
        // Parse destination register
        if (std::getline(args, token, ',')) {
            trimWhitespace(token);
            inst.rd = parseRegister(token);
        }
        
        // Parse label
        if (std::getline(args, token)) {
            trimWhitespace(token);
            inst.label = token;
        }
    }
    
    static int parseRegister(const std::string& regStr) {
        // Extract register number (e.g., "x5" -> 5)
        if (regStr.size() < 2 || tolower(regStr[0]) != 'x') {
            std::cerr << "Invalid register: " << regStr << std::endl;
            return -1;
        }
        return std::stoi(regStr.substr(1));
    }
    
    static int parseImmediate(const std::string& immStr) {
        try {
            return std::stoi(immStr);
        } catch (const std::exception& e) {
            std::cerr << "Invalid immediate: " << immStr << std::endl;
            return 0;
        }
    }
    
    static void trimWhitespace(std::string& str) {
        str.erase(0, str.find_first_not_of(" \t"));
        str.erase(str.find_last_not_of(" \t") + 1);
    }
};

#endif // INSTRUCTION_PARSER_HPP