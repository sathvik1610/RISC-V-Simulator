#ifndef SCRATCHPAD_MEMORY_HPP
#define SCRATCHPAD_MEMORY_HPP

#include <vector>
#include <mutex>
#include <stdexcept>
#include <string>

class ScratchpadMemory {
public:


    explicit ScratchpadMemory(size_t size_bytes, int access_latency)
      : capacity(size_bytes),
        latency(access_latency),
        storage(size_bytes)
    { }


    // void storeWord(int address, int value) {
    //     if (address % 4 != 0) {
    //         throw std::runtime_error("Unaligned SPM access at address " + std::to_string(address));
    //     }
    //
    //     int wordIndex = address / 4;
    //     std::lock_guard<std::mutex> lock(spmMutex);
    //
    //     if (wordIndex < 0 || wordIndex >= (int)memory.size()) {
    //         throw std::runtime_error("SPM address out of bounds: " + std::to_string(address));
    //     }
    //
    //     memory[wordIndex] = value;
    // }

    // int loadWord(int address) const {
    //     if (address % 4 != 0) {
    //         throw std::runtime_error("Unaligned SPM access at address " + std::to_string(address));
    //     }
    //
    //     int wordIndex = address / 4;
    //     std::lock_guard<std::mutex> lock(spmMutex);
    //
    //     if (wordIndex < 0 || wordIndex >= (int)memory.size()) {
    //         throw std::runtime_error("SPM address out of bounds: " + std::to_string(address));
    //     }
    //
    //     return memory[wordIndex];
    // }
    uint32_t loadWord(size_t addr) {
        checkBounds(addr);
        uint32_t val = 0;
        std::memcpy(&val, &storage[addr], sizeof(val));
        return val;
    }
    void storeWord(size_t addr, uint32_t word) {
        checkBounds(addr);
        std::memcpy(&storage[addr], &word, sizeof(word));
    }
    int  getLatency()    const { return latency; }
      size_t getCapacity() const { return capacity; }

private:
    void checkBounds(size_t addr) const {
        if (addr + sizeof(uint32_t) > capacity) {
            throw std::out_of_range("SPM access out of bounds");
        }
    }
    std::vector<uint8_t> storage;
    size_t   capacity;
    mutable std::mutex spmMutex;
    int   latency;
};

#endif // SCRATCHPAD_MEMORY_HPP