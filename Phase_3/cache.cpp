#include "cache.hpp"
#include "cache_system.hpp"
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <cmath>

Cache::Cache(const std::string& name, int cacheSize, int blockSize, int associativity, 
             int accessLatency, ReplacementPolicy policy)
    : name(name), cacheSize(cacheSize), blockSize(blockSize), 
      associativity(associativity), accessLatency(accessLatency), policy(policy) {
    
    // Validate parameters
    if (cacheSize <= 0 || blockSize <= 0 || associativity <= 0) {
        throw std::invalid_argument("Cache parameters must be positive");
    }
    
    if (cacheSize % blockSize != 0) {
        throw std::invalid_argument("Cache size must be a multiple of block size");
    }
    
    numSets = cacheSize / (blockSize * associativity);
    if (numSets <= 0) {
        throw std::invalid_argument("Invalid cache configuration");
    }
    
    // Calculate bit fields
    blockOffsetBits = static_cast<int>(std::log2(blockSize));
    setIndexBits = static_cast<int>(std::log2(numSets));
    tagBits = 32 - setIndexBits - blockOffsetBits;
    
    // Initialize cache sets
    sets.reserve(numSets);
    for (int i = 0; i < numSets; i++) {
        sets.emplace_back(associativity, blockSize);
    }
    
    std::cout << "Created " << name << " cache: " 
              << cacheSize << "B, " 
              << blockSize << "B blocks, "
              << associativity << "-way, "
              << numSets << " sets, "
              << "latency=" << accessLatency << " cycles" << std::endl;
}

void Cache::setNextLevelCache(std::unique_ptr<CacheSystem> next) {
    nextLevelCache = std::move(next);
}

uint32_t Cache::getTag(uint32_t address) const {
    return address >> (blockOffsetBits + setIndexBits);
}

uint32_t Cache::getSetIndex(uint32_t address) const {
    return (address >> blockOffsetBits) & ((1 << setIndexBits) - 1);
}

uint32_t Cache::getBlockOffset(uint32_t address) const {
    return address & ((1 << blockOffsetBits) - 1);
}

uint32_t Cache::getAddress(uint32_t tag, uint32_t setIndex) const {
    return (tag << (blockOffsetBits + setIndexBits)) | (setIndex << blockOffsetBits);
}

int Cache::findBlockInSet(uint32_t tag, uint32_t setIndex) const {
    auto& set = sets[setIndex];
    for (size_t i = 0; i < set.blocks.size(); i++) {
        if (set.blocks[i].valid && set.blocks[i].tag == tag) {
            return static_cast<int>(i);
        }
    }
    return -1;  // Block not found
}

int Cache::selectVictim(uint32_t setIndex) {
    auto& set = sets[setIndex];
    
    // Look for invalid block first
    for (size_t i = 0; i < set.blocks.size(); i++) {
        if (!set.blocks[i].valid) {
            return static_cast<int>(i);
        }
    }
    
    // Use the specified replacement policy
    if (policy == ReplacementPolicy::LRU) {
        // Find block with smallest timestamp (least recently used)
        int lruIndex = 0;
        int minTimestamp = set.blocks[0].timestamp;
        
        for (size_t i = 1; i < set.blocks.size(); i++) {
            if (set.blocks[i].timestamp < minTimestamp) {
                minTimestamp = set.blocks[i].timestamp;
                lruIndex = static_cast<int>(i);
            }
        }
        return lruIndex;
    } else {  // FIFO
        int fifoIndex = set.fifoQueue.front();
        set.fifoQueue.pop_front();
        set.fifoQueue.push_back(fifoIndex);
        return fifoIndex;
    }
}

void Cache::updateReplacementInfo(uint32_t setIndex, int blockIndex) {
    auto& set = sets[setIndex];
    
    if (policy == ReplacementPolicy::LRU) {
        // Update timestamp for LRU
        set.blocks[blockIndex].timestamp = globalTimestamp++;
    } else if (policy == ReplacementPolicy::FIFO && !set.blocks[blockIndex].valid) {
        // For FIFO, only update the queue when bringing in a new block
        set.fifoQueue.push_back(blockIndex);
    }
}
void Cache::resetStatistics() {
    accesses = 0;
    hits = 0;
    misses = 0;
}
std::pair<int, std::vector<uint8_t>> Cache::read(uint32_t address, int size) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    // Calculate cache addressing
    uint32_t tag = getTag(address);
    uint32_t setIndex = getSetIndex(address);
    uint32_t blockOffset = getBlockOffset(address);
    
    accesses++;
    
    // Check if the block is in the cache
    int blockIndex = findBlockInSet(tag, setIndex);
    int latency = accessLatency;
    
    if (blockIndex != -1) {
        // Cache hit
        hits++;
        updateReplacementInfo(setIndex, blockIndex);
        
        // Extract data from cache block
        std::vector<uint8_t> data(size);
        auto& block = sets[setIndex].blocks[blockIndex];
        
        // Ensure we don't read beyond block boundaries
        for (int i = 0; i < size; i++) {
            if (blockOffset + i < blockSize) {
                data[i] = block.data[blockOffset + i];
            } else {
                // Need to fetch another block for the rest of the data
                // This is a simplification - in reality we would need multiple cache accesses
                auto nextBlockData = read(address + i, size - i);
                for (int j = 0; j < size - i; j++) {
                    data[i + j] = nextBlockData.second[j];
                }
                latency += nextBlockData.first;
                break;
            }
        }
        
        return {latency, data};
    } else {
        // Cache miss
        misses++;
        
        // Fetch the block from the next level cache
        uint32_t blockAddress = address & ~((1 << blockOffsetBits) - 1);

        // Get data and latency from next level in one operation
        int nextLevelLatency = 0;
        std::vector<uint8_t> blockData;

        if (nextLevelCache) {
            auto result = nextLevelCache->read(blockAddress, blockSize);
            nextLevelLatency = result.first;
            blockData = result.second;
        } else {
            // Handle the case where there's no next level (should never happen in your design)
            blockData.resize(blockSize, 0);
        }

        // Select a victim block to replace
        blockIndex = selectVictim(setIndex);
        auto& block = sets[setIndex].blocks[blockIndex];

        // Handle writeback if necessary
        if (block.valid && block.dirty) {
            uint32_t victimAddress = getAddress(block.tag, setIndex);
            writeToNextLevel(victimAddress, block.data);
        }

        // Update the cache block
        block.tag = tag;
        block.valid = true;
        block.dirty = false;
        block.data = blockData;

        updateReplacementInfo(setIndex, blockIndex);

        // Extract the requested data
        std::vector<uint8_t> data(size);
        for (int i = 0; i < size; i++) {
            if (blockOffset + i < blockSize) {
                data[i] = block.data[blockOffset + i];
            } else {
                // Need to fetch another block for the rest of the data
                auto nextBlockData = read(address + i, size - i);
                for (int j = 0; j < size - i; j++) {
                    data[i + j] = nextBlockData.second[j];
                }
                latency += nextBlockData.first;
                break;
            }
        }

        // Add next level latency only once
        latency += nextLevelLatency;

        return {latency, data};
    }
}

int Cache::write(uint32_t address, const std::vector<uint8_t>& data) {
    std::cout <<" write to addr 0x" << std::hex << address
          << ", data = ";
    for (auto b : data) std::cout << std::hex << int(b) << " ";
    std::cout << std::endl;

    std::lock_guard<std::mutex> lock(cacheMutex);
    
    // Calculate cache addressing
    uint32_t tag = getTag(address);
    uint32_t setIndex = getSetIndex(address);
    uint32_t blockOffset = getBlockOffset(address);
    
    accesses++;
    
    // Check if the block is in the cache
    int blockIndex = findBlockInSet(tag, setIndex);
    int latency = accessLatency;
    
    if (blockIndex != -1) {
        // Cache hit
        hits++;
        updateReplacementInfo(setIndex, blockIndex);
        
        // Update the cache block
        auto& block = sets[setIndex].blocks[blockIndex];
        block.dirty = true;
        
        // Write data to the block
        for (size_t i = 0; i < data.size(); i++) {
            if (blockOffset + i < blockSize) {
                block.data[blockOffset + i] = data[i];
            } else {
                // Need to update another block for the rest of the data
                // This is a simplification - in reality we would need multiple cache accesses
                std::vector<uint8_t> remainingData(data.begin() + i, data.end());
                latency += write(address + i, remainingData);
                break;
            }
        }
        
        // Write-back policy: don't propagate to next level yet
        return latency;
    } else {
        // Cache miss
        misses++;
        
        // For write miss, we have two options:
        // 1. Write-allocate: fetch the block and then write to it (what we'll do)
        // 2. Write-no-allocate: write directly to next level without fetching
        
        // Fetch the block from the next level cache (write-allocate)
        uint32_t blockAddress = address & ~((1 << blockOffsetBits) - 1);
        std::vector<uint8_t> blockData = readFromNextLevel(blockAddress);
        
        // Select a victim block to replace
        blockIndex = selectVictim(setIndex);
        auto& block = sets[setIndex].blocks[blockIndex];
        
        // Handle writeback if necessary
        if (block.valid && block.dirty) {
            uint32_t victimAddress = getAddress(block.tag, setIndex);
            writeToNextLevel(victimAddress, block.data);
        }
        
        // Update the cache block
        block.tag = tag;
        block.valid = true;
        block.dirty = true;
        block.data = blockData;
        
        // Write data to the block
        for (size_t i = 0; i < data.size(); i++) {
            if (blockOffset + i < blockSize) {
                block.data[blockOffset + i] = data[i];
            } else {
                // Need to update another block for the rest of the data
                std::vector<uint8_t> remainingData(data.begin() + i, data.end());
                latency += write(address + i, remainingData);
                break;
            }
        }
        
        updateReplacementInfo(setIndex, blockIndex);
        
        // Calculate total latency: cache access + next level access
      //  latency += nextLevelCache ? nextLevelCache->read(blockAddress, blockSize).first : 0;
        
        return latency;
    }
}

void Cache::invalidateBlock(uint32_t address) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    uint32_t tag = getTag(address);
    uint32_t setIndex = getSetIndex(address);
    
    int blockIndex = findBlockInSet(tag, setIndex);
    if (blockIndex != -1) {
        auto& block = sets[setIndex].blocks[blockIndex];
        if (block.dirty) {
            // Writeback before invalidating
            uint32_t blockAddress = getAddress(block.tag, setIndex);
            writeToNextLevel(blockAddress, block.data);
        }
        block.valid = false;
    }
}

// void Cache::flushCache() {
//     std::lock_guard<std::mutex> lock(cacheMutex);
//
//     for (uint32_t setIndex = 0; setIndex < sets.size(); setIndex++) {
//         auto& set = sets[setIndex];
//         for (uint32_t blockIndex = 0; blockIndex < set.blocks.size(); blockIndex++) {
//             auto& block = set.blocks[blockIndex];
//             if (block.valid) {
//                 uint32_t blockAddress = getAddress(block.tag, setIndex);
//                 writeToNextLevel(blockAddress, block.data);
//                 block.dirty = false;
//             }
//         }
//     }
// }

// void Cache::flushCache() {
//
//     std::lock_guard<std::mutex> lock(cacheMutex);
//
//     for (uint32_t setIndex = 0; setIndex < sets.size(); setIndex++) {
//         auto& set = sets[setIndex];
//         for (uint32_t blockIndex = 0; blockIndex < set.blocks.size(); blockIndex++) {
//             auto& block = set.blocks[blockIndex];
//
//             // flush every valid block, whether dirty or clean
//             if (block.valid) {
//                 uint32_t blockAddress = getAddress(block.tag, setIndex);
//                 std::cout << "[FLUSH] Block addr = 0x" << std::hex << blockAddress << ", data: ";
//                 for (uint8_t b : block.data) std::cout << std::hex << int(b) << " ";
//                 std::cout << std::endl;
//                 writeToNextLevel(blockAddress, block.data);
//                 block.dirty = false;
//             }
//
//         }
//     }
// }
void Cache::flushCache() {
    // only write back lines that were actually modified
    CacheSystem* next = getNextLevelCache();
    if (!next) return;

    for (int setIdx = 0; setIdx < numSets; ++setIdx) {
        for (auto &blk : sets[setIdx].blocks) {
            if (blk.valid && blk.dirty) {
                // reconstruct the block-aligned address:
                uint32_t addr = getAddress(blk.tag, setIdx);
                next->write(addr, blk.data);  // push only dirty data
                blk.dirty = false;            // now clean/coherent
            }
        }
    }
}


double Cache::getHitRate() const {
    if (accesses == 0) return 0.0;
    return static_cast<double>(hits) / accesses;
}

std::vector<uint8_t> Cache::readFromNextLevel(uint32_t address) {
    if (!nextLevelCache) {
        throw std::runtime_error("No next level cache or memory configured");
    }
    
    auto [_, data] = nextLevelCache->read(address, blockSize);
    return data;
}

void Cache::writeToNextLevel(uint32_t address, const std::vector<uint8_t>& data) {
    if (!nextLevelCache) {
        throw std::runtime_error("No next level cache or memory configured");
    }

    const uint32_t blockSizeBytes = blockSize;
    // Align down to the start of the block
    uint32_t blockAddress = address & ~(blockSizeBytes - 1);
    // Offset within that block
    uint32_t offset       = address - blockAddress;

    // 1) Read the *entire* block from the next level
    auto [readLatency, existingData] =
        nextLevelCache->read(blockAddress, blockSizeBytes);

    // (optional) debug
    std::cout << "[WRITE_TO_NEXT] blockAddr=0x" << std::hex << blockAddress
              << " offset=" << std::dec << offset
              << " origData=";
    for (auto b : existingData) std::cout << std::hex << int(b) << " ";
    std::cout << std::endl;

    // 2) Merge in only our dirty bytes
    for (size_t i = 0; i < data.size(); ++i) {
        if (offset + i < blockSizeBytes) {
            existingData[offset + i] = data[i];
        }
    }

    // (optional) debug
    std::cout << "[WRITE_TO_NEXT] mergedData=";
    for (auto b : existingData) std::cout << std::hex << int(b) << " ";
    std::cout << std::endl;

    // 3) Write the full block back —
    //    if this is L1, it goes into L2; if L2, it ultimately hits DRAM.
    nextLevelCache->write(blockAddress, existingData);
}
