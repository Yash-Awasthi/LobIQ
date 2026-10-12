# Roadmap

The system has two jobs: replay Nasdaq ITCH days as fast as the hardware allows, and run a
live paper-trading pipeline against a prediction-market exchange. Every change is measured
before and after, and every phase ends with CI green on all presets.

Target machine: i7-13650HX (6 P-cores, 8 E-cores, AVX2, no AVX-512) on bare-metal Arch Linux,
with hardware performance counters and isolated cores (docs/exec/HW.md). Finished phases are
in [CHANGELOG.md](../CHANGELOG.md) and the git history. Nothing is open.

## Not planned

- Live order entry: never (docs/exec DECISIONS D1). The code has no order entry and a CI gate
  keeps it that way.
- Miss-probability research for the arbitrage executor: would need a model estimated on some
  days and tested on later ones; a research step, outside this engineering project (D12).
- Maker rebates (VENUE F18: 15-25% of fees by category, paid daily): the simulated venue leaves
  them out, so maker PnL is understated.
- Cancel-only and closed-only market modes (VENUE F12): never seen in the recordings, so there is
  nothing to test a model against.
- System zstd, LZ4, zlib-ng and OpenSSL instead of vcpkg: the vcpkg build works in CI.
- Bloomberg, FRED, Tiingo, Quiver: daily research data with no use in this pipeline.
