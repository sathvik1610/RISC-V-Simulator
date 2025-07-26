    #ifndef SYNC_MECHANISM_HPP
    #define SYNC_MECHANISM_HPP

    #include <vector>
    #include <iostream>
    #include "memory_hierarchy.hpp"

    // A cycle-accurate barrier implementation for a multi-core simulator with cache coherence support
    class SyncMechanism {
    private:
        int numCores;
        std::vector<bool> arrived;
        std::vector<bool> retired;
        int arriveCount = 0, retireCount = 0;

        MemoryHierarchy* memoryHierarchy;

    public:
        SyncMechanism(int n, MemoryHierarchy* mem)
          : numCores(n),
            memoryHierarchy(mem),
            arrived(n, false),
            retired(n, false)
        {}

        // Phase 1: Called in EX stage when a core reaches the SYNC
        void arrive(int coreId) {
            if (!arrived[coreId]) {
                std::cout << "[Core " << coreId << "] Arrived at SYNC\n";
                arrived[coreId] = true;
                ++arriveCount;
            }
        }

        // Check if all cores have arrived at the barrier
        bool allArrived() const {
            return arriveCount == numCores;
        }

        // Determine if the core can proceed past the barrier
        bool canProceed(int coreId) const {
            // A core can proceed if all cores have arrived at the barrier
            return allArrived();
        }

        // Phase 2: Called when retiring the SYNC in MEM/WB stage
        void retire(int coreId) {
            if (!retired[coreId]) {
                retired[coreId] = true;
                ++retireCount;
                std::cout << "[Barrier] core " << coreId
                         << " retired (count=" << retireCount << ")\n";
            }

            // Only clear the barrier state when all cores have retired
            if (retireCount == numCores) {
                std::cout << "[Barrier] all cores retired—flushing L1Ds now\n";

                // Flush all L1 data caches to ensure memory coherence
                for (int c = 0; c < numCores; ++c) {
                    std::cout << "[Barrier] calling flushL1D(" << c << ")\n";
                    // Ensure all dirty cache lines are written back to memory
                    memoryHierarchy->flushL1D(c);
                }

                // Memory fence to ensure all memory operations complete
                std::atomic_thread_fence(std::memory_order_seq_cst);

                // Reset barrier state for next use
                std::fill(arrived.begin(), arrived.end(), false);
                std::fill(retired.begin(), retired.end(), false);
                arriveCount = retireCount = 0;

                std::cout << "[Barrier] Barrier reset complete\n";
            }
        }

        // Reset the barrier (for simulation reset)
        void reset() {
            std::fill(arrived.begin(), arrived.end(), false);
            std::fill(retired.begin(), retired.end(), false);
            arriveCount = retireCount = 0;
        }
    };

    #endif