#ifndef SYNC_BARRIER_HPP
#define SYNC_BARRIER_HPP

#include <atomic>

class SyncBarrier {
private:
    int totalCores;
    std::atomic<int> arrivingCores;
    std::atomic<int> generation;

public:
    SyncBarrier(int numCores) : totalCores(numCores), arrivingCores(0), generation(0) {
        if (numCores <= 0) {
            throw std::invalid_argument("Number of cores must be positive");
        }
    }
    
    // Wait at barrier until all cores arrive
    int sync(int coreId) {
        int myGeneration = generation.load();
        
        if (arrivingCores.fetch_add(1) == totalCores - 1) {
            // Last core to arrive resets the barrier
            arrivingCores.store(0);
            generation.fetch_add(1);  // Move to next generation to prevent re-entry
            return 0;  // No waiting required
        }
        
        // Wait until generation changes, indicating all cores arrived
        int waitCycles = 0;
        while (generation.load() == myGeneration) {
            waitCycles++;
            // In a real implementation, this would be hardware-enforced
        }
        
        return waitCycles;
    }
    
    int getArrivingCores() const {
        return arrivingCores.load();
    }
    
    int getGeneration() const {
        return generation.load();
    }
};

#endif // SYNC_BARRIER_HPP