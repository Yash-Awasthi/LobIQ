#pragma once

// Hardware counters for a code region through perf_event_open, one group read per stop.
// Events the kernel or hypervisor does not expose are reported as missing, never estimated.

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace hft {

class PerfCounters {
   public:
    enum Event { Cycles, Instructions, L1dMisses, LlcMisses, BranchMisses, DtlbMisses, kEvents };
    static constexpr std::array<const char*, kEvents> kNames = {
        "cycles", "instructions", "L1d-misses", "LLC-misses", "branch-misses", "dTLB-misses"};

    PerfCounters() {
        // Hybrid CPUs: a plain hardware event counts only on one core type, so a thread
        // moved to the other type reads zero. Open one counter per core type and sum.
        const std::array<std::uint64_t, kPmus> pmus = {pmu_type("cpu_core"), pmu_type("cpu_atom")};
        for (int e = 0; e < kEvents; ++e) {
            fd_[e].fill(-1);
            if (pmus[0] == 0 && pmus[1] == 0) {
                fd_[e][0] = open(static_cast<Event>(e), 0);
                continue;
            }
            for (int p = 0; p < kPmus; ++p)
                if (pmus[p]) fd_[e][p] = open(static_cast<Event>(e), pmus[p]);
        }
    }
    ~PerfCounters() {
        for (const auto& fds : fd_)
            for (int fd : fds)
                if (fd >= 0) ::close(fd);
    }
    PerfCounters(const PerfCounters&) = delete;
    PerfCounters& operator=(const PerfCounters&) = delete;

    bool available(Event e) const {
        for (int fd : fd_[e])
            if (fd >= 0) return true;
        return false;
    }
    bool any() const {
        for (int e = 0; e < kEvents; ++e)
            if (available(static_cast<Event>(e))) return true;
        return false;
    }

    void start() {
        for (int e = 0; e < kEvents; ++e) begin_[e] = read(e);
    }
    void stop() {
        for (int e = 0; e < kEvents; ++e) total_[e] += read(e) - begin_[e];
    }
    std::uint64_t total(Event e) const { return total_[e]; }

   private:
    static constexpr int kPmus = 2;

    static std::uint64_t pmu_type(const char* name) {
        std::ifstream f(std::string("/sys/bus/event_source/devices/") + name + "/type");
        std::uint64_t t = 0;
        return (f >> t) ? t : 0;
    }
    static int open(Event e, std::uint64_t pmu) {
        perf_event_attr a;
        std::memset(&a, 0, sizeof a);
        a.size = sizeof a;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        a.type = PERF_TYPE_HARDWARE;
        switch (e) {
            case Cycles:
                a.config = PERF_COUNT_HW_CPU_CYCLES;
                break;
            case Instructions:
                a.config = PERF_COUNT_HW_INSTRUCTIONS;
                break;
            case BranchMisses:
                a.config = PERF_COUNT_HW_BRANCH_MISSES;
                break;
            case L1dMisses:
            case LlcMisses:
            case DtlbMisses:
                a.type = PERF_TYPE_HW_CACHE;
                a.config = (e == L1dMisses   ? PERF_COUNT_HW_CACHE_L1D
                            : e == LlcMisses ? PERF_COUNT_HW_CACHE_LL
                                             : PERF_COUNT_HW_CACHE_DTLB) |
                           (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                           (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
                break;
            default:
                return -1;
        }
        a.config |= pmu << 32;
        return static_cast<int>(::syscall(SYS_perf_event_open, &a, 0, -1, -1, 0));
    }
    std::uint64_t read(int e) const {
        std::uint64_t sum = 0;
        for (int fd : fd_[e]) {
            std::uint64_t v = 0;
            if (fd >= 0 && ::read(fd, &v, sizeof v) == sizeof v) sum += v;
        }
        return sum;
    }

    std::array<std::array<int, kPmus>, kEvents> fd_{};
    std::array<std::uint64_t, kEvents> begin_{}, total_{};
};

}  // namespace hft
