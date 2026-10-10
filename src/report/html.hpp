#pragma once

// Self-contained HTML run reports: inline SVG charts and tables, no scripts or external files,
// so a report opens anywhere and can be attached to a pull request or an email.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hft::report {

inline std::string esc(const std::string& s) {
    std::string o;
    for (const char c : s) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

inline std::string num(double v, int prec = 2) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", prec, v);
    return b;
}

inline std::string money(double usd) { return (usd < 0 ? "-$" : "$") + num(std::fabs(usd)); }

struct Series {
    explicit Series(std::string n, std::string c = "var(--s1)") : name(std::move(n)), color(std::move(c)) {}
    std::string name;
    std::vector<double> x, y;
    std::string color;
    bool step = false;    // hold each value until the next x
    bool points = false;  // dots instead of a line
};

// Round tick step of about (hi - lo) / n: 1, 2 or 5 times a power of ten.
inline double tick_step(double lo, double hi, int n) {
    const double raw = (hi - lo) / n;
    if (!(raw > 0)) return 1;
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    for (const double m : {1.0, 2.0, 5.0, 10.0})
        if (raw <= m * p) return m * p;
    return 10 * p;
}

inline std::string chart(const std::string& title, const std::string& xlabel, const std::string& ylabel,
                         const std::vector<Series>& series, bool zero_line = true) {
    constexpr double W = 760, H = 280, L = 70, R = 16, T = 30, B = 44;
    double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    for (const Series& s : series)
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            if (!std::isfinite(s.x[i]) || !std::isfinite(s.y[i])) continue;
            x0 = std::min(x0, s.x[i]), x1 = std::max(x1, s.x[i]);
            y0 = std::min(y0, s.y[i]), y1 = std::max(y1, s.y[i]);
        }
    std::string o = "<figure><figcaption>" + esc(title) + "</figcaption>";
    if (!(x0 <= x1)) return o + "<p class=\"muted\">no data</p></figure>";
    if (zero_line) y0 = std::min(y0, 0.0), y1 = std::max(y1, 0.0);
    if (x1 == x0) x1 = x0 + 1;
    if (y1 == y0) y1 = y0 + 1;
    const double pad = (y1 - y0) * 0.06;
    y0 -= pad, y1 += pad;
    auto px = [&](double x) { return L + (x - x0) / (x1 - x0) * (W - L - R); };
    auto py = [&](double y) { return T + (y1 - y) / (y1 - y0) * (H - T - B); };
    o += "<svg viewBox=\"0 0 " + num(W, 0) + " " + num(H, 0) + "\" role=\"img\" aria-label=\"" + esc(title) + "\">";
    const double ys = tick_step(y0, y1, 5), xs = tick_step(x0, x1, 8);
    const int yp = ys >= 1 ? 0 : static_cast<int>(std::ceil(-std::log10(ys)));
    const int xp = xs >= 1 ? 0 : static_cast<int>(std::ceil(-std::log10(xs)));
    for (double y = std::ceil(y0 / ys) * ys; y <= y1; y += ys) {
        o += "<line class=\"grid\" x1=\"" + num(L, 1) + "\" x2=\"" + num(W - R, 1) + "\" y1=\"" + num(py(y), 1) + "\" y2=\"" +
             num(py(y), 1) + "\"/>";
        o += "<text class=\"axis\" x=\"" + num(L - 6, 1) + "\" y=\"" + num(py(y) + 4, 1) + "\" text-anchor=\"end\">" + num(y, yp) +
             "</text>";
    }
    for (double x = std::ceil(x0 / xs) * xs; x <= x1; x += xs)
        o += "<text class=\"axis\" x=\"" + num(px(x), 1) + "\" y=\"" + num(H - B + 16, 1) + "\" text-anchor=\"middle\">" +
             num(x, xp) + "</text>";
    if (zero_line && y0 < 0 && y1 > 0)
        o += "<line class=\"zero\" x1=\"" + num(L, 1) + "\" x2=\"" + num(W - R, 1) + "\" y1=\"" + num(py(0), 1) + "\" y2=\"" +
             num(py(0), 1) + "\"/>";
    o += "<text class=\"axis\" x=\"" + num((L + W - R) / 2, 1) + "\" y=\"" + num(H - 6, 1) + "\" text-anchor=\"middle\">" +
         esc(xlabel) + "</text>";
    o += "<text class=\"axis\" transform=\"translate(14 " + num((T + H - B) / 2, 1) + ") rotate(-90)\" text-anchor=\"middle\">" +
         esc(ylabel) + "</text>";
    double lx = L + 8;
    for (const Series& s : series) {
        if (s.points) {
            for (std::size_t i = 0; i < s.x.size(); ++i)
                if (std::isfinite(s.y[i]))
                    o += "<circle cx=\"" + num(px(s.x[i]), 1) + "\" cy=\"" + num(py(s.y[i]), 1) + "\" r=\"2\" fill-opacity=\"0.55\" fill=\"" +
                         s.color + "\"/>";
        } else if (!s.x.empty()) {
            std::string d;
            for (std::size_t i = 0; i < s.x.size(); ++i) {
                if (!std::isfinite(s.y[i])) continue;
                if (s.step && !d.empty()) d += "L" + num(px(s.x[i]), 1) + "," + num(py(s.y[i - 1]), 1);
                d += (d.empty() ? "M" : "L") + num(px(s.x[i]), 1) + "," + num(py(s.y[i]), 1);
            }
            o += "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + s.color + "\" stroke-width=\"1.6\"/>";
        }
        if (series.size() > 1) {
            o += "<rect x=\"" + num(lx, 1) + "\" y=\"10\" width=\"10\" height=\"10\" rx=\"2\" fill=\"" + s.color + "\"/>";
            o += "<text class=\"axis\" x=\"" + num(lx + 14, 1) + "\" y=\"19\">" + esc(s.name) + "</text>";
            lx += 24 + 7.0 * static_cast<double>(s.name.size());
        }
    }
    return o + "</svg></figure>";
}

inline std::string table(const std::vector<std::string>& head, const std::vector<std::vector<std::string>>& rows) {
    std::string o = "<table><thead><tr>";
    for (const auto& h : head) o += "<th>" + esc(h) + "</th>";
    o += "</tr></thead><tbody>";
    for (const auto& r : rows) {
        o += "<tr>";
        for (const auto& c : r) o += "<td>" + esc(c) + "</td>";
        o += "</tr>";
    }
    return o + "</tbody></table>";
}

// Headline numbers as cards: (label, value) pairs.
inline std::string cards(const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string o = "<div class=\"cards\">";
    for (const auto& [k, v] : kv) o += "<div class=\"card\"><div class=\"k\">" + esc(k) + "</div><div class=\"v\">" + esc(v) + "</div></div>";
    return o + "</div>";
}

inline std::string page(const std::string& title, const std::string& subtitle, const std::string& body) {
    return "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\"><title>" +
           esc(title) +
           "</title><style>"
           ":root{--bg:#fbfbfa;--fg:#1d1d1b;--muted:#6b6b66;--line:#e2e1dc;--card:#fff;--s1:#2563eb;--s2:#dc2626;--s3:#059669}"
           "@media(prefers-color-scheme:dark){:root{--bg:#141413;--fg:#ecebe6;--muted:#9c9b95;--line:#2e2e2b;--card:#1d1d1b;"
           "--s1:#60a5fa;--s2:#f87171;--s3:#34d399}}"
           "body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}"
           "main{max-width:820px;margin:0 auto;padding:24px 16px 48px}h1{font-size:22px;margin:0}"
           ".muted,.sub{color:var(--muted)}.sub{margin:4px 0 20px}h2{font-size:16px;margin:28px 0 8px}"
           ".cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}"
           ".card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px 12px}"
           ".k{color:var(--muted);font-size:12px}.v{font-size:18px;font-variant-numeric:tabular-nums}"
           "figure{margin:16px 0;background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px}"
           "figcaption{font-weight:600;margin-bottom:4px}svg{width:100%;height:auto;display:block}"
           ".grid{stroke:var(--line)}.zero{stroke:var(--muted);stroke-dasharray:3 3}.axis{fill:var(--muted);font-size:11px}"
           "table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums;font-size:13px}"
           "th,td{border-bottom:1px solid var(--line);padding:4px 8px;text-align:right}th:first-child,td:first-child{text-align:left}"
           "</style></head><body><main><h1>" +
           esc(title) + "</h1><p class=\"sub\">" + esc(subtitle) + "</p>" + body + "</main></body></html>\n";
}

inline void write_file(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    if (!(f << text)) throw std::runtime_error("cannot write " + path);
}

}  // namespace hft::report
