# Machine (measurement conditions)

Checked 2026-10-10. Every benchmark result cites this file; re-check before quoting numbers.

- H1 Laptop Lenovo 83JJ; CPU i7-13650HX: 14 cores (6 P + 8 E), 20 threads (Windows WMI). Base 2.6 GHz.
- H2 Turbo is off: Windows power plan "Balanced", maximum processor state AC 99% (0x63), which disables boost; DC 100%. Measured core clock 2.39-2.44 GHz on vCPUs 2, 4, 12, 16 (dependent-add chain, /tmp/ghz.cpp). On AC power at check time.
- H3 TSC 2.803 GHz (constant_tsc, nonstop_tsc, tsc_known_freq). rdtsc ticks are not core cycles: cycles = ns * 2.4, not ticks.
- H4 ISA: AVX2, FMA, BMI2, ADX, MOVBE, AVX-VNNI, GFNI, VAES, VPCLMULQDQ, SHA-NI, ERMS, FSRM. No AVX-512.
- H5 Caches (Windows): L2 11.5 MiB total, L3 24 MiB. WSL lscpu shows a synthetic topology (10 cores x 2 threads, L2 10 x 1.25 MiB): do not use it.
- H6 RAM 2 x 12 GB DDR5-4800 (24 GB); WSL capped at 18 GB (.wslconfig memory=18GB).
- H7 WSL2 kernel 6.18.40.1-microsoft-standard-WSL2. vCPU -> physical core mapping is decided by Hyper-V and can move; `taskset`/`--cpu` pins a vCPU, not a P-core. P/E placement of a benchmark is unknown.
- H8 No hardware PMU in WSL: `.wslconfig hardwarePerformanceCounters=true` is set but /sys/devices/cpu is absent; perf_event_paranoid 2. Profile with `perf -e cpu-clock` only.
- H9 THP: madvise (Pool uses MADV_HUGEPAGE). Clock: chrony stratum 1 from PHC0.
- H10 Storage: C: 283.7 GB free; WSL disk file ext4.vhdx 120.3 GB (grows, does not shrink by itself); WSL `df` shows the 1 TB virtual maximum, not real free space.
- H11 Background load during work: pm_record and pm_live run continuously from ~/data/bin (pinned copies). Benchmarks share the machine with them; compare only back-to-back runs.
- H12 Network: request RTT to the exchange 182 ms median (VENUE F28), via Cloudflare BOM.

## Low-latency host tuning (checked 2026-10-11)

- H13 Huge pages matter and are on. `book_study book:tick` vs `book:tick-4k` (S120825-v50, 5 symbols, 44.6M events, vCPU 2): p99 244-261 ns vs 295-296 ns, total 2.44-2.46 s vs 2.63-2.67 s.
- H14 `isolcpus=2-3 irqaffinity=0-1,4-19` (via `.wslconfig kernelCommandLine`) changed nothing measurable: `pm_live --replay` sample engine p99 563-950 ns vs 595-876 ns before, and book max latency stayed 1-8 ms. The stalls come from Hyper-V descheduling the vCPU, which the guest cannot isolate. Reverted. `nohz_full` is not built into the WSL kernel.
- H15 Kernel bypass is not applicable here. The only NIC is the Hyper-V `hv_netvsc` device, and DPDK's netvsc PMD would take it away from WSL. ef_vi needs a Solarflare/AMD NIC. The venue is a TLS WebSocket at 182 ms RTT (H12), so the 5-10 µs kernel stack is under 0.01% of it.
- H16 To go further: bare-metal Linux with `isolcpus`/`nohz_full`/`rcu_nocbs`, then a kernel-bypass NIC for a colocated venue. Without those, these knobs cannot be measured. The FPGA path exists in simulation only (`fpga/itch_parser.sv`); there is no board here.
