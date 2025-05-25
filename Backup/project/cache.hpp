#ifndef CACHE_HPP
#define CACHE_HPP

#include <vector>
#include <list>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <iostream>
#include "shared_memory.hpp"

// Cache policy types
enum class WritePolicy {
    WRITE_BACK,
    WRITE_THROUGH
};

enum class WriteAllocatePolicy {
    WRITE_ALLOCATE,
    NO_WRITE_ALLOCATE
};

// Cache block structure
struct CacheBlock {
    bool valid;
    bool dirty;
    uint32_t tag;
    std::vector<int> data;
    int lastUsed;
    int insertionTime;

    CacheBlock(int blockSize)
        : valid(false), dirty(false), tag(0), data(blockSize/4, 0), lastUsed(0), insertionTime(0) {}
};

// Cache set structure
struct CacheSet {
    std::vector<CacheBlock> blocks;
    std::list<int> lruList;
    std::list<int> fifoList;

    CacheSet(int associativity, int blockSize) {
        blocks.reserve(associativity);
        for (int i = 0; i < associativity; i++) {
            blocks.emplace_back(blockSize);
            lruList.push_back(i);
            fifoList.push_back(i);
        }
    }
};

class Cache {
private:
    int cacheSize;
    int blockSize;
    int associativity;
    int accessLatency;
    int numSets;
    std::vector<CacheSet> sets;
    int accessCount;
    int missCount;
    int hitCount;
    int writeCount;
    int writeMissCount;
    int writeHitCount;
    int cycleCount;
    int globalCycle;

    int indexBits;
    int offsetBits;
    int tagBits;
    
    WritePolicy writePolicy;
    WriteAllocatePolicy writeAllocatePolicy;

public:
    enum class Policy { LRU, FIFO } replacementPolicy;

    Cache(int size, int blockSize, int assoc, int latency, 
          Policy p = Policy::LRU)
        : cacheSize(size), blockSize(blockSize), associativity(assoc),
          accessLatency(latency), accessCount(0), missCount(0), hitCount(0),
          writeCount(0), writeMissCount(0), writeHitCount(0), cycleCount(0), globalCycle(0),
          replacementPolicy(p), 
          writePolicy(WritePolicy::WRITE_BACK),  // Always write-back
          writeAllocatePolicy(WriteAllocatePolicy::WRITE_ALLOCATE)  // Always write-allocate
    {
        numSets = cacheSize / (blockSize * associativity);
        if (numSets == 0) {
            std::cerr << "[FATAL] Invalid cache config. Zero sets!" << std::endl;
            exit(1);
        }
        sets.reserve(numSets);
        for (int i = 0; i < numSets; i++) {
            sets.emplace_back(associativity, blockSize);
        }
        offsetBits = static_cast<int>(log2(blockSize));
        indexBits = static_cast<int>(log2(numSets));
        tagBits = 32 - indexBits - offsetBits;
    }

    struct CacheAccess {
        bool hit;
        int latency;
        int value;
    };

    void setReplacementPolicy(Policy p) { replacementPolicy = p; }
    void setWritePolicy(WritePolicy wp) { writePolicy = wp; }
    void setWriteAllocatePolicy(WriteAllocatePolicy wap) { writeAllocatePolicy = wap; }

    WritePolicy getWritePolicy() const { return writePolicy; }
    WriteAllocatePolicy getWriteAllocatePolicy() const { return writeAllocatePolicy; }
    Policy getReplacementPolicy() const { return replacementPolicy; }

    int getAccessLatency() const { return accessLatency; }
    void incrementCycle() { globalCycle++; }

    int getAccessCount() const { return accessCount; }
    int getHitCount() const { return hitCount; }
    int getMissCount() const { return missCount; }
    int getWriteCount() const { return writeCount; }
    int getWriteHitCount() const { return writeHitCount; }
    int getWriteMissCount() const { return writeMissCount; }

    void resetStats() {
        accessCount = 0;
        missCount = 0;
        hitCount = 0;
        writeCount = 0;
        writeMissCount = 0;
        writeHitCount = 0;
    }

    double calculateMissRate() const {
        return accessCount > 0 ? static_cast<double>(missCount) / accessCount : 0.0;
    }

    double calculateHitRate() const {
        return accessCount > 0 ? static_cast<double>(hitCount) / accessCount : 0.0;
    }

    double calculateWriteMissRate() const {
        return writeCount > 0 ? static_cast<double>(writeMissCount) / writeCount : 0.0;
    }

    double calculateWriteHitRate() const {
        return writeCount > 0 ? static_cast<double>(writeHitCount) / writeCount : 0.0;
    }


    // Load a full block from memory into cache
    void loadBlockFromMemory(uint32_t address,
                            std::shared_ptr<SharedMemory> memory,
                            int coreId) {
        uint32_t blockBase = address & ~(blockSize - 1);
        uint32_t index = (address >> offsetBits) & ((1u << indexBits) - 1);
        uint32_t tag = address >> (offsetBits + indexBits);
        CacheSet &set = sets[index];
        int victim = getVictimBlock(set);
        CacheBlock &blk = set.blocks[victim];

        // If using write-back policy and victim block is dirty, write it back
        if (writePolicy == WritePolicy::WRITE_BACK && blk.valid && blk.dirty) {
            uint32_t victimBlockBase = (blk.tag << (offsetBits + indexBits)) | (index << offsetBits);
            for (uint32_t off = 0; off < (uint32_t)blockSize; off += 4) {
                memory->storeWord(coreId, victimBlockBase + off, blk.data[off/4]);
            }
        }

        // Fill entire block from memory
        for (uint32_t off = 0; off < (uint32_t)blockSize; off += 4) {
            blk.data[off/4] = memory->loadWord(coreId, blockBase + off);
        }
        blk.tag = tag;
        blk.valid = true;
        blk.dirty = false;
        blk.insertionTime = globalCycle;
        updateReplacementPolicy(set, victim);
    }

    // Write cache block back to memory (for write-back policy)
    void writeBlockBackToMemory(uint32_t address,
                               std::shared_ptr<SharedMemory> memory,
                               int coreId) {
        uint32_t blockBase = address & ~(blockSize - 1);
        uint32_t index = (address >> offsetBits) & ((1u << indexBits) - 1);
        uint32_t tag = address >> (offsetBits + indexBits);
        CacheSet &set = sets[index];

        for (int i = 0; i < associativity; i++) {
            auto &blk = set.blocks[i];
            if (blk.valid && blk.tag == tag && blk.dirty) {
                for (uint32_t off = 0; off < (uint32_t)blockSize; off += 4) {
                    memory->storeWord(coreId, blockBase + off, blk.data[off/4]);
                }
                blk.dirty = false;
                return;
            }
        }
    }

   // In the Cache::read method, make this change:

CacheAccess read(uint32_t address) {
    accessCount++;
    int totalLatency = accessLatency;
    uint32_t offset = address & ((1u << offsetBits) - 1);
    uint32_t index = (address >> offsetBits) & ((1u << indexBits) - 1);
    uint32_t tag = address >> (offsetBits + indexBits);
    CacheSet &set = sets[index];
        std::cout << "Cache read: address=0x" << std::hex << address
                      << " (tag=0x" << tag << ", index=0x" << index
                      << ", offset=0x" << offset << ")" << std::dec << std::endl;

    // Check for hit
    for (int i = 0; i < associativity; i++) {
        auto &blk = set.blocks[i];
        if (blk.valid && blk.tag == tag) {
            // Hit - update replacement policy
            blk.lastUsed = globalCycle;
            updateReplacementPolicy(set, i);
            hitCount++;
            std::cout<<"HIT on address 0x" << std::hex << address
                      << " block tag=0x" << blk.tag << std::dec << std::endl;

            return {true, accessLatency, blk.data[offset/4]};  // Just return access latency, not total
        }
    }


    // Miss
    missCount++;
 std::cout<<"MISS on address 0x" << std::hex << address << std::dec << std::endl;
    return {false, accessLatency, 0};  // Just return access latency, not total
}

    // Write a word into cache
    CacheAccess write(uint32_t address, int value, std::shared_ptr<SharedMemory> memory, int coreId) {
        writeCount++;
        int totalLatency = accessLatency;
        uint32_t offset = address & ((1u << offsetBits) - 1);
        uint32_t index = (address >> offsetBits) & ((1u << indexBits) - 1);
        uint32_t tag = address >> (offsetBits + indexBits);
        CacheSet &set = sets[index];
        std::cout << "Cache write: address=0x" << std::hex << address
                      << " (tag=0x" << tag << ", index=0x" << index
                      << ", offset=0x" << offset << ") value=" << std::dec << value << std::endl;

        // Check for hit
        for (int i = 0; i < associativity; i++) {
            auto &blk = set.blocks[i];
            if (blk.valid && blk.tag == tag) {
                // Hit - update cache block data
                blk.data[offset/4] = value;
                blk.dirty = (writePolicy == WritePolicy::WRITE_BACK);
                blk.lastUsed = globalCycle;
                updateReplacementPolicy(set, i);
                writeHitCount++;
                std::cout<<"WRITE HIT on address 0x" << std::hex << address
                                   << " block tag=0x" << blk.tag << std::dec << std::endl;

                // For write-through, also update memory
                if (writePolicy == WritePolicy::WRITE_THROUGH) {
                    memory->storeWord(coreId, address, value);
                }

                return {true, totalLatency, value};
            }
        }

        // Miss
        writeMissCount++;
        std::cout<<"WRITE MISS on address 0x" << std::hex << address << std::dec << std::endl;

        // For write-allocate policy, allocate a block in cache
        if (writeAllocatePolicy == WriteAllocatePolicy::WRITE_ALLOCATE) {
            // Get victim block
            int victim = getVictimBlock(set);
            CacheBlock &blk = set.blocks[victim];

            // If using write-back and victim is dirty, write it back
            if (writePolicy == WritePolicy::WRITE_BACK && blk.valid && blk.dirty) {
                uint32_t victimBlockBase = (blk.tag << (offsetBits + indexBits)) | (index << offsetBits);
                for (uint32_t off = 0; off < (uint32_t)blockSize; off += 4) {
                    memory->storeWord(coreId, victimBlockBase + off, blk.data[off/4]);
                }
            }

            // Load block from memory
            uint32_t blockBase = address & ~(blockSize - 1);
            for (uint32_t off = 0; off < (uint32_t)blockSize; off += 4) {
                blk.data[off/4] = memory->loadWord(coreId, blockBase + off);
            }

            // Update the value for the specific word
            blk.data[offset/4] = value;
            blk.tag = tag;
            blk.valid = true;
            blk.dirty = (writePolicy == WritePolicy::WRITE_BACK);
            blk.lastUsed = globalCycle;
            blk.insertionTime = globalCycle;
            updateReplacementPolicy(set, victim);
        }

        // Always write to memory for write-through or no-write-allocate
        memory->storeWord(coreId, address, value);

        return {false, totalLatency, value};
    }

    uint32_t getBlockSize() const { return blockSize; }

private:
    // Update cache replacement policy (LRU or FIFO)
    void updateReplacementPolicy(CacheSet& set, int blockIndex) {
        if (replacementPolicy == Policy::LRU) {
            // LRU policy: move accessed block to front of LRU list
            set.lruList.remove(blockIndex);
            set.lruList.push_front(blockIndex);
        } else if (replacementPolicy == Policy::FIFO) {
            // For FIFO, we only update on insertion, not on access
            // So we do nothing here for existing blocks
            // But we need to update the FIFO list for new blocks
            if (set.blocks[blockIndex].insertionTime == globalCycle) {
                set.fifoList.remove(blockIndex);
                set.fifoList.push_front(blockIndex);
            }
        }
    }

    // Get victim block for replacement
    int getVictimBlock(CacheSet& set) {
        // First look for invalid blocks
        for (int i = 0; i < (int)set.blocks.size(); i++) {
            if (!set.blocks[i].valid) {
                return i;
            }
        }

        // If all blocks are valid, use the replacement policy
        if (replacementPolicy == Policy::LRU) {
            int victim = set.lruList.back();
            set.lruList.pop_back();
            return victim;
        } else { // FIFO
            int victim = set.fifoList.back();
            set.fifoList.pop_back();
            return victim;
        }
    }
};

#endif // CACHE_HPP