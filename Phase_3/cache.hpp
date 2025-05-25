#ifndef CACHE_HPP
#define CACHE_HPP

#include <vector>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <memory>
#include <mutex>

// Forward declaration
class CacheSystem;

enum class ReplacementPolicy {
    LRU,   // Least Recently Used
    FIFO   // First In First Out
};

struct CacheBlock {
    uint32_t tag = 0;
    bool valid = false;
    bool dirty = false;
    std::vector<uint8_t> data;
    int timestamp = 0;  // Used for LRU/FIFO
    
    CacheBlock(int blockSize) : data(blockSize, 0) {}
};

struct CacheSet {
    std::vector<CacheBlock> blocks;
    std::deque<int> fifoQueue;  // For FIFO replacement policy
    
    CacheSet(int associativity, int blockSize) {
        for (int i = 0; i < associativity; i++) {
            blocks.emplace_back(blockSize);
        }
    }
};

class Cache {
protected:
    std::string name;
    int cacheSize;        // in bytes
    int blockSize;        // in bytes
    int associativity;    // 1 = direct mapped, cacheSize/blockSize = fully associative
    int accessLatency;    // in cycles
    ReplacementPolicy policy;
    
    int numSets;
    int setIndexBits;
    int blockOffsetBits;
    int tagBits;
    
    uint64_t accesses = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    
    std::vector<CacheSet> sets;

    std::mutex cacheMutex;
    
    int globalTimestamp = 0;

public:
    Cache(const std::string& name, int cacheSize, int blockSize, int associativity, 
          int accessLatency, ReplacementPolicy policy);
    
    virtual ~Cache() = default;
    
    void setNextLevelCache(std::unique_ptr<CacheSystem> next);
    // in Cache.h (or your shared interface header)
    //irtual std::vector<uint8_t> readBytes(uint32_t address, size_t length) = 0;
    CacheSystem* getNextLevelCache() const {
             return nextLevelCache.get();
            }
    virtual std::pair<int, std::vector<uint8_t>> read(uint32_t address, int size);
    virtual int write(uint32_t address, const std::vector<uint8_t>& data);
    void invalidateAll() {
        // For each set in the cache...
        for (auto &cacheSet : sets) {
            // For each block in that set...
            for (auto &block : cacheSet.blocks) {
                block.valid = false;
                block.dirty = false;
            }
        }
    }

    void invalidateBlock(uint32_t address);
    void flushCache();
    
    double getHitRate() const;
    uint64_t getAccesses() const { return accesses; }
    uint64_t getHits() const { return hits; }
    uint64_t getMisses() const { return misses; }
    
    std::string getName() const { return name; }
    int getCacheSize() const { return cacheSize; }
    int getBlockSize() const { return blockSize; }
    int getAssociativity() const { return associativity; }
    int getAccessLatency() const { return accessLatency; }
    void resetStatistics();
    
protected:
    std::unique_ptr<CacheSystem> nextLevelCache;
    uint32_t getTag(uint32_t address) const;
    uint32_t getSetIndex(uint32_t address) const;
    uint32_t getBlockOffset(uint32_t address) const;
    uint32_t getAddress(uint32_t tag, uint32_t setIndex) const;
    
    int findBlockInSet(uint32_t tag, uint32_t setIndex) const;
    int selectVictim(uint32_t setIndex);
    
    void updateReplacementInfo(uint32_t setIndex, int blockIndex);



    std::vector<uint8_t> readFromNextLevel(uint32_t address);
    void writeToNextLevel(uint32_t address, const std::vector<uint8_t>& data);
};

#endif // CACHE_HPP