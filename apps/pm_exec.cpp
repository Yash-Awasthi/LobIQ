// Paper execution: makers and the arbitrage executor through risk, order manager and simulated venue;
// no order leaves the machine. A replay without --config reads DIR/exec.cfg and overrides.cfg, so it runs as recorded.

#include <atomic>
#include <mutex>
#include <ranges>
#include <thread>

#include "pm_app.hpp"
#include "core/parallel.hpp"

namespace {

void on_kill_signal(int) { g_kill = true; }

std::string read_file(const fs::path& p) {
    std::ifstream f(p);
    if (!f) throw std::runtime_error("cannot read " + p.string());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file(const fs::path& p, const std::string& text) {
    std::ofstream f(p);
    if (!(f << text)) throw std::runtime_error("cannot write " + p.string());
}

// Next session number from EXEC_DIR/session.counter; never reused (T6).
std::uint16_t next_session(const fs::path& dir) {
    fs::create_directories(dir);
    const fs::path p = dir / "session.counter";
    long n = 0;
    if (fs::exists(p)) n = std::atol(read_file(p).c_str());
    if (n < 0 || n >= 65'535) throw std::runtime_error(p.string() + ": session numbers used up or corrupt");
    write_file(p, std::to_string(n + 1) + "\n");
    return static_cast<std::uint16_t>(n + 1);
}

// --sweep key=a,b,... (exec.cfg keys, repeatable): every combination as config overrides, replayed on a
// thread pool; one TSV row per run and, with --report, a ranked index linking each run's report.
int sweep(const Options& base, const std::vector<std::pair<std::string, std::vector<std::string>>>& axes) {
    std::vector<std::vector<std::pair<std::string, std::string>>> combos(1);
    for (const auto& [key, values] : axes) {
        std::vector<std::vector<std::pair<std::string, std::string>>> next;
        for (const auto& c : combos)
            for (const std::string& v : values) {
                auto x = c;
                x.emplace_back(key, v);
                next.push_back(std::move(x));
            }
        combos = std::move(next);
    }
    if (combos.size() > 256) throw std::runtime_error("sweep has " + std::to_string(combos.size()) + " runs; at most 256");
    std::vector<PmSweepRun> runs(combos.size());
    std::vector<Options> opts(combos.size(), base);
    for (std::size_t i = 0; i < combos.size(); ++i) {
        for (const auto& [k, v] : combos[i]) {
            apply_config(opts[i].engine, k + "=" + v, "--sweep " + k + "=" + v);
            runs[i].label += (runs[i].label.empty() ? "" : ",") + k + "=" + v;
        }
        opts[i].quiet = true, opts[i].rows_out = &runs[i].rows;
        if (!base.report.empty()) opts[i].report = base.report / runs[i].label;
    }
    std::string error;
    std::mutex error_lock;
    hft::parallel_for(runs.size(), [&](std::size_t i) {
        try {
            replay(opts[i]);
        } catch (const std::exception& e) {
            const std::lock_guard g(error_lock);
            error = e.what();
        }
    });
    if (!error.empty()) throw std::runtime_error(error);
    const char* cols[] = {"ledger_pnl_usd", "fees_usd", "maker_fills", "taker_fills", "arb_attempts", "arb_realised_usd", "kill", "decision_hash_v2"};
    for (const auto& [k, v] : axes) std::printf("%s\t", k.c_str());
    for (const char* c : cols) std::printf("%s%s", c, c == cols[std::size(cols) - 1] ? "\n" : "\t");
    for (std::size_t i = 0; i < runs.size(); ++i) {
        for (const auto& [k, v] : combos[i]) std::printf("%s\t", v.c_str());
        for (const char* c : cols) std::printf("%s%s", runs[i].value(c).c_str(), c == cols[std::size(cols) - 1] ? "\n" : "\t");
    }
    if (!base.report.empty()) {
        write_pm_sweep_report(base.report, base.replay.filename().string(), runs);
        std::fprintf(stderr, "report: %s\n", (base.report / "index.html").c_str());
    }
    return 0;
}

int usage() {
    std::fprintf(stderr,
                 "usage: pm_exec --replay DIR [--config FILE]... [--set KEY=VALUE]... [--run OUT] [--profile] [--report DIR]\n"
                 "               [--quoter NAME] [--sweep KEY=V1,V2,... (repeatable; THREADS=N)]\n"
                 "       pm_exec --paper --run OUT --config FILE [--set KEY=VALUE]... [--seconds S] [--port P]\n"
                 "               [--cpu C] [--feed-cpus C,..] [--spin] [--spin-feeds] [--events N] [--max-tokens N] [--tokens-per-conn N]\n"
                 "               [--record-cap-gb G] [--exec-dir DIR] [--reset-kill]\n"
                 "kill: SIGUSR1 or a file OUT/KILL cancels every order and places none; it leaves EXEC_DIR/KILLED\n"
                 "      (default ~/data/exec) and later paper runs refuse to start until given --reset-kill\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    std::vector<fs::path> configs;
    std::vector<std::string> sets;
    std::vector<std::pair<std::string, std::vector<std::string>>> axes;
    bool paper = false, reset_kill = false;
    const char* home = std::getenv("HOME");
    fs::path exec_dir = fs::path(home ? home : ".") / "data" / "exec";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(usage());
            }
            return argv[++i];
        };
        auto num = [&] { return std::atof(val().c_str()); };
        if (a == "--replay") o.replay = val();
        else if (a == "--paper") paper = true;
        else if (a == "--config") configs.push_back(val());
        else if (a == "--set") sets.push_back(val());
        else if (a == "--sweep") {
            const std::string v = val();
            const std::size_t eq = v.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 == v.size()) return usage();
            std::vector<std::string> values;
            for (const auto part : std::views::split(v.substr(eq + 1), ',')) values.emplace_back(part.begin(), part.end());
            axes.emplace_back(v.substr(0, eq), std::move(values));
        }
        else if (a == "--run") o.run = val();
        else if (a == "--exec-dir") exec_dir = val();
        else if (a == "--reset-kill") reset_kill = true;
        else if (a == "--seconds") o.seconds = num();
        else if (a == "--port") o.port = static_cast<int>(num());
        else if (a == "--cpu") o.cpu = static_cast<int>(num());
        else if (a == "--spin") o.spin = true;
        else if (a == "--spin-feeds") o.spin_feeds = true;
        else if (a == "--feed-cpus") {
            o.feed_cpus = parse_cpus(val());
            if (o.feed_cpus.empty()) {
                std::fprintf(stderr, "--feed-cpus wants a list like 6,7\n");
                return 2;
            }
        }
        else if (a == "--events") o.events = static_cast<int>(num());
        else if (a == "--max-tokens") o.max_tokens = static_cast<int>(num());
        else if (a == "--tokens-per-conn") o.tokens_per_conn = std::max(1, static_cast<int>(num()));
        else if (a == "--record-cap-gb") o.record_cap_gb = num();
        else if (a == "--profile") o.engine.profile = true;
        else if (a == "--report") o.report = val();
        else if (a == "--quoter") o.engine.quoter = quoter_or_exit(val());
        else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return usage();
        }
    }
    if (paper == !o.replay.empty() || (paper && (o.run.empty() || configs.size() != 1 || !o.report.empty() || !axes.empty())) ||
        (!axes.empty() && (!o.run.empty() || !o.dump_fills.empty())))
        return usage();

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, SIG_IGN);
    try {
        if (configs.empty()) {
            configs.push_back(o.replay / "exec.cfg");
            if (fs::exists(o.replay / "overrides.cfg")) configs.push_back(o.replay / "overrides.cfg");
            if (!o.engine.quoter && fs::exists(o.replay / "quoter.txt")) {
                std::string q = read_file(o.replay / "quoter.txt");
                while (!q.empty() && q.back() == '\n') q.pop_back();
                o.engine.quoter = quoter_or_exit(q);
            }
        }
        o.engine.venue = true;
        for (const fs::path& c : configs) apply_config(o.engine, read_file(c), c.string());
        for (const std::string& s : sets) apply_config(o.engine, s, "--set " + s);

        int rc;
        if (paper) {
            const fs::path latch = exec_dir / "KILLED";
            if (fs::exists(latch)) {
                if (!reset_kill) {
                    std::string run = read_file(latch);
                    if (!run.empty() && run.back() == '\n') run.pop_back();
                    std::fprintf(stderr, "kill switch tripped in %s: read its kill.log, then start with --reset-kill\n", run.c_str());
                    return 1;
                }
                fs::remove(latch);
            }
            if (fs::exists(o.run)) throw std::runtime_error(o.run.string() + " exists: a paper run needs a new directory");
            fs::create_directories(o.run);
            o.engine.session = next_session(exec_dir);
            std::string overrides;
            for (const std::string& s : sets) overrides += s + "\n";
            overrides += "session = " + std::to_string(o.engine.session) + "\n";
            write_file(o.run / "exec.cfg", read_file(configs[0]));
            write_file(o.run / "overrides.cfg", overrides);
            if (o.engine.quoter) write_file(o.run / "quoter.txt", o.engine.quoter->name + "\n");
            o.record = o.run;
            o.kill_latch = latch;
            std::signal(SIGUSR1, on_kill_signal);
            rc = live(o);
        } else {
            if (!o.run.empty()) fs::create_directories(o.run);
            rc = axes.empty() ? replay(o) : sweep(o, axes);
        }
        return g_signal ? 128 + g_signal : rc;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}
