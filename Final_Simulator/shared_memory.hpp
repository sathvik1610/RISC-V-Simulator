#ifndef SHARED_MEMORY_HPP
#define SHARED_MEMORY_HPP

#include <vector>
#include <mutex>
#include <stdexcept>
#include <string>

class SharedMemory {
public:
    static constexpr int TOTAL_MEMORY_SIZE = 4096;  // 4KB total memory

    SharedMemory() : memory(TOTAL_MEMORY_SIZE / 4, 0) {}

    void setWord(int address, int value) {
        if (address % 4 != 0) {
            throw std::runtime_error("Unaligned memory access at address " + std::to_string(address));
        }

        int wordIndex = address / 4;
        std::lock_guard<std::mutex> lock(memoryMutex);

        if (wordIndex < 0 || wordIndex >= (int)memory.size()) {
            throw std::runtime_error("Address out of bounds: " + std::to_string(address));
        }

        memory[wordIndex] = value;
    }

    int loadWord(int coreId, int address) const {
        if (address % 4 != 0) {
            throw std::runtime_error("Unaligned memory access at address " + std::to_string(address));
        }

        int wordIndex = address / 4;
        std::lock_guard<std::mutex> lock(memoryMutex);

        if (wordIndex < 0 || wordIndex >= (int)memory.size()) {
            throw std::runtime_error("Address out of bounds: " + std::to_string(address));
        }

        return memory[wordIndex];
    }

    void storeWord(int coreId, int address, int value) {
        if (address % 4 != 0) {
            throw std::runtime_error("Unaligned memory access at address " + std::to_string(address));
        }

        int wordIndex = address / 4;
        std::lock_guard<std::mutex> lock(memoryMutex);

        if (wordIndex < 0 || wordIndex >= (int)memory.size()) {
            throw std::runtime_error("Address out of bounds: " + std::to_string(address));
        }

        memory[wordIndex] = value;
    }

    std::vector<int> getMemorySegment(int coreId) const {
        // Since all cores can access all memory, we'll return the full memory
        // This maintains backward compatibility with code that uses this method
        return getFullMemory();
    }
    
    const std::vector<int>& getFullMemory() const {
        std::lock_guard<std::mutex> lock(memoryMutex);
        return memory;
    }

private:
    std::vector<int> memory;
    mutable std::mutex memoryMutex;
};

#endif // SHARED_MEMORY_HPP