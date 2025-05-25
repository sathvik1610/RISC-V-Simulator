#ifndef SHARED_MEMORY_HPP
#define SHARED_MEMORY_HPP

#include <vector>
#include <mutex>
#include <stdexcept>
#include <string>

class SharedMemory {
private:

    std::vector<int> memory;
    mutable std::mutex memoryMutex;

public:
    static constexpr int TOTAL_MEMORY_SIZE = 4096;
    static constexpr int SEGMENT_SIZE = 1024;
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
        int segment = address / SEGMENT_SIZE;
        int relativeAddr = address % SEGMENT_SIZE;
        if (segment != coreId) {
            throw std::runtime_error("Core " + std::to_string(coreId) +
                                       " cannot access memory segment " + std::to_string(segment) +
                                       ". Use an address in the range " +
                                       std::to_string(coreId * SEGMENT_SIZE) + " to " +
                                       std::to_string((coreId + 1) * SEGMENT_SIZE - 4) + ".");
        }
        int wordIndex = relativeAddr / 4;
        int baseIndex = coreId * (SEGMENT_SIZE / 4);
        std::lock_guard<std::mutex> lock(memoryMutex);
        int value = memory[baseIndex + wordIndex];
        return value;
    }

    void storeWord(int coreId, int address, int value) {
        if (address % 4 != 0) {
            throw std::runtime_error("Unaligned memory access at address " + std::to_string(address));
        }
        int segment = address / SEGMENT_SIZE;
        int relativeAddr = address % SEGMENT_SIZE;
        if (segment != coreId) {
            throw std::runtime_error("Core " + std::to_string(coreId) +
                                       " cannot access memory segment " + std::to_string(segment) +
                                       ". Use an address in the range " +
                                       std::to_string(coreId * SEGMENT_SIZE) + " to " +
                                       std::to_string((coreId + 1) * SEGMENT_SIZE - 4) + ".");
        }
        int wordIndex = relativeAddr / 4;
        int baseIndex = coreId * (SEGMENT_SIZE / 4);
        std::lock_guard<std::mutex> lock(memoryMutex);
        memory[baseIndex + wordIndex] = value;
    }

    std::vector<int> getMemorySegment(int coreId) const {
        if (coreId < 0 || coreId >= (TOTAL_MEMORY_SIZE / SEGMENT_SIZE)) {
            throw std::runtime_error("Invalid core ID: " + std::to_string(coreId));
        }
        std::lock_guard<std::mutex> lock(memoryMutex);
        int baseAddress = coreId * (SEGMENT_SIZE / 4);
        return std::vector<int>(
            memory.begin() + baseAddress,
            memory.begin() + baseAddress + (SEGMENT_SIZE / 4)
        );
    }

    const std::vector<int>& getFullMemory() const {
        std::lock_guard<std::mutex> lock(memoryMutex);
        return memory;
    }
};

#endif
