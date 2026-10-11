# Bare-metal Linux setup (Arch)

## 1. Packages

    sudo pacman -Syu base-devel git cmake ninja gcc clang lld ccache zip unzip curl zstd \
        pkgconf python perf cpupower github-cli verilator linux-headers

## 2. Code and dependencies

    gh auth login
    git clone https://github.com/Yash-Awasthi/hft ~/hft
    git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
    echo 'export VCPKG_ROOT=~/vcpkg' >> ~/.bashrc && source ~/.bashrc
    cd ~/hft
    cmake --preset native && cmake --build build/native && ctest --test-dir build/native
    cmake -S fpga -B build/fpga && cmake --build build/fpga && build/fpga/itch_rtl_test

Nasdaq days used by the benchmarks (public, about 5-10 GB each):

    cmake --preset release && cmake --build build/release
    for d in S112825 S120825 S120925 S121025 S121125; do scripts/ingest_day.sh $d-v50.txt.gz; done

## 3. Pick the cores

The i7-13650HX has 6 P-cores (2 threads each) and 8 E-cores. Find them:

    lscpu -e=CPU,CORE,MAXMHZ        # P-core threads have the higher MAXMHZ
    cat /sys/devices/system/cpu/cpu4/topology/thread_siblings_list

Isolate whole P-cores (both threads of each), e.g. CPUs 4-7 if the siblings are 4,5 and 6,7.
Check the CPU numbers on your machine first.

## 4. Kernel command line

Check the kernel supports full tickless mode:

    zcat /proc/config.gz | grep -E 'NO_HZ_FULL|CPU_ISOLATION'

Add to the boot options (GRUB: `GRUB_CMDLINE_LINUX_DEFAULT` in `/etc/default/grub`, then
`sudo grub-mkconfig -o /boot/grub/grub.cfg`; systemd-boot: the `options` line in
`/boot/loader/entries/*.conf`):

    isolcpus=nohz,domain,managed_irq,4-7 nohz_full=4-7 rcu_nocbs=4-7 irqaffinity=0-3,8-19

Reboot, then check: `cat /sys/devices/system/cpu/isolated` prints `4-7`. Do not install irqbalance.

## 5. Before each benchmark session (resets at reboot)

    sudo cpupower frequency-set -g performance
    echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo    # stable clock; 0 for speed
    echo madvise | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
    sudo sysctl kernel.perf_event_paranoid=1                           # perf stat counters

Laptop on AC power.

## 6. Check and measure

    build/native/apps/pm_live --replay data/sample/pm-20261009T1600-20min --cpu 4

Decision hash must be `d97079e6c1474af9`. Under WSL2 the engine p99 was 563-950 ns and the
book replay max 1-8 ms (docs/exec/HW.md H13-H14); compare against those, then update HW.md
for the new machine (H1-H16).

    perf stat -e cycles,instructions,cache-misses,dTLB-load-misses taskset -c 4 \
        build/native/bench/book_study ~/data/store-S120825-v50 book:tick 5 3 1
