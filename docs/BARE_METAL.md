# Bare-metal Linux setup (Arch)

## 1. Packages

    sudo pacman -Syu base-devel git cmake ninja gcc clang lld ccache zip unzip curl aria2 zstd \
        pkgconf python perf cpupower github-cli verilator linux-headers

## 2. Code and dependencies

    gh auth login
    git clone https://github.com/Yash-Awasthi/lobiq ~/lobiq
    git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
    echo 'export VCPKG_ROOT=~/vcpkg' >> ~/.bashrc && source ~/.bashrc
    cd ~/lobiq
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

With the BIOS GPU mode set to discrete only, the panel is driven by the Nvidia GPU and
`acpi_video0` does not change brightness. Add `acpi_backlight=nvidia_wmi_ec` to the same line.

## 5. Before each benchmark session (resets at reboot)

`sudo scripts/tune.sh` applies all of the following, moves every movable IRQ off the isolated
cores (docs/exec/HW.md H19) and prints the result:

    sudo cpupower frequency-set -g performance
    echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo    # stable clock; 0 for speed
    echo madvise | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
    sudo sysctl kernel.perf_event_paranoid=1                           # perf stat counters
    sudo modprobe intel_uncore_frequency                               # L3 and memory path at full clock
    echo 4300000 | sudo tee /sys/devices/system/cpu/intel_uncore_frequency/package_00_die_00/min_freq_khz

Only for a thread that sleeps on the doorbell instead of spinning, keep its core out of deep
idle (here CPU 6; never the SMT sibling of a busy core, docs/exec/HW.md H17):

    for s in 1 2 3; do echo 1 | sudo tee /sys/devices/system/cpu/cpu6/cpuidle/state$s/disable; done

Hybrid CPU: plain `perf -e cycles` counts only on one core type; use `cpu_core/cycles/` for
P-cores and `cpu_atom/cycles/` for E-cores.

Laptop on AC power.

## 6. Check and measure

For `pm_live` runs use the profile-guided build (parse 9% faster, docs/exec/PERF.md R12):

    scripts/pgo.sh ~/data/store-S112825-v50 ~/data/ref/pm-snap


    build/native/apps/pm_live --replay data/sample/pm-20261009T1600-20min --cpu 4

Decision hash must be `d97079e6c1474af9`. Under WSL2 the engine p99 was 563-950 ns and the
book replay max 1-8 ms (docs/exec/HW.md W13-W14); compare against those, then update HW.md
for the new machine (H1-H16).

    perf stat -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/cache-misses/,cpu_core/dTLB-load-misses/ taskset -c 4 \
        build/native/bench/book_study ~/data/store-S120825-v50 book:tick 5 3 1

## 7. Last session before removing the install

Everything that needs this machine, in one sitting. Nothing here changes the published latency
figures unless `--bench` is given: the changes since v1.5 add reporting, a strategy, a tool and the
studio, and CI's perf gate checks every change for regressions on the hot paths.

1. Update the clone (an existing `~/hft` clone works; the repository moved to `lobiq`):

       cd ~/hft
       git remote set-url origin https://github.com/Yash-Awasthi/lobiq.git
       git checkout main && git pull

2. Run the session (about 20 minutes without `--bench`, plus about an hour with it):

       scripts/arch_session.sh            # or: scripts/arch_session.sh --bench

   It builds the native preset, runs the tests, the CLI and security gates, the demos, maker parity
   on every live recording, `checkpoint --verify` on every store, then on real days: `naive`,
   `imbalance` and `microprice` per symbol, a walk-forward of the micro-price maker (parameters
   chosen on `TRAIN`, scored on `TEST`), and the Markov chain's out-of-sample hit rate per symbol.
   Results go to `docs/results/session-<date>.md`; the logs' directory is printed at the end.
   `SYMBOLS`, `TRAIN` and `TEST` override the defaults (`SPY,...,TSLA`, `S120825`, `S121025`).

3. Optional, with the studio on real data: `hft studio`, then in the browser the Prediction tab on a
   few symbols, the leaderboard's portfolio frontier on the session's runs, and a short live run
   from the Live paper tab (`hft live 15` does the same from the shell). Screenshots of these can
   replace the ones in `docs/img/`.

4. Commit the results:

       git checkout -b bare-metal-session
       git add docs/results && git commit -m "Record the bare-metal session results"
       git push -u origin bare-metal-session && gh pr create --fill

5. Back up what cannot be fetched again. The end of the results page lists the sizes. Live
   recordings (`~/data/pm-live`, `~/data/live`, `~/data/pm`) are unique; Nasdaq days can be fetched
   again with `hft get` for as long as Nasdaq publishes them. To an external disk, for example:

       rsync -a --info=progress2 ~/data/pm-live ~/data/live ~/data/pm /run/media/$USER/<disk>/lobiq-data/

   After the install is gone, the container image runs every demo and the studio on any machine
   with Docker or Podman, and GitHub Codespaces builds everything in the browser.
