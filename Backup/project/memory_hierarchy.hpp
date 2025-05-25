#ifndef MEMORY_HIERARCHY_HPP
#define MEMORY_HIERARCHY_HPP

#include <iostream>
#include <vector>
#include <memory>
#include <stdexcept>
#include "Cache.hpp"
#include "Shared_memory.hpp"

class MemoryHierarchy
{
public:
    struct Access
    {
        bool hit;
        int latency;
        int value;
    };

    MemoryHierarchy(
        int numCores,
        int l1Size, int l1BlockSize, int l1Assoc, int l1Latency,
        int l2Size, int l2BlockSize, int l2Assoc, int l2Latency,
        int memLatency,
        std::shared_ptr<SharedMemory> memory,
        Cache::Policy replacementPolicy,
        WritePolicy writePolicy,
        WriteAllocatePolicy writeAllocatePolicy) : numCores(numCores),
                                                   memoryLatency(memLatency),
                                                   globalCycle(0),
                                                   memory(memory),
                                                   replacementPolicy(replacementPolicy),
                                                   writePolicy(WritePolicy::WRITE_BACK),
                                                   writeAllocatePolicy(WriteAllocatePolicy::WRITE_ALLOCATE),
                                                   memAccessCount(0)
    {
        l2Cache = std::make_shared<Cache>(
            l2Size, l2BlockSize, l2Assoc, l2Latency,
            replacementPolicy);

        for (int i = 0; i < numCores; i++)
        {
            auto l1ICache = std::make_shared<Cache>(
                l1Size, l1BlockSize, l1Assoc, l1Latency,
                replacementPolicy);
            auto l1DCache = std::make_shared<Cache>(
                l1Size, l1BlockSize, l1Assoc, l1Latency,
                replacementPolicy);
            l1ICaches.push_back(l1ICache);
            l1DCaches.push_back(l1DCache);
        }
    }

    void setCachePolicy(Cache::Policy policy)
    {
        this->replacementPolicy = policy;
        l2Cache->setReplacementPolicy(policy);
        for (auto &cache : l1ICaches)
            cache->setReplacementPolicy(policy);
        for (auto &cache : l1DCaches)
            cache->setReplacementPolicy(policy);
    }

    void setWritePolicy(WritePolicy policy)
    {
        writePolicy = WritePolicy::WRITE_BACK;
        l2Cache->setWritePolicy(writePolicy);
        for (auto &cache : l1ICaches)
            cache->setWritePolicy(writePolicy);
        for (auto &cache : l1DCaches)
            cache->setWritePolicy(writePolicy);
    }

    void setWriteAllocatePolicy(WriteAllocatePolicy policy)
    {
        writeAllocatePolicy = WriteAllocatePolicy::WRITE_ALLOCATE;
        l2Cache->setWriteAllocatePolicy(writeAllocatePolicy);
        for (auto &cache : l1ICaches)
            cache->setWriteAllocatePolicy(writeAllocatePolicy);
        for (auto &cache : l1DCaches)
            cache->setWriteAllocatePolicy(writeAllocatePolicy);
    }

    Access fetchInstruction(uint32_t address, int coreId)
    {
        if (coreId < 0 || coreId >= numCores)
            throw std::out_of_range("Core ID out of range in fetchInstruction");

        std::cout << "[Core " << coreId << "] Fetching instruction at 0x"
                  << std::hex << address << std::dec << std::endl;

        auto &l1i = l1ICaches[coreId];
        auto l1Access = l1i->read(address);

        if (l1Access.hit)
        {
            std::cout << "[Core " << coreId << "] L1I hit for instruction fetch" << std::endl;
            return {true, l1Access.latency, l1Access.value};
        }

        std::cout << "[Core " << coreId << "] L1I miss for instruction fetch, trying L2" << std::endl;
        auto l2Access = l2Cache->read(address);

        if (l2Access.hit)
        {
            std::cout << "[Core " << coreId << "] L2 hit for instruction fetch" << std::endl;

            // Load data into L1 without additional L2 accesses
            uint32_t blockSize = l1i->getBlockSize();
            uint32_t blockAddr = address & ~(blockSize - 1);

            for (uint32_t offset = 0; offset < blockSize; offset += 4) {
                uint32_t wordAddr = blockAddr + offset;
                // Get value directly from L2 cache or memory
                int value = memory->loadWord(coreId, wordAddr);
                // No need to update stats for this memory access
            }

            // Update L1I cache
            l1i->loadBlockFromMemory(address, memory, coreId);

            // Return combined latency
            return {false, l1Access.latency + l2Access.latency, l2Access.value};
        }

        std::cout << "[Core " << coreId << "] L2 miss for instruction fetch, going to memory" << std::endl;
        int memValue = memory->loadWord(coreId, address);
        memAccessCount++;

        // Need to manually load block into L2 and L1I caches
        // Without causing additional L2 access counts

        // Load block into L2 cache once
        uint32_t blockAddr = address & ~((uint32_t)l2Cache->getBlockSize() - 1);
        l2Cache->loadBlockFromMemory(blockAddr, memory, coreId);

        // Then load into L1I cache
        l1i->loadBlockFromMemory(address, memory, coreId);

        // Return combined latency
        return {false, l1Access.latency + l2Access.latency + memoryLatency, memValue};
    }

    std::shared_ptr<SharedMemory> getRawMemoryPtr() const
    {
        return memory;
    }

    Access accessData(int coreId, uint32_t address, bool isStore, int value = 0)
    {
        if (coreId < 0 || coreId >= numCores)
            throw std::out_of_range("Core ID out of range in accessData");
uint32_t originalAddress = address;

        // —— 1) Align for L1-D ——
        auto &l1d = l1DCaches[coreId];
        uint32_t bsize1 = l1d->getBlockSize();
        uint32_t addrL1 = address & ~(bsize1 - 1);

        std::cout << "[Core " << coreId << "] "
                  << (isStore ? "Store" : "Load")
                  << " at 0x" << std::hex << addrL1 << std::dec
                  << (isStore ? " value=" + std::to_string(value) : "")
                  << std::endl;

        int totalLatency = 0;
        bool hit = false;
        int readValue = 0;

        if (isStore)
        {
            // —— Write: L1-D then L2-D/MM on miss ——
            auto l1Access = l1d->write(addrL1, value, memory, coreId);
            totalLatency = l1d->getAccessLatency();
            hit = l1Access.hit;

            if (!hit)
            {
                // —— Align for L2-D ——
                uint32_t bsize2 = l2Cache->getBlockSize();
                uint32_t addrL2 = address & ~(bsize2 - 1);

                auto l2Access = l2Cache->write(addrL2, value, memory, coreId);
                totalLatency += l2Cache->getAccessLatency();

                if (!l2Access.hit)
                {
                    totalLatency += memoryLatency;
                    memAccessCount++;
                }
                // Update L1D cache
                l1d->loadBlockFromMemory(addrL1, memory, coreId);
            }
            readValue = value;
        }
        else
        {
            // —— Read path: L1-D, then L2-D, then memory ——
            auto l1Access = l1d->read(addrL1);
            totalLatency = l1d->getAccessLatency();
            hit = l1Access.hit;

            if (hit)
            {
                readValue = l1Access.value;
            }
            else
            {
                // —— Align for L2-D ——
                uint32_t bsize2 = l2Cache->getBlockSize();
                uint32_t addrL2 = address & ~(bsize2 - 1);

                auto l2Access = l2Cache->read(addrL2);
                totalLatency += l2Cache->getAccessLatency();

                if (l2Access.hit)
                {
                    readValue = l2Access.value;

                    // Load data directly from L2 to L1 without additional access
                    uint32_t blockSize = l1d->getBlockSize();
                    uint32_t blockAddr = addrL1 & ~(blockSize - 1);

                    // Update L1D cache
                    l1d->loadBlockFromMemory(addrL1, memory, coreId);
                }
                else
                {
                    // —— truly miss out to memory ——
                    readValue = memory->loadWord(coreId, address);
                    memAccessCount++;
                    totalLatency += memoryLatency;

                    // Load block into L2 and then L1
                    // Do this in one step to avoid multiple L2 accesses
                    l2Cache->loadBlockFromMemory(addrL2, memory, coreId);
                    l1d->loadBlockFromMemory(addrL1, memory, coreId);
                }
            }
        }

        return {hit, totalLatency, readValue};
    }

    void resetStats()
    {
        l2Cache->resetStats();
        for (auto &cache : l1ICaches)
            cache->resetStats();
        for (auto &cache : l1DCaches)
            cache->resetStats();
        memAccessCount = 0;
    }

    void incrementCycle()
    {
        globalCycle++;
        l2Cache->incrementCycle();
        for (auto &cache : l1ICaches)
            cache->incrementCycle();
        for (auto &cache : l1DCaches)
            cache->incrementCycle();
    }

    double getL2MissRate() const { return l2Cache->calculateMissRate(); }
    int getL2HitCount() const { return l2Cache->getHitCount(); }
    int getL2MissCount() const { return l2Cache->getMissCount(); }
    int getL2AccessCount() const { return l2Cache->getAccessCount(); }
    int getMemAccessCount() const { return memAccessCount; }

    std::shared_ptr<Cache> getL2Cache() const { return l2Cache; }

private:
    int numCores;
    int memoryLatency;
    int globalCycle;
    int memAccessCount;

    std::shared_ptr<SharedMemory> memory;
    Cache::Policy replacementPolicy;
    WritePolicy writePolicy;
    WriteAllocatePolicy writeAllocatePolicy;

    std::shared_ptr<Cache> l2Cache;
    std::vector<std::shared_ptr<Cache>> l1ICaches;
    std::vector<std::shared_ptr<Cache>> l1DCaches;
};

#endif // MEMORY_HIERARCHY_HPP