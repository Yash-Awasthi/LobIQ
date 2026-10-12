#!/usr/bin/env bash
# Benchmark-session tuning from docs/BARE_METAL.md step 5; resets at reboot. Run as root (sudo).
# Turbo stays off for stable before/after comparisons; NO_TURBO=0 for peak numbers.
# Prints the settings afterwards so a report can quote them.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 2; }
cpupower frequency-set -g performance >/dev/null
echo "${NO_TURBO:-1}" > /sys/devices/system/cpu/intel_pstate/no_turbo  # NO_TURBO=0: peak clocks
echo madvise > /sys/kernel/mm/transparent_hugepage/enabled
sysctl -q kernel.perf_event_paranoid=1
modprobe intel_uncore_frequency
u=/sys/devices/system/cpu/intel_uncore_frequency/package_00_die_00
cat "$u/max_freq_khz" > "$u/min_freq_khz"
# Drivers pin per-queue IRQs one per CPU (iwlwifi queues 5-8 land on CPUs 4-7) regardless of
# irqaffinity=; move every movable IRQ to the boot-time housekeeping mask. Managed ones refuse.
hk=$(cat /proc/irq/default_smp_affinity)
moved=0
for d in /proc/irq/[0-9]*; do
    echo "$hk" > "$d/smp_affinity" 2>/dev/null && moved=$((moved + 1))
done
echo "governor $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)," \
     "no_turbo $(cat /sys/devices/system/cpu/intel_pstate/no_turbo)," \
     "thp $(grep -o '\[[a-z]*\]' /sys/kernel/mm/transparent_hugepage/enabled)," \
     "uncore min $(cat "$u/min_freq_khz") kHz," \
     "isolated $(cat /sys/devices/system/cpu/isolated), irqs moved $moved," \
     "ac $(cat /sys/class/power_supply/A*/online 2>/dev/null | head -1)"
