// Live paper trading on the prediction-market stream (no order is ever sent) and replay of its
// recordings, which gives the same decisions and hash. Options: kUsage.

#include "pm_app.hpp"

constexpr const char* kUsage =
    "usage: pm_live [--events N] [--max-tokens N] [--tokens-per-conn N] [--port P] [--record DIR [--record-cap-gb G]]\n"
    "               [--seconds S] [--cpu C] [--spin] [--spin-feeds] [--no-quote] [options]\n"
    "       pm_live --replay DIR [options]\n"
    "maker and risk: --gamma G --k K --size S --max-inv N --horizon-s T --loss-stop USD --max-gross N --stale-s S\n"
    "venue (replay): --venue | --venue-compat, --lat-ms M --jitter-ms M --seed N --drop-ack P --drop-fill P\n"
    "                --dup P --settle-fail P --disconnect EVERY_S:FOR_S --dump-fills FILE";

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        auto num = [&] { return std::atof(val().c_str()); };
        if (a == "--events") o.events = static_cast<int>(num());
        else if (a == "--max-tokens") o.max_tokens = static_cast<int>(num());
        else if (a == "--tokens-per-conn") o.tokens_per_conn = std::max(1, static_cast<int>(num()));
        else if (a == "--port") o.port = static_cast<int>(num());
        else if (a == "--record") o.record = val();
        else if (a == "--record-cap-gb") o.record_cap_gb = num();
        else if (a == "--replay") o.replay = val();
        else if (a == "--dump-fills") o.dump_fills = val();
        else if (a == "--venue") o.engine.venue = true;
        else if (a == "--venue-compat") o.engine.venue = true, config_detail::compat(o.engine, 1);  // D25
        else if (a == "--lat-ms") o.engine.sim.lat_in = o.engine.sim.lat_out = static_cast<exec::Ns>(num() * 1e6);
        else if (a == "--jitter-ms") o.engine.sim.jitter = static_cast<exec::Ns>(num() * 1e6);
        else if (a == "--seed") o.engine.sim.seed = static_cast<std::uint64_t>(num());
        else if (a == "--drop-ack") o.engine.sim.p_drop_ack_ppm = static_cast<std::uint32_t>(num() * 1e6);
        else if (a == "--drop-fill") o.engine.sim.p_drop_fill_ppm = static_cast<std::uint32_t>(num() * 1e6);
        else if (a == "--dup") o.engine.sim.p_dup_ppm = static_cast<std::uint32_t>(num() * 1e6);
        else if (a == "--settle-fail") o.engine.sim.p_settle_fail_ppm = static_cast<std::uint32_t>(num() * 1e6);
        else if (a == "--disconnect") {  // "every_s:for_s"
            const std::string v = val();
            o.engine.sim.disc_every = static_cast<exec::Ns>(std::atof(v.c_str()) * 1e9);
            o.engine.sim.disc_for = static_cast<exec::Ns>(std::atof(v.substr(v.find(':') + 1).c_str()) * 1e9);
        }
        else if (a == "--seconds") o.seconds = num();
        else if (a == "--cpu") o.cpu = static_cast<int>(num());
        else if (a == "--no-quote") o.engine.quote = false;
        else if (a == "--spin") o.spin = true;
        else if (a == "--spin-feeds") o.spin_feeds = true;
        else if (a == "--gamma") o.engine.maker.gamma = num();
        else if (a == "--k") o.engine.maker.k = num();
        else if (a == "--size") o.engine.maker.size = num();
        else if (a == "--max-inv") o.engine.maker.max_inv = num();
        else if (a == "--horizon-s") o.engine.maker.horizon_s = num();
        else if (a == "--loss-stop") o.engine.loss_stop_usd = num();
        else if (a == "--max-gross") o.engine.max_gross_shares = num();
        else if (a == "--stale-s") o.engine.stale_s = num();
        else {
            std::fprintf(stderr, "unknown option %s\n%s\n", a.c_str(), kUsage);
            return 2;
        }
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, SIG_IGN);  // closing the session that started it is not a stop
    try {
        const int rc = o.replay.empty() ? live(o) : replay(o);
        return g_signal ? 128 + g_signal : rc;  // a stop request is not a crash to restart after
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}
