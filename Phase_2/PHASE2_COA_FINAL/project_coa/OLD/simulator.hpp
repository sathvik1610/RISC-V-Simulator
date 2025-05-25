#ifndef SIMULATOR_HPP
#define SIMULATOR_HPP

#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iomanip>
#include "core.hpp"

class RISCVSimulator {
private:
    std::vector<Core> cores;
    std::shared_ptr<SharedMemory> sharedMemory;
    std::vector<std::string> program;
    std::mutex barrierMutex;
    std::condition_variable barrierCV;
    int barrierCount;
    int barrierGeneration;
    std::atomic<bool> running;

    int getInstructionPosition(const std::string& label) const {
        for (size_t i = 0; i < program.size(); i++) {
            if (program[i].find(label + ":") != std::string::npos) {
                return i;
            }
        }
        return -1;
    }

    void barrierWait();

public:
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, (last - first + 1));
    }

    std::string trimAndRemoveComments(const std::string& str) {
        std::string result = str;
        size_t commentPos = result.find('#');
        if (commentPos != std::string::npos) {
            result = result.substr(0, commentPos);
        }
        return trim(result);
    }

    explicit RISCVSimulator(int numCores = 4);
    RISCVSimulator(const RISCVSimulator&) = delete;
    RISCVSimulator& operator=(const RISCVSimulator&) = delete;
    RISCVSimulator(RISCVSimulator&&) = default;
    RISCVSimulator& operator=(RISCVSimulator&&) = default;

    void loadProgramFromFile(const std::string& filename);
    void loadProgram(const std::string& assembly);
    void run();
    void stop();
    void printState() const;
};

#endif
