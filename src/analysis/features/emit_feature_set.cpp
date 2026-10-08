// Agent-A (T26) - real-data FeatureSet dumper.
//
// Standalone executor that runs the REAL C++ AnalyticalFeatureEngine over
// bars read from CSV and serialises the resulting AnalyticalFeatureSet to the
// frozen JSON contract consumed by src/models/features.py (parse_feature_set).
//
// This is the T26 boundary: feature computation happens ONLY here (the engine),
// never in Python. The Python side (research/features_real/run_features.py) is a
// data-prep driver: it aggregates M1 bars into the requested timeframes and
// hands them to this binary; it does not compute a single feature.
//
// Usage:
//   aura_feature_dump <bars_dir> <decisions.csv> <out.json> [timeframes_csv]
//
//   <bars_dir>/<TF>.csv   header: timestamp_ms_utc,open,high,low,close,volume
//                         (bar-open time in ms UTC, ascending, no gaps assumed)
//   <decisions.csv>       one decision instant per line: unix seconds UTC
//   <out.json>            array of FeatureSet objects, one per decision
//   [timeframes_csv]      default: all nine present as <TF>.csv
//
// Deterministic: identical inputs always produce byte-identical output (no
// wall-clock, no randomness). Causal: the engine is pinned to each decision
// instant, so no bar opening after it can be read.

#include "analysis/features/AnalyticalFeatureEngine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(line);
    while (std::getline(ss, cur, ',')) out.push_back(trim(cur));
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

// Crisis-proof double rendering: fixed 9 dp keeps output byte-identical across
// runs and avoids locale/scientific-notation drift.
std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9f", v);
    std::string s = buf;
    // Trim trailing zeros but keep at least one decimal digit.
    if (s.find('.') != std::string::npos) {
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

// Reads <dir>/<TF>.csv into Bars. Returns false if the file is absent.
bool readBars(const std::string& path, aura::Timeframe tf,
              std::vector<aura::Bar>& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (trim(line).empty()) continue;
        if (header) { header = false; continue; }  // skip column names
        const auto f = splitCsv(line);
        if (f.size() < 6) continue;
        aura::Bar b;
        b.timeframe = tf;
        b.openTimeSec = std::stoll(f[0]) / 1000;  // ms -> s
        b.open = std::stod(f[1]);
        b.high = std::stod(f[2]);
        b.low = std::stod(f[3]);
        b.close = std::stod(f[4]);
        b.tickVolume = static_cast<std::int64_t>(std::llround(std::stod(f[5])));
        b.realVolume = 0;
        b.spread = 0;
        b.quality = aura::DataQualityState::VALID;
        b.sourceSymbol = "XAUUSD";
        b.sourceBroker = "DUKASCOPY";
        out.push_back(b);
    }
    return true;
}

void writeVector(std::ostringstream& o, const aura::TimeframeFeatures& f) {
    o << "{";
    o << "\"timeframe\":\"" << aura::toString(f.timeframe) << "\",";
    o << "\"asOfBarOpenSec\":" << f.asOfBarOpenSec << ",";
    o << "\"barsAvailable\":" << f.barsAvailable << ",";
    o << "\"windowUsed\":" << f.windowUsed << ",";
    o << "\"quality\":\"" << aura::toString(f.quality) << "\",";
    o << "\"valid\":" << (f.valid ? "true" : "false") << ",";
    o << "\"values\":{";
    o << "\"structureTrend\":" << num(f.structureTrend) << ",";
    o << "\"rangePosition\":" << num(f.rangePosition) << ",";
    o << "\"swingAsymmetry\":" << num(f.swingAsymmetry) << ",";
    o << "\"bodyRatio\":" << num(f.bodyRatio) << ",";
    o << "\"upperWickRatio\":" << num(f.upperWickRatio) << ",";
    o << "\"lowerWickRatio\":" << num(f.lowerWickRatio) << ",";
    o << "\"candleDirection\":" << num(f.candleDirection) << ",";
    o << "\"runBalance\":" << num(f.runBalance) << ",";
    o << "\"momentumNorm\":" << num(f.momentumNorm) << ",";
    o << "\"momentumPersistence\":" << num(f.momentumPersistence) << ",";
    o << "\"momentumAcceleration\":" << num(f.momentumAcceleration) << ",";
    o << "\"volatilityRatio\":" << num(f.volatilityRatio) << ",";
    o << "\"atrRatio\":" << num(f.atrRatio) << ",";
    o << "\"netChangeRatio\":" << num(f.netChangeRatio) << ",";
    o << "\"higherHighShare\":" << num(f.higherHighShare) << ",";
    o << "\"lowerLowShare\":" << num(f.lowerLowShare) << ",";
    o << "\"patternScore\":" << num(f.patternScore) << ",";
    o << "\"contextTrend\":" << num(f.contextTrend) << ",";
    o << "\"contextVolatility\":" << num(f.contextVolatility) << ",";
    o << "\"contextRangePosition\":" << num(f.contextRangePosition);
    o << "},";
    o << "\"detail\":\"" << jsonEscape(f.detail) << "\"";
    o << "}";
}

void writeCross(std::ostringstream& o, const aura::CrossTimeframeFeatures& c) {
    o << "{";
    o << "\"asOfBarOpenSec\":" << c.asOfBarOpenSec << ",";
    o << "\"quality\":\"" << aura::toString(c.quality) << "\",";
    o << "\"valid\":" << (c.valid ? "true" : "false") << ",";
    o << "\"m15Available\":" << (c.m15Available ? "true" : "false") << ",";
    o << "\"h4Available\":" << (c.h4Available ? "true" : "false") << ",";
    o << "\"d1Available\":" << (c.d1Available ? "true" : "false") << ",";
    o << "\"values\":{";
    o << "\"h4M15Agreement\":" << num(c.h4M15Agreement) << ",";
    o << "\"h4D1Agreement\":" << num(c.h4D1Agreement) << ",";
    o << "\"mtfConflictScore\":" << num(c.mtfConflictScore) << ",";
    o << "\"h4StructuralAuthority\":" << num(c.h4StructuralAuthority) << ",";
    o << "\"m15TriggerState\":" << num(c.m15TriggerState);
    o << "},";
    o << "\"detail\":\"" << jsonEscape(c.detail) << "\"";
    o << "}";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: %s <bars_dir> <decisions.csv> <out.json> "
                     "[timeframes_csv]\n",
                     argv[0]);
        return 2;
    }
    const std::string barsDir = argv[1];
    const std::string decisionsPath = argv[2];
    const std::string outPath = argv[3];

    std::vector<std::string> tfs;
    if (argc >= 5) {
        const auto parts = splitCsv(argv[4]);
        tfs.assign(parts.begin(), parts.end());
    } else {
        tfs = {"M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"};
    }

    std::map<aura::Timeframe, std::vector<aura::Bar>> byTimeframe;
    for (const auto& tfName : tfs) {
        aura::Timeframe tf;
        if (!aura::parseTimeframe(tfName, tf)) {
            std::fprintf(stderr, "unknown timeframe: %s\n", tfName.c_str());
            return 2;
        }
        std::vector<aura::Bar> bars;
        if (!readBars(barsDir + "/" + tfName + ".csv", tf, bars)) {
            std::fprintf(stderr, "missing bars file: %s/%s.csv\n",
                         barsDir.c_str(), tfName.c_str());
            return 3;
        }
        byTimeframe[tf] = std::move(bars);
    }

    std::ifstream dec(decisionsPath);
    if (!dec) {
        std::fprintf(stderr, "cannot open decisions: %s\n", decisionsPath.c_str());
        return 3;
    }
    std::vector<std::int64_t> decisions;
    std::string line;
    while (std::getline(dec, line)) {
        const std::string t = trim(line);
        if (t.empty()) continue;
        decisions.push_back(std::stoll(t));
    }

    // Decisions must be ascending for the causal cursor below.
    for (std::size_t i = 1; i < decisions.size(); ++i) {
        if (decisions[i] < decisions[i - 1]) {
            std::fprintf(stderr, "decisions must be ascending (line %zu)\n", i + 1);
            return 2;
        }
    }

    const aura::AnalyticalFeatureEngine engine;
    // The engine reads only the last `contextBars` (default 200) and the last
    // kTriggerWindow (9) closed bars, so retaining a bounded trailing window per
    // stream yields byte-identical features while keeping the run O(n) instead
    // of O(n^2). `retained` (>= contextBars) is the window actually supplied;
    // the reported barsAvailable is therefore a lower bound on available history.
    constexpr std::size_t kRetainBars = 300;
    std::map<aura::Timeframe, std::size_t> cursor;  // next unread bar index
    std::map<aura::Timeframe, std::deque<aura::Bar>> retained;
    std::map<aura::Timeframe, std::vector<aura::Bar>> closed;
    std::ostringstream out;
    out << "[\n";
    for (std::size_t i = 0; i < decisions.size(); ++i) {
        const std::int64_t asOf = decisions[i];
        // Causality: a bar is usable only once it has CLOSED at or before the
        // decision instant. The engine filters by bar-open time, which alone
        // would admit a bar still forming at `asOf`; retaining only the
        // fully-closed prefix here closes that gap. The instant reported is the
        // true decision instant `asOf`, so all vectors share it.
        for (auto& entry : byTimeframe) {
            const auto tf = entry.first;
            const auto& bars = entry.second;
            std::size_t& c = cursor[tf];
            auto& win = retained[tf];
            while (c < bars.size() && bars[c].closeTimeSec() <= asOf) {
                win.push_back(bars[c]);
                ++c;
            }
            while (win.size() > kRetainBars) win.pop_front();
            closed[tf].assign(win.begin(), win.end());
        }
        const auto set = engine.computeAll(closed, asOf);
        // T27 contract: the decision-bar close (last M15 bar closed at `asOf`)
        // is emitted as a top-level sibling key so the calibration layer can
        // label without re-reading the corpus. Causal: it is the most recent
        // close known at the decision instant. Absent only before the first M15
        // close, which callers exclude.
        const auto& m15 = retained[aura::Timeframe::M15];
        if (m15.empty()) {
            std::fprintf(stderr,
                         "decision %lld precedes the first closed M15 bar; "
                         "start decisions after it\n",
                         (long long)asOf);
            return 2;
        }
        const double decisionClose = m15.back().close;
        out << "  {";
        out << "\"asOfBarOpenSec\":" << set.asOfBarOpenSec << ",";
        out << "\"close\":" << num(decisionClose) << ",";
        out << "\"quality\":\"" << aura::toString(set.quality) << "\",";
        out << "\"valid\":" << (set.valid ? "true" : "false") << ",";
        out << "\"perTimeframe\":[";
        for (std::size_t j = 0; j < set.perTimeframe.size(); ++j) {
            if (j) out << ",";
            writeVector(out, set.perTimeframe[j]);
        }
        out << "],";
        out << "\"cross\":";
        writeCross(out, set.cross);
        out << "}";
        if (i + 1 < decisions.size()) out << ",";
        out << "\n";
    }
    out << "]\n";

    std::ofstream fo(outPath, std::ios::binary);
    if (!fo) {
        std::fprintf(stderr, "cannot write out: %s\n", outPath.c_str());
        return 3;
    }
    fo << out.str();
    std::fprintf(stderr, "aura_feature_dump: %zu decision(s) -> %s\n",
                 decisions.size(), outPath.c_str());
    return 0;
}
