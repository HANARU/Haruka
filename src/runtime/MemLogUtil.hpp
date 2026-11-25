#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#include <unistd.h>
#endif

namespace hmemlog {

inline double queryProcessRssMb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    return 0.0;
#else
    std::ifstream f("/proc/self/statm");
    long totalPages = 0, residentPages = 0;
    if (f >> totalPages >> residentPages) {
        const long pageSz = sysconf(_SC_PAGESIZE);
        if (pageSz > 0 && residentPages > 0)
            return static_cast<double>(residentPages) * static_cast<double>(pageSz) / (1024.0 * 1024.0);
    }
    return 0.0;
#endif
}

inline std::string formatMemLine(uint32_t chunks, uint64_t committedBytes, double rssMb) {
    const uint64_t committedMb = committedBytes / (1024ULL * 1024ULL);
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "Mem: core heap chunks %u (%llu MB committed), process RSS %.1f MB",
                  chunks, static_cast<unsigned long long>(committedMb), rssMb);
    return std::string(buf);
}

} // namespace hmemlog
