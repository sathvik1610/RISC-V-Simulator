
#ifndef CACHE_SYSTEM_HPP
#define CACHE_SYSTEM_HPP

#include <memory>
#include <vector>
#include <cstdint>
#include "cache.hpp"
#include <stdexcept>
#include <mutex>
#include <string>
#include <iostream>



class MainMemory {
private:
    std::vector<uint8_t> memory;
    int accessLatency;  // in cycles
    std::mutex memoryMutex;

public:
    MainMemory(int size, int accessLatency)
        : memory(size, 0), accessLatency(accessLatency) {}
    void writeBytes(uint32_t address, const std::vector<uint8_t>& data) {
        std::cout << "[DRAM WRITE] Address 0x" << std::hex << address << " <- ";
        for (auto b : data) std::cout << std::hex << int(b) << " ";
        std::cout << std::endl;

        for (size_t i = 0; i < data.size(); ++i) {
            memory[address + i] = data[i];  // memory is a std::unordered_map<uint32_t, uint8_t> or vector
        }
    }
    // std::vector<uint8_t> MainMemory::readBytes(uint32_t address, uint32_t length) {
    //     std::lock_guard<std::mutex> lock(memoryMutex);
    //     std::vector<uint8_t> result(length);
    //     for (uint32_t i = 0; i < length; ++i) {
    //         if (address + i < memory.size()) {
    //             result[i] = memory[address + i];
    //         } else {
    //             result[i] = 0; // out-of-bounds reads return zero
    //         }
    //     }
    //     return result;
    // }


    std::pair<int, std::vector<uint8_t>> read(uint32_t address, int size) {
        std::lock_guard<std::mutex> lock(memoryMutex);
        std::vector<uint8_t> data(size);
        for (int i = 0; i < size; i++) {
            if (address + i < memory.size()) {
                data[i] = memory[address + i];
            }
        }
        return {accessLatency, data};
    }

    const std::vector<uint8_t>& getRawMemory() const {
        return memory;
    }
    int write(uint32_t address, const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(memoryMutex);
        for (size_t i = 0; i < data.size(); i++) {
            if (address + i < memory.size()) {
                memory[address + i] = data[i];
            }
        }
        return accessLatency;
    }

    void setWord(uint32_t address, int32_t value) {
        std::lock_guard<std::mutex> lock(memoryMutex);
        if (address + 3 < memory.size()) {
            memory[address] = value & 0xFF;
            memory[address + 1] = (value >> 8) & 0xFF;
            memory[address + 2] = (value >> 16) & 0xFF;
            memory[address + 3] = (value >> 24) & 0xFF;
        }
    }

    int32_t getWord(uint32_t address) const {
        if (address + 3 < memory.size()) {
            return memory[address] |
                  (memory[address + 1] << 8) |
                  (memory[address + 2] << 16) |
                  (memory[address + 3] << 24);
        }
        return 0;
    }

    int getAccessLatency() const { return accessLatency; }
};

class CacheSystem {
public:
    virtual std::pair<int, std::vector<uint8_t>> read(uint32_t address, int size) = 0;
    virtual int write(uint32_t address, const std::vector<uint8_t>& data) = 0;
    virtual ~CacheSystem() = default;
};

class L1ICache : public Cache, public CacheSystem {
public:
    L1ICache(int cacheSize, int blockSize, int associativity, int accessLatency, ReplacementPolicy policy)
        : Cache("L1I", cacheSize, blockSize, associativity, accessLatency, policy) {}

    std::pair<int, std::vector<uint8_t>> read(uint32_t address, int size) override {
        return Cache::read(address, size);
    }

    int write(uint32_t address, const std::vector<uint8_t>& data) override {
        return Cache::write(address, data);
    }
    void writeBackAndInvalidate() {
        CacheSystem* next = getNextLevelCache();
        if (!next) throw std::runtime_error("No L2 cache!");

        // (A) Write back only dirty lines
        for (int setIdx = 0; setIdx < numSets; ++setIdx) {
            for (auto &blk : sets[setIdx].blocks) {
                if (blk.valid && blk.dirty) {
                    uint32_t addr = getAddress(blk.tag, setIdx);
                    next->write(addr, blk.data);
                    blk.dirty = false;
                }
            }
        }

        // (B) Invalidate everything so future accesses come from L2
        invalidateAll();
    }

};

class L1DCache : public Cache, public CacheSystem {
private:
    bool isBlockValidInL1(uint32_t address) const {
        uint32_t tag      = getTag(address);
        uint32_t setIndex = getSetIndex(address);
        int blockIdx      = findBlockInSet(tag, setIndex);
        return (blockIdx >= 0
                && sets[setIndex].blocks[blockIdx].valid);
    }


public:
    L1DCache(int cacheSize, int blockSize, int associativity, int accessLatency, ReplacementPolicy policy)
        : Cache("L1D", cacheSize, blockSize, associativity, accessLatency, policy) {}

    std::pair<int, std::vector<uint8_t>> read(uint32_t address, int size) override {
        return Cache::read(address, size);
    }

    // int write(uint32_t address, const std::vector<uint8_t>& data) override {
    //     int latency = Cache::write(address, data);
    //
    //          // 2) write‐through: propagate straight to L2
    //          auto* next = getNextLevelCache();             // now available
    //          if (!next) throw std::runtime_error("No L2 cache!");
    //
    //          // we need to build a full block’s worth of data—
    //          // simplest is to re‐read the block from L1 and send it down:
    //          // uint32_t blockBase = address & ~(blockSize - 1);
    //          // auto blockBytes    = readFromNextLevel(blockBase); // inherited helper
    //          next->write(address,data);
    //
    //          return latency;
    // }
    int write(uint32_t addr, const std::vector<uint8_t>& data) override {
        // (1) always write-allocate so the line is in L1
        Cache::read(addr, data.size());
        // (2) update L1 (and mark it dirty, if you like)
        int latency = Cache::write(addr, data);
        // (3) write through to L2
        if (auto *l2 = getNextLevelCache()) {
            l2->write(addr, data);
        }
        return latency;
    }

    void invalidateAll() {
        for (auto& set : sets) {
            for (auto& block : set.blocks) {
                block.valid = false;
            }
        }
    }




    /// Push all dirty lines to L2, then invalidate every line in this L1D.

    // void writeBackAndInvalidate() {
    //     std::cout << "[L1DCache] writeBackAndInvalidate on core " <<"hii"<< "\n";
    //     // (A) Write back all dirty lines into L2 and clear dirty bits:
    //     flushCache();            // inherited from CacheSystem
    //
    //     // (B) Invalidate every line so future loads miss here:
    //     invalidateAll();         // inherited from Cache
    // }
    void writeBackAndInvalidate() {
        CacheSystem* next = getNextLevelCache();
        if (!next) throw std::runtime_error("No L2 cache!");

        // (A) Write back only dirty lines
        for (int setIdx = 0; setIdx < numSets; ++setIdx) {
            for (auto &blk : sets[setIdx].blocks) {
                if (blk.valid && blk.dirty) {
                    uint32_t addr = getAddress(blk.tag, setIdx);
                    next->write(addr, blk.data);
                    blk.dirty = false;
                }
            }
        }

        // (B) Invalidate everything so future accesses come from L2
        invalidateAll();
    }






};

class L2Cache : public Cache, public CacheSystem {
public:
    L2Cache(int cacheSize, int blockSize, int associativity, int accessLatency, ReplacementPolicy policy)
        : Cache("L2", cacheSize, blockSize, associativity, accessLatency, policy) {}

    std::pair<int, std::vector<uint8_t>> read(uint32_t address, int size) override {
        return Cache::read(address, size);
    }

    int write(uint32_t address, const std::vector<uint8_t>& data) override {
        return Cache::write(address, data);
    }
};

class ScratchpadMemory : public CacheSystem {
private:
    std::vector<uint8_t> memory;
    int size;
    int accessLatency;
    std::mutex spmMutex;

public:
    ScratchpadMemory(int size, int accessLatency)
        : memory(size, 0), size(size), accessLatency(accessLatency) {}

    std::pair<int, std::vector<uint8_t>> read(uint32_t address, int size) override {
        std::lock_guard<std::mutex> lock(spmMutex);
        std::vector<uint8_t> data(size);
        for (int i = 0; i < size; i++) {
            if (address + i < memory.size()) {
                data[i] = memory[address + i];
            }
        }
        return {accessLatency, data};
    }

    int write(uint32_t address, const std::vector<uint8_t>& data) override {
        std::lock_guard<std::mutex> lock(spmMutex);
        for (size_t i = 0; i < data.size(); i++) {
            if (address + i < memory.size()) {
                memory[address + i] = data[i];
            }
        }
        return accessLatency;
    }

    int32_t loadWord(uint32_t address) {
        if (address % 4 != 0 || address >= static_cast<uint32_t>(size)) {
            throw std::runtime_error("Invalid SPM memory access at address " + std::to_string(address));
        }

        std::lock_guard<std::mutex> lock(spmMutex);
        return memory[address] |
              (memory[address + 1] << 8) |
              (memory[address + 2] << 16) |
              (memory[address + 3] << 24);
    }

    void storeWord(uint32_t address, int32_t value) {
        if (address % 4 != 0 || address >= static_cast<uint32_t>(size)) {
            throw std::runtime_error("Invalid SPM memory access at address " + std::to_string(address));
        }

        std::lock_guard<std::mutex> lock(spmMutex);
        memory[address] = value & 0xFF;
        memory[address + 1] = (value >> 8) & 0xFF;
        memory[address + 2] = (value >> 16) & 0xFF;
        memory[address + 3] = (value >> 24) & 0xFF;
    }

    int getAccessLatency() const { return accessLatency; }
};

class MemorySystem : public CacheSystem {
private:
    std::shared_ptr<MainMemory> mainMemory;
    std::shared_ptr<CacheSystem> cacheSystem;
    bool useCache;

public:
    // Constructor for main memory
    MemorySystem(std::shared_ptr<MainMemory> memory)
        : mainMemory(memory), useCache(false) {}

    // Constructor for cache system
    MemorySystem(std::shared_ptr<CacheSystem> cache)
        : cacheSystem(cache), useCache(true) {}

    std::pair<int, std::vector<uint8_t>> read(uint32_t address, int size) override {
        if (useCache) {
            return cacheSystem->read(address, size);
        } else {
            return mainMemory->read(address, size);
        }
    }

    int write(uint32_t address, const std::vector<uint8_t>& data) override {
        if (useCache) {
            return cacheSystem->write(address, data);
        } else {
            return mainMemory->write(address, data);
        }
    }
};

#endif // CACHE_SYSTEM_HPP