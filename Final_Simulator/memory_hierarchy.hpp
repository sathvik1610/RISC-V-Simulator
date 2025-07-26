#ifndef MEMORY_HIERARCHY_HPP
#define MEMORY_HIERARCHY_HPP

#include <memory>
#include <vector>
#include <mutex>
#include "cache.hpp"
#include "cache_system.hpp"
#include <atomic>



class MemoryHierarchy {
private:
    std::shared_ptr<MainMemory> mainMemory;
    std::shared_ptr<L2Cache> l2Cache;
    std::vector<std::shared_ptr<L1ICache>> l1ICaches;
    std::vector<std::shared_ptr<L1DCache>> l1DCaches;
    std::vector<std::shared_ptr<ScratchpadMemory>> scratchpads;
    int numCores;
    
public:
    MemoryHierarchy(int numCores, const std::string& configFile);
    std::shared_ptr<MainMemory> getMainMemory() const { return mainMemory; }
    std::pair<int, int32_t> fetchInstruction(int coreId, uint32_t address);

    void waitForAllWriteBacksToComplete();

    std::pair<int, int32_t> loadWord(int coreId, uint32_t address);
    int storeWord(int coreId, uint32_t address, int32_t value);
    
    std::pair<int, int32_t> loadWordFromSPM(int coreId, uint32_t address);
    int storeWordToSPM(int coreId, uint32_t address, int32_t value);
    /// Write back (and optionally invalidate) all dirty lines in coreId’s L1D.
    void flushL1D(int coreId);

    void printStatistics() const;
    // In memory_hierarchy.hpp

    // First, declare the struct for cache statistics
    struct CacheStats {
        uint64_t accesses;
        uint64_t hits;
        uint64_t misses;
    };
    // Add to memory_hierarchy.hpp in the MemoryHierarchy class
    enum class CacheType { L1I, L1D, L2 };

    CacheStats getCacheStats(CacheType type, int coreId = 0) const {
        if (type == CacheType::L1I) {
            if (coreId < 0 || coreId >= numCores) {
                throw std::out_of_range("Core ID out of range");
            }
            return {
                l1ICaches[coreId]->getAccesses(),
                l1ICaches[coreId]->getHits(),
                l1ICaches[coreId]->getMisses()
            };
        } else if (type == CacheType::L1D) {
            if (coreId < 0 || coreId >= numCores) {
                throw std::out_of_range("Core ID out of range");
            }
            return {
                l1DCaches[coreId]->getAccesses(),
                l1DCaches[coreId]->getHits(),
                l1DCaches[coreId]->getMisses()
            };
        } else if (type == CacheType::L2) {
            return {
                l2Cache->getAccesses(),
                l2Cache->getHits(),
                l2Cache->getMisses()
            };
        }
        throw std::invalid_argument("Invalid cache type");
    }
    // Make sure this method is a member of MemoryHierarchy class
    CacheStats getL1DCacheStats(int coreId) const {
        if (coreId < 0 || coreId >= numCores) {
            throw std::out_of_range("Core ID out of range");
        }
        return {
            l1DCaches[coreId]->getAccesses(),
            l1DCaches[coreId]->getHits(),
            l1DCaches[coreId]->getMisses()
        };
    }
    void resetStatistics() ;
    void flushCache();
    /// Return a const reference to the entire memory array
    const std::vector<uint8_t>& getRawMemory() const {
              return mainMemory->getRawMemory();
            }
    /// Write back all dirty lines to L2, then invalidate every line.
    void invalidateL1D(int coreId);
    std::shared_ptr<ScratchpadMemory> getSPM(int coreId) {
        return scratchpads[coreId];
    }

private:

    std::vector<bool> flushComplete;
    void loadConfiguration(const std::string& configFile);




    void setupMemoryHierarchy(int l1iSize, int l1dSize, int l2Size,
                              int l1iBlockSize, int l1dBlockSize, int l2BlockSize,
                              int l1iAssoc, int l1dAssoc, int l2Assoc,
                              int l1iLatency, int l1dLatency, int l2Latency, int memLatency,
                              int spmSize, int spmLatency,
                              ReplacementPolicy l1iPolicy, ReplacementPolicy l1dPolicy, ReplacementPolicy l2Policy);


};

#endif // MEMORY_HIERARCHY_HPP