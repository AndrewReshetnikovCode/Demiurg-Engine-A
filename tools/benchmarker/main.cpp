// =============================================================================
// main.cpp — Benchmarker harness binary (Appendix E §E.7).
//
// Usage:
//   demen_benchmarker --suite <id> [--out path.json]
//
// Where <id> is one of: B-STREAM, B-MESH, B-TEXGEN. The Phase 8 cut wires
// only those three (the only ones whose suites exist as native test
// binaries today). B-FPS, B-AIR, B-WATER, B-WEATHER, B-COLD, B-DETERM
// land when the matching subsystems' acceptance tests grow timing assertions.
//
// The harness does NOT re-implement the suites. It shells to the existing
// test binary, captures wall-clock time, and emits a §E.5 JSON record.
// That keeps the timing path identical to what the Specialist iterates
// against, per Appendix E's "harness mirrors the shipping build" rule.
// =============================================================================
#include "demen/abi.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace {

struct Suite {
    const char* id;
    const char* binary;     // name of the test binary under build/tests/
    const char* metric;
    const char* unit;
    double      target;
    bool        lower_is_better;
};

constexpr Suite kSuites[] = {
    { "B-STREAM",  "test_round_trip_10k",   "round_trip_seconds", "s",  2.0,   true },
    { "B-MESH",    "test_greedy_mesh",      "cold_mesh_seconds",  "s",  0.5,   true },
    { "B-TEXGEN",  "test_atlas_hot_reload", "reload_seconds",     "s",  0.05,  true },
};

const Suite* find_suite(const char* id) {
    for (const auto& s : kSuites) if (std::strcmp(s.id, id) == 0) return &s;
    return nullptr;
}

int run_command(const std::string& cmd) {
    return std::system(cmd.c_str());
}

} // namespace

int main(int argc, char** argv) {
    const char* suite_id = nullptr;
    const char* out_path = "decision.json";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--suite") == 0 && i + 1 < argc) suite_id = argv[++i];
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (std::strcmp(argv[i], "--abi") == 0) {
            std::printf("DEMEN_ABI_VERSION=0x%08X\n", demen_abi_version());
            return 0;
        }
    }
    if (!suite_id) {
        std::fprintf(stderr,
            "demen_benchmarker — Appendix E §E.7\n"
            "usage: demen_benchmarker --suite <id> [--out path.json]\n"
            "suites: B-STREAM, B-MESH, B-TEXGEN\n");
        return 2;
    }

    const Suite* s = find_suite(suite_id);
    if (!s) { std::fprintf(stderr, "unknown suite '%s'\n", suite_id); return 2; }

    // Locate the test binary alongside this benchmarker. CMake places test
    // binaries under build/tests/native/<config>/<name>(.exe). The harness
    // is run by run.py which sets DEMEN_TEST_BIN_DIR; fall back to ./.
    const char* dir = std::getenv("DEMEN_TEST_BIN_DIR");
    std::string cmd = (dir ? dir : ".");
    cmd += '/'; cmd += s->binary;
#ifdef _WIN32
    cmd += ".exe";
#endif

    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    const int rc = run_command(cmd);
    const auto t1 = clk::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();

    const bool pass = (rc == 0) &&
        (s->lower_is_better ? secs < s->target : secs > s->target);

    std::ofstream out(out_path, std::ios::trunc);
    if (!out) { std::fprintf(stderr, "cannot write %s\n", out_path); return 3; }
    out << "{\n";
    out << "  \"suite\": \"" << s->id << "\",\n";
    out << "  \"metric\": \"" << s->metric << "\",\n";
    out << "  \"value\":  " << secs << ",\n";
    out << "  \"unit\":   \"" << s->unit << "\",\n";
    out << "  \"target\": " << s->target << ",\n";
    out << "  \"binary_rc\": " << rc << ",\n";
    out << "  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\"\n";
    out << "}\n";

    std::printf("[%s] %s = %.3f %s (target %.3f, rc=%d) -> %s\n",
        s->id, s->metric, secs, s->unit, s->target, rc, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
