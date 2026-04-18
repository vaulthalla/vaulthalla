#pragma once

#include <vector>
#include <utility>

namespace vh::concurrency {

using opRange = std::pair<unsigned int, unsigned int>;

inline std::vector<opRange> getTaskOperationRanges(const unsigned int totalOperations,
                                                   const unsigned int maxThreads = std::thread::hardware_concurrency(),
                                                   const unsigned int minOperationsPerTask = 2) {
    std::vector<opRange> taskOperationRanges;
    taskOperationRanges.reserve(totalOperations);

    const unsigned int numThreads = std::min(totalOperations / minOperationsPerTask, maxThreads);
    const unsigned int operationsPerTask = totalOperations / numThreads;
    const unsigned int remainder = totalOperations % numThreads;

    for (unsigned int i = 0; i < numThreads; ++i) {
        unsigned int start = i * operationsPerTask;
        unsigned int end = start + operationsPerTask;
        if (i < remainder) end++;
        if (start < totalOperations) taskOperationRanges.emplace_back(start, end);
    }

    return taskOperationRanges;
}

}
