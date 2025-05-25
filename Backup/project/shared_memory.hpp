#ifndef SHARED_MEMORY_HPP
#define SHARED_MEMORY_HPP

#include <vector>
#include <stdexcept>
#include <mutex>

class SharedMemory
{
public:
    static const int TOTAL_MEMORY_SIZE = 1024 * 4; // 1MB of memory

private:
    std::vector<int> memory;
    std::mutex accessMutex;

    int accessCount;
    int storeCount;

public:
    SharedMemory() : memory(TOTAL_MEMORY_SIZE / 4, 0), accessCount(0), storeCount(0)
    {
        // Initialize memory to all zeros
    }

    int loadWord(int coreId, int address)
    {
        if (address < 0 || address >= TOTAL_MEMORY_SIZE || address % 4 != 0)
        {
            throw std::out_of_range("Memory address out of range or not word-aligned");
        }

        std::lock_guard<std::mutex> lock(accessMutex);
        accessCount++;
        return memory[address / 4];
    }

    void storeWord(int coreId, int address, int value)
    {
        if (address < 0 || address >= TOTAL_MEMORY_SIZE || address % 4 != 0)
        {
            throw std::out_of_range("Memory address out of range or not word-aligned");
        }

        std::lock_guard<std::mutex> lock(accessMutex);
        storeCount++;
        memory[address / 4] = value;
    }

    // Set a word directly (used for memory initialization)
    void setWord(int address, int value)
    {
        if (address < 0 || address >= TOTAL_MEMORY_SIZE || address % 4 != 0)
        {
            throw std::out_of_range("Memory address out of range or not word-aligned");
        }

        memory[address / 4] = value;
    }

    int getAccessCount() const { return accessCount; }
    int getStoreCount() const { return storeCount; }

    void resetStats()
    {
        accessCount = 0;
        storeCount = 0;
    }
};

#endif // SHARED_MEMORY_HPP