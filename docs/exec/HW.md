# Machine (measurement conditions)

Every benchmark result cites this file; re-check before quoting numbers. Results measured
before 2026-10-12 ran under WSL2 and cite the W entries below.

## Bare-metal Arch Linux (checked 2026-10-12)

- H1 Laptop Lenovo 83JJ; CPU i7-13650HX: 14 cores (6 P + 8 E), 20 threads. P-cores are CPUs 0-11 (siblings 0-1, 2-3, ..., 10-11), E-cores 12-19. CPUs 4-7 (cores 2-3) are the favoured P-cores (4.9 GHz max, the others 4.7).
- H2 Benchmarks run with turbo off (`intel_pstate/no_turbo=1`), governor `performance`, EPP `performance`, platform profile `performance`, on AC power. Measured clock (dependent-add chain, pinned): P-core 2.59 GHz, E-core 1.89 GHz. Turbo on: CPU 4 4.88 GHz, CPU 0 4.68 GHz, CPU 12 3.59 GHz.
- H3 TSC 2.803 GHz (constant_tsc, nonstop_tsc, tsc_known_freq), clocksource tsc. rdtsc ticks are not core cycles: cycles = ns * 2.59 on a P-core with turbo off.
- H4 ISA: AVX2, FMA, BMI2, ADX, MOVBE, AVX-VNNI, GFNI, VAES, VPCLMULQDQ, SHA-NI, ERMS, FSRM. No AVX-512.
- H5 Caches: L1d 48 KiB per P-core, L2 11.5 MiB total (8 instances), L3 24 MiB.
- H6 RAM 2 x 12 GiB DDR5-4800 (24 GiB).
- H7 Kernel 7.2.9-arch1-1, built with NO_HZ_FULL, CPU_ISOLATION and RCU_NOCB_CPU. `taskset`/`--cpu` pins a real core.
- H8 Hardware PMU present as two PMUs, `cpu_core` (CPUs 0-11) and `cpu_atom` (12-19). Generic `perf -e cycles` counts on one of them only; name the PMU (`cpu_core/cycles/`). `PerfCounters` opens both and sums. perf_event_paranoid 1 per session (default 2).
- H9 THP set to madvise per session (Arch default is always; Pool uses MADV_HUGEPAGE). Clock: systemd-timesyncd, NTP synchronized.
- H10 Storage: root ext4 on the internal NVMe, 181 GB free after the five ITCH stores (45 GB). Windows C: is mounted at /mnt/win (ntfs3) and keeps the original ITCH downloads.
- H11 Background load: none. No recorders or services run; benchmarks start after ingest finishes.
- H12 Network: Wi-Fi (iwlwifi); Ethernet r8169 unplugged. To clob.polymarket.com (Cloudflare MAA): TCP connect median 32 ms, p90 39 ms; request round trip after TLS median 219 ms, p90 470 ms, min 179 ms (n=21, `curl -w`). D4 still uses 182 ms.

### Host tuning

- H13 Huge pages still matter. `book_study book:tick` vs `book:tick-4k` (S120825-v50, 5 symbols, 44.6M events, CPU 4, two runs of 3 reps each): p99 156-159 ns vs 176-180 ns, total 2.03-2.04 s vs 2.15-2.16 s, max 0.33-0.37 ms vs 0.57-0.59 ms. dTLB load misses over the whole run 48.0M vs 126.5M. IPC 1.34 (`perf stat`, cpu_core PMU).
- H14 Without core isolation (current boot), CPU 4: `pm_live --replay` sample engine p50/p99/p99.9 174-177/476-829/1297-1752 ns, decision hash d97079e6c1474af9. Book max latency fell from 1-8 ms under WSL2 (W14) to 0.33-0.37 ms. The boot line now has `isolcpus=nohz,domain,managed_irq,4-7 nohz_full=4-7 rcu_nocbs=4-7 irqaffinity=0-3,8-19`; its effect is measured after the next reboot.
- H15 Kernel bypass still does not apply: the NICs are Wi-Fi and a Realtek r8169 port, neither supported by ef_vi, and the venue is a TLS WebSocket at about 200 ms (H12).
- H16 Next: measure H14 again with CPUs 4-7 isolated, then a kernel-bypass NIC only for a colocated venue. The FPGA path is simulation only (`fpga/itch_parser.sv`).

## WSL2 on Windows (until 2026-10-11)

- W1 Laptop Lenovo 83JJ; CPU i7-13650HX: 14 cores (6 P + 8 E), 20 threads (Windows WMI). Base 2.6 GHz.
- W2 Turbo is off: Windows power plan "Balanced", maximum processor state AC 99% (0x63), which disables boost; DC 100%. Measured core clock 2.39-2.44 GHz on vCPUs 2, 4, 12, 16 (dependent-add chain, /tmp/ghz.cpp). On AC power at check time.
- W3 TSC 2.803 GHz (constant_tsc, nonstop_tsc, tsc_known_freq). rdtsc ticks are not core cycles: cycles = ns * 2.4, not ticks.
- W4 ISA: AVX2, FMA, BMI2, ADX, MOVBE, AVX-VNNI, GFNI, VAES, VPCLMULQDQ, SHA-NI, ERMS, FSRM. No AVX-512.
- W5 Caches (Windows): L2 11.5 MiB total, L3 24 MiB. WSL lscpu shows a synthetic topology (10 cores x 2 threads, L2 10 x 1.25 MiB): do not use it.
- W6 RAM 2 x 12 GB DDR5-4800 (24 GB); WSL capped at 18 GB (.wslconfig memory=18GB).
- W7 WSL2 kernel 6.18.40.1-microsoft-standard-WSL2. vCPU -> physical core mapping is decided by Hyper-V and can move; `taskset`/`--cpu` pins a vCPU, not a P-core. P/E placement of a benchmark is unknown.
- W8 No hardware PMU in WSL: `.wslconfig hardwarePerformanceCounters=true` is set but /sys/devices/cpu is absent; perf_event_paranoid 2. Profile with `perf -e cpu-clock` only.
- W9 THP: madvise (Pool uses MADV_HUGEPAGE). Clock: chrony stratum 1 from PHC0.
- W10 Storage: C: 283.7 GB free; WSL disk file ext4.vhdx 120.3 GB (grows, does not shrink by itself); WSL `df` shows the 1 TB virtual maximum, not real free space.
- W11 Background load during work: pm_record and pm_live run continuously from ~/data/bin (pinned copies). Benchmarks share the machine with them; compare only back-to-back runs.
- W12 Network: request RTT to the exchange 182 ms median (VENUE F28), via Cloudflare BOM.

### WSL2 host tuning (checked 2026-10-11)

- W13 Huge pages matter and are on. `book_study book:tick` vs `book:tick-4k` (S120825-v50, 5 symbols, 44.6M events, vCPU 2): p99 244-261 ns vs 295-296 ns, total 2.44-2.46 s vs 2.63-2.67 s.
- W14 `isolcpus=2-3 irqaffinity=0-1,4-19` (via `.wslconfig kernelCommandLine`) changed nothing measurable: `pm_live --replay` sample engine p99 563-950 ns vs 595-876 ns before, and book max latency stayed 1-8 ms. The stalls come from Hyper-V descheduling the vCPU, which the guest cannot isolate. Reverted. `nohz_full` is not built into the WSL kernel.
- W15 Kernel bypass is not applicable here. The only NIC is the Hyper-V `hv_netvsc` device, and DPDK's netvsc PMD would take it away from WSL. ef_vi needs a Solarflare/AMD NIC. The venue is a TLS WebSocket at 182 ms RTT (W12), so the 5-10 µs kernel stack is under 0.01% of it.
- W16 To go further: bare-metal Linux with `isolcpus`/`nohz_full`/`rcu_nocbs`, then a kernel-bypass NIC for a colocated venue. Without those, these knobs cannot be measured. The FPGA path exists in simulation only (`fpga/itch_parser.sv`); there is no board here. Bare-metal setup: [BARE_METAL.md](../BARE_METAL.md).
