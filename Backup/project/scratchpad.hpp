#ifndef SCRATCHPAD_HPP
#define SCRATCHPAD_HPP

#include <vector>
#include <iostream>
#include <iomanip>

class Scratchpad {
private:
    int size;
    int accessLatency;
    std::vector<int> memory;
    
    // Statistics
    int loadCount;
    int storeCount;
    std::map<std::string, int> symbolMap;


public:
    Scratchpad(int sizeBytes, int latency) 
    : size(sizeBytes), accessLatency(latency), loadCount(0), storeCount(0) {
        if (sizeBytes <= 0) {
            throw std::invalid_argument("Scratchpad size must be positive");
        }
        if (sizeBytes % 4 != 0) {
            throw std::invalid_argument("Scratchpad size must be a multiple of 4 bytes");
        }
        if (latency <= 0) {
            throw std::invalid_argument("Scratchpad latency must be positive");
        }
        
        // Initialize memory (word-addressable, so size/4 words)
        memory.resize(sizeBytes / 4, 0);
    }
    
    int getAccessLatency() const {
        return accessLatency;
    }
    
    int getSize() const {
        return size;
    }
    
    int getLoadCount() const {
        return loadCount;
    }
    
    int getStoreCount() const {
        return storeCount;
    }
    
    void resetStats() {
        loadCount = 0;
        storeCount = 0;
    }
    bool isValidAddress(int address) const {
        return (address >= 0 && address < size && address % 4 == 0);
    }

    
    // Load a word from scratchpad
    int load(int address) {
        if (!isValidAddress(address)) {
            throw std::out_of_range("Scratchpad address out of range or not word-aligned");
        }

        loadCount++;
        return memory[address / 4];
    }

    
    // Store a word to scratchpad
    void store(int address, int value) {
        if (!isValidAddress(address)) {
            throw std::out_of_range("Scratchpad address out of range or not word-aligned");
        }

        storeCount++;
        memory[address / 4] = value;
    }

    
   void loadBlock(int spmAddress, const std::vector<int>& data) {
        if (!isValidAddress(spmAddress)) {
            throw std::out_of_range("Scratchpad address out of range or not word-aligned");
        }

        int spmIndex = spmAddress / 4;
        for (size_t i = 0; i < data.size() && spmIndex < memory.size(); i++, spmIndex++) {
            memory[spmIndex] = data[i];
        }
    }

    // Register a symbol for .data section
    void registerSymbol(const std::string& name, int address) {
        if (!isValidAddress(address)) {
            throw std::out_of_range("Symbol address out of range or not word-aligned");
        }
        symbolMap[name] = address;
    }

    // Get address for a symbol
    int getSymbolAddress(const std::string& name) const {
        auto it = symbolMap.find(name);
        if (it == symbolMap.end()) {
            throw std::runtime_error("Symbol not found in scratchpad: " + name);
        }
        return it->second;
    }

    // Dump contents of scratchpad for debugging
    void dump() const {
        std::cout << "Scratchpad Memory (" << size << " bytes, " << accessLatency << " cycle latency):\n";
        std::cout << "Stats: Loads=" << loadCount << ", Stores=" << storeCount << "\n";

        // Print symbols
        if (!symbolMap.empty()) {
            std::cout << "Symbols in Scratchpad:\n";
            for (const auto& pair : symbolMap) {
                std::cout << "  " << pair.first << " @ 0x" << std::hex << pair.second << std::dec << "\n";
            }
        }

        // Print memory contents
        for (int i = 0; i < (int)memory.size(); i++) {
            if (i % 4 == 0) {
                std::cout << std::hex << std::setw(8) << std::setfill('0') << (i * 4) << ": ";
            }

            std::cout << std::hex << std::setw(8) << std::setfill('0') << memory[i] << " ";

            if (i % 4 == 3 || i == (int)memory.size() - 1) {
                std::cout << std::endl;
            }
        }
        std::cout << std::dec;  // Reset to decimal output
    }
};

#endif // SCRATCHPAD_HPP
