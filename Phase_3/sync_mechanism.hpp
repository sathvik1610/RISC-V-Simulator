// // #ifndef SYNC_MECHANISM_HPP
// // #define SYNC_MECHANISM_HPP
// //
// // #include <vector>
// // #include <atomic>
// // #include <mutex>
// // #include <condition_variable>
// //
// // // SYNC instruction implementation
// // class SyncMechanism {
// // private:
// //     int numCores;
// //     std::vector<bool> syncReached;
// //     std::atomic<int> syncCounter;
// //     std::mutex syncMutex;
// //     std::condition_variable syncCondition;
// //
// // public:
// //     SyncMechanism(int numCores)
// //         : numCores(numCores), syncReached(numCores, false), syncCounter(0) {}
// //
// //     // Called when a core reaches a SYNC instruction
// //     void syncCore(int coreId) {
// //         std::unique_lock<std::mutex> lock(syncMutex);
// //
// //         if (syncReached[coreId]) {
// //             // Already synced
// //             return;
// //         }
// //
// //         // Mark this core as having reached the sync point
// //         syncReached[coreId] = true;
// //         int count = ++syncCounter;
// //
// //         std::cout << "[SYNC] Core " << coreId << " reached barrier. Total so far = " << count << "\n";;
// //         // std::cout << "[SYNC] Core " << coreId << " reached barrier. Total so far = " << syncCounter << "\n";
// //         // Check if all cores have reached the sync point
// //         if (count == numCores) {
// //             std::cout << "[SYNC] All cores reached barrier. Releasing...\n";
// //             syncReached.assign(numCores, false);
// //             syncCounter.store(0);
// //             syncCondition.notify_all();
// //         } else {
// //             std::cout << "[SYNC] Core " << coreId << " waiting at barrier\n";
// //
// //             syncCondition.wait(lock, [this]() {
// //                 return syncCounter.load() == 0;
// //             });
// //             std::cout << "[SYNC] Core " << coreId << " passed barrier.\n";
// //         }
// //     }
// //
// //
// //     // Check if a core can proceed past a SYNC instruction
// //     bool canProceed(int coreId) {
// //         std::lock_guard<std::mutex> lock(syncMutex);
// //         return !syncReached[coreId];
// //     }
// //
// //     // Reset the sync state (useful for simulator reset)
// //     void reset() {
// //         std::lock_guard<std::mutex> lock(syncMutex);
// //         std::fill(syncReached.begin(), syncReached.end(), false);
// //         syncCounter.store(0);
// //     }
// // };
// //
// // #endif // SYNC_MECHANISM_HPP
//
// #ifndef SYNC_MECHANISM_HPP
// #define SYNC_MECHANISM_HPP
//
// #include <vector>
// #include "memory_hierarchy.hpp"
// // A simple, cycle‑accurate barrier for a single‑threaded simulator.
// class SyncMechanism {
//     int numCores;
//     std::vector<bool> arrived;
//     std::vector<bool> retired;
//     int  arriveCount = 0, retireCount = 0;
//
//     MemoryHierarchy* memoryHierarchy;
//
// public:
//     SyncMechanism(int n, MemoryHierarchy* mem)
//       : numCores(n),
//     memoryHierarchy(mem),
//         arrived(n,false),
//         retired(n,false)
//     {}
//
//     // Phase 1: called in EX
//     void arrive(int coreId) {
//         if (!arrived[coreId]) {
//             arrived[coreId] = true;
//             ++arriveCount;
//         }
//     }
//
//     bool allArrived() const {
//         return arriveCount == numCores;
//     }
//
//     // Phase 2: called when retiring the SYNC in MEM/WB
//     void retire(int coreId) {
//         if (!retired[coreId]) {
//             retired[coreId] = true;
//             ++retireCount;
//             std::cout << "[Barrier] core " << coreId
//                               << " retired (count=" << retireCount << ")\n";
//         }
//         // Only clear once *everyone* has retired
//         if (retireCount == numCores) {
//             std::cout << "[Barrier] all cores retired—flushing L1Ds now\n";
//             // for (int c = 0; c < numCores; c++) {
//             //     memoryHierarchy->flushL1D(c);
//             // }
//             for (int c = 0; c < numCores; ++c) {
//                 std::cout << "[Barrier] calling flushL1D(" << c << ")\n";
//                 memoryHierarchy->flushL1D(c);
//             }
//             std::atomic_thread_fence(std::memory_order_seq_cst);
//             std::fill(arrived.begin(),  arrived.end(),  false);
//             std::fill(retired.begin(),  retired.end(),  false);
//             arriveCount = retireCount = 0;
//         }
//     }
//     /// Returns true once every hart has arrived at the barrier.
//     bool canProceed(int /*coreId*/) const {
//         return allArrived();
//
//     }
//
//     void reset() {
//         std::fill(arrived.begin(),  arrived.end(),  false);
//         std::fill(retired.begin(),  retired.end(),  false);
//         arriveCount = retireCount = 0;
//     }
// };
//
//
// #endif // SYNC_MECHANISM_HPP
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