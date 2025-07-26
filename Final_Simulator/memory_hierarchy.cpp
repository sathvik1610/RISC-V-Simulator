#include "memory_hierarchy.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <thread>

MemoryHierarchy::MemoryHierarchy(int numCores, const std::string& configFile)
    : numCores(numCores) {
    loadConfiguration(configFile);
}

void MemoryHierarchy::loadConfiguration(const std::string& configFile) {
    // Default configuration values
    int l1iSize = 16 * 1024;     // 16KB
    int l1dSize = 16 * 1024;     // 16KB
    int l2Size = 256 * 1024;     // 256KB
    int l1iBlockSize = 64;       // 64B
    int l1dBlockSize = 64;       // 64B
    int l2BlockSize = 64;        // 64B
    int l1iAssoc = 2;            // 2-way
    int l1dAssoc = 4;            // 4-way
    int l2Assoc = 8;             // 8-way
    int l1iLatency = 1;          // 1 cycle
    int l1dLatency = 1;          // 1 cycle
    int l2Latency = 10;          // 10 cycles
    int memLatency = 100;        // 100 cycles
    int spmSize = 16 * 1024;     // 16KB (same as L1D)
    int spmLatency = 1;          // 1 cycle (same as L1D)
    
    ReplacementPolicy l1iPolicy = ReplacementPolicy::LRU;
    ReplacementPolicy l1dPolicy = ReplacementPolicy::LRU;
    ReplacementPolicy l2Policy = ReplacementPolicy::LRU;
    
    // Try to load configuration from file
    try {
        std::ifstream file(configFile);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                std::istringstream iss(line);
                std::string key;
                if (std::getline(iss, key, '=')) {
                    std::string value;
                    if (std::getline(iss, value)) {
                        // Remove whitespace
                        key.erase(0, key.find_first_not_of(" \t"));
                        key.erase(key.find_last_not_of(" \t") + 1);
                        value.erase(0, value.find_first_not_of(" \t"));
                        value.erase(value.find_last_not_of(" \t") + 1);
                        
                        if (key == "L1I_SIZE") l1iSize = std::stoi(value);
                        else if (key == "L1D_SIZE") l1dSize = std::stoi(value);
                        else if (key == "L2_SIZE") l2Size = std::stoi(value);
                        else if (key == "L1I_BLOCK_SIZE") l1iBlockSize = std::stoi(value);
                        else if (key == "L1D_BLOCK_SIZE") l1dBlockSize = std::stoi(value);
                        else if (key == "L2_BLOCK_SIZE") l2BlockSize = std::stoi(value);
                        else if (key == "L1I_ASSOC") l1iAssoc = std::stoi(value);
                        else if (key == "L1D_ASSOC") l1dAssoc = std::stoi(value);
                        else if (key == "L2_ASSOC") l2Assoc = std::stoi(value);
                        else if (key == "L1I_LATENCY") l1iLatency = std::stoi(value);
                        else if (key == "L1D_LATENCY") l1dLatency = std::stoi(value);
                        else if (key == "L2_LATENCY") l2Latency = std::stoi(value);
                        else if (key == "MEM_LATENCY") memLatency = std::stoi(value);
                        else if (key == "SPM_SIZE") spmSize = std::stoi(value);
                        else if (key == "SPM_LATENCY") spmLatency = std::stoi(value);
                        else if (key == "L1I_POLICY") {
                            if (value == "LRU") l1iPolicy = ReplacementPolicy::LRU;
                            else if (value == "FIFO") l1iPolicy = ReplacementPolicy::FIFO;
                        }
                        else if (key == "L1D_POLICY") {
                            if (value == "LRU") l1dPolicy = ReplacementPolicy::LRU;
                            else if (value == "FIFO") l1dPolicy = ReplacementPolicy::FIFO;
                        }
                        else if (key == "L2_POLICY") {
                            if (value == "LRU") l2Policy = ReplacementPolicy::LRU;
                            else if (value == "FIFO") l2Policy = ReplacementPolicy::FIFO;
                        }
                    }
                }
            }
            file.close();
            std::cout << "Cache configuration loaded from " << configFile << std::endl;
        } else {
            std::cout << "Cache configuration file " << configFile << " not found, using defaults" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading cache configuration: " << e.what() << std::endl;
        std::cerr << "Using default configuration" << std::endl;
    }
    
    // Setup the memory hierarchy with the loaded configuration
    setupMemoryHierarchy(l1iSize, l1dSize, l2Size, 
                       l1iBlockSize, l1dBlockSize, l2BlockSize,
                       l1iAssoc, l1dAssoc, l2Assoc,
                       l1iLatency, l1dLatency, l2Latency, memLatency,
                       spmSize, spmLatency,
                       l1iPolicy, l1dPolicy, l2Policy);
}

void MemoryHierarchy::flushL1D(int coreId) {


    if (coreId < 0 || coreId >= numCores)
        throw std::out_of_range("Core ID out of range");
    std::cout << "[MemoryHierarchy] flushL1D(" << coreId << ")\n";
    // write back AND invalidate coreId’s L1D:
    l1DCaches[coreId]->writeBackAndInvalidate();
}



void MemoryHierarchy::flushCache() {
    // First: write back everything in each core’s L1 instruction and data caches
    for (auto& c : l1ICaches) c->writeBackAndInvalidate();
       for (auto& c : l1DCaches) c->writeBackAndInvalidate();

    // Then: write back any dirty lines still sitting in the shared L2
    l2Cache->flushCache();
}

void MemoryHierarchy::setupMemoryHierarchy(
    int l1iSize, int l1dSize, int l2Size,
    int l1iBlockSize, int l1dBlockSize, int l2BlockSize,
    int l1iAssoc, int l1dAssoc, int l2Assoc,
    int l1iLatency, int l1dLatency, int l2Latency, int memLatency,
    int spmSize, int spmLatency,
    ReplacementPolicy l1iPolicy, ReplacementPolicy l1dPolicy, ReplacementPolicy l2Policy) {

    // Create main memory (4MB for simplicity)
    mainMemory = std::make_shared<MainMemory>(4 * 1024, memLatency);

    // Create L2 cache (shared by all cores)
    l2Cache = std::make_shared<L2Cache>(l2Size, l2BlockSize, l2Assoc, l2Latency, l2Policy);

    // Connect L2 to main memory
    auto memorySystem = std::make_unique<MemorySystem>(mainMemory);
    l2Cache->setNextLevelCache(std::move(memorySystem));

    // Create L1 caches for each core
    for (int i = 0; i < numCores; i++) {
        // Create L1I cache
        auto l1i = std::make_shared<L1ICache>(l1iSize, l1iBlockSize, l1iAssoc, l1iLatency, l1iPolicy);

        // Create L1D cache
        auto l1d = std::make_shared<L1DCache>(l1dSize, l1dBlockSize, l1dAssoc, l1dLatency, l1dPolicy);

        // Create scratchpad memory
        auto spm = std::make_shared<ScratchpadMemory>(spmSize, spmLatency);

        // Connect L1 caches to L2
        // Use the CacheSystem constructor that accepts a shared_ptr to CacheSystem
        l1i->setNextLevelCache(std::make_unique<MemorySystem>(l2Cache));
        l1d->setNextLevelCache(std::make_unique<MemorySystem>(l2Cache));

        // Store the caches
        l1ICaches.push_back(l1i);
        l1DCaches.push_back(l1d);
        scratchpads.push_back(spm);
    }

    std::cout << "Memory hierarchy initialized for " << numCores << " cores" << std::endl;
}
void MemoryHierarchy::invalidateL1D(int coreID) {
    if (coreID >= 0 && coreID < numCores) {
        l1DCaches[coreID]->invalidateAll();
    }
}
void MemoryHierarchy::resetStatistics() {
    for (auto& cache : l1ICaches) {
        cache->resetStatistics();
    }
    for (auto& cache : l1DCaches) {
        cache->resetStatistics();
    }
    l2Cache->resetStatistics();
}
std::pair<int, int32_t> MemoryHierarchy::fetchInstruction(int coreId, uint32_t address) {
    if (coreId < 0 || coreId >= numCores) {
        throw std::out_of_range("Core ID out of range");
    }
    
    // Align address to word boundary
    address = address & ~0x3;
    
    // Use L1I cache to fetch the instruction
    auto [latency, data] = l1ICaches[coreId]->read(address, 4);
    
    // Convert bytes to 32-bit instruction
    int32_t instruction = 0;
    for (int i = 0; i < 4; i++) {
        instruction |= (static_cast<int32_t>(data[i]) << (i * 8));
    }
    
    return {latency, instruction};
}


std::pair<int, int32_t> MemoryHierarchy::loadWord(int coreId, uint32_t address) {
    if (coreId < 0 || coreId >= numCores) {
        throw std::out_of_range("Core ID out of range");
    }
    
    // Align address to word boundary
    address = address & ~0x3;
    
    // Use L1D cache to load the word
    auto [latency, data] = l1DCaches[coreId]->read(address, 4);
    
    // Convert bytes to 32-bit word
    int32_t word = 0;
    for (int i = 0; i < 4; i++) {
        word |= (static_cast<int32_t>(data[i]) << (i * 8));
    }
    
    return {latency, word};
}

int MemoryHierarchy::storeWord(int coreId, uint32_t address, int32_t value) {
    if (coreId < 0 || coreId >= numCores) {
        throw std::out_of_range("Core ID out of range");
    }
    
    // Align address to word boundary
    address = address & ~0x3;
    
    // Convert 32-bit word to bytes
    std::vector<uint8_t> data(4);
    for (int i = 0; i < 4; i++) {
        data[i] = (value >> (i * 8)) & 0xFF;
    }
    
    // Use L1D cache to store the word
    return l1DCaches[coreId]->write(address, data);
}

std::pair<int, int32_t> MemoryHierarchy::loadWordFromSPM(int coreId, uint32_t address) {
    if (coreId < 0 || coreId >= numCores) {
        throw std::out_of_range("Core ID out of range");
    }
    
    // Align address to word boundary
    address = address & ~0x3;
    
    // Load from scratchpad memory
    int32_t value = scratchpads[coreId]->loadWord(address);
    
    return {scratchpads[coreId]->getAccessLatency(), value};
}

int MemoryHierarchy::storeWordToSPM(int coreId, uint32_t address, int32_t value) {
    if (coreId < 0 || coreId >= numCores) {
        throw std::out_of_range("Core ID out of range");
    }
    
    // Align address to word boundary
    address = address & ~0x3;
    
    // Store to scratchpad memory
    scratchpads[coreId]->storeWord(address, value);
    
    return scratchpads[coreId]->getAccessLatency();
}

void MemoryHierarchy::printStatistics() const {
    std::cout << "\n=== Memory Hierarchy Statistics ===\n";
    
    // L1I caches
    std::cout << "\nL1I Caches:\n";
    double totalL1IHitRate = 0.0;
    uint64_t totalL1IAccesses = 0;
    
    for (int i = 0; i < numCores; i++) {
        const auto& cache = l1ICaches[i];
        double hitRate = cache->getHitRate();
        totalL1IHitRate += hitRate * cache->getAccesses();
        totalL1IAccesses += cache->getAccesses();
        
        std::cout << "  Core " << i << ": "
                  << "Accesses=" << cache->getAccesses() << ", "
                  << "Hits=" << cache->getHits() << ", "
                  << "Misses=" << cache->getMisses() << ", "
                  << "Hit Rate=" << (hitRate * 100.0) << "%" << std::endl;
    }
    
    if (totalL1IAccesses > 0) {
        totalL1IHitRate /= totalL1IAccesses;
        std::cout << "  Overall L1I Hit Rate: " << (totalL1IHitRate * 100.0) << "%" << std::endl;
    }
    
    // L1D caches
    std::cout << "\nL1D Caches:\n";
    double totalL1DHitRate = 0.0;
    uint64_t totalL1DAccesses = 0;
    
    for (int i = 0; i < numCores; i++) {
        const auto& cache = l1DCaches[i];
        double hitRate = cache->getHitRate();
        totalL1DHitRate += hitRate * cache->getAccesses();
        totalL1DAccesses += cache->getAccesses();
        
        std::cout << "  Core " << i << ": "
                  << "Accesses=" << cache->getAccesses() << ", "
                  << "Hits=" << cache->getHits() << ", "
                  << "Misses=" << cache->getMisses() << ", "
                  << "Hit Rate=" << (hitRate * 100.0) << "%" << std::endl;
    }
    
    if (totalL1DAccesses > 0) {
        totalL1DHitRate /= totalL1DAccesses;
        std::cout << "  Overall L1D Hit Rate: " << (totalL1DHitRate * 100.0) << "%" << std::endl;
    }
    
    // L2 cache
    std::cout << "\nL2 Cache:\n";
    std::cout << "  Accesses=" << l2Cache->getAccesses() << ", "
              << "Hits=" << l2Cache->getHits() << ", "
              << "Misses=" << l2Cache->getMisses() << ", "
              << "Hit Rate=" << (l2Cache->getHitRate() * 100.0) << "%" << std::endl;
    
    // Calculate overall miss rates
    double l1iMissRate = (totalL1IAccesses > 0) ? (1.0 - totalL1IHitRate) : 0.0;
    double l1dMissRate = (totalL1DAccesses > 0) ? (1.0 - totalL1DHitRate) : 0.0;
    double l2MissRate = (l2Cache->getAccesses() > 0) ? (1.0 - l2Cache->getHitRate()) : 0.0;
    
    std::cout << "\nOverall Cache Miss Rates:\n";
    std::cout << "  L1I Miss Rate: " << (l1iMissRate * 100.0) << "%" << std::endl;
    std::cout << "  L1D Miss Rate: " << (l1dMissRate * 100.0) << "%" << std::endl;
    std::cout << "  L2 Miss Rate: " << (l2MissRate * 100.0) << "%" << std::endl;
}