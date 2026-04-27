// =============================================================================
// test_region_header.cpp — Phase 1 Specialist must pass this.
// =============================================================================
// Covers: Appendix A §A.3 (magic + version_byte placement),
//         §A.5 + invariant #7 (refused cleanly, not crashed on).
//
// Phase 0 status: SKELETON. The asserts are live but the helper functions
// that forge and mutate a region file are TODO — they land when the Phase 1
// Specialist implements the writer, because this test depends on the same
// byte layout Appendix A freezes.
// =============================================================================

#include "demen/voxel_store.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// Helpers — to be fleshed out by the Phase 1 Specialist. Signatures are
// fixed here so the test body does not change when the helpers land.
// -----------------------------------------------------------------------------

// Forge a minimally valid empty region file at the given path.
// Returns DEMEN_VS_OK on success.
static int forge_empty_region(const fs::path& p, uint8_t version_byte);

// Read byte at offset `off` of file `p`. Returns -1 on I/O error.
static int read_byte(const fs::path& p, size_t off);

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

static int test_magic_and_version_byte_present() {
    auto dir = fs::temp_directory_path() / "demen_test_magic";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto region = dir / "r.0.0.dem";

    if (forge_empty_region(region, DEMEN_REGION_FORMAT_VERSION) != DEMEN_VS_OK) return 1;

    // §A.3: "DEMEN\0\0\0" at offset 0, version at offset 8.
    const char expected_magic[8] = {'D','E','M','E','N',0,0,0};
    for (size_t i = 0; i < 8; ++i) {
        if (read_byte(region, i) != static_cast<int>(static_cast<uint8_t>(expected_magic[i]))) {
            std::fprintf(stderr, "magic byte %zu mismatch\n", i);
            return 2;
        }
    }
    if (read_byte(region, 8) != DEMEN_REGION_FORMAT_VERSION) {
        std::fprintf(stderr, "version byte missing or wrong\n");
        return 3;
    }
    return 0;
}

static int test_mismatched_version_refused_cleanly() {
    // Invariant #7: reading a file whose version byte doesn't match must
    // return DEMEN_VS_ERR_VERSION_MISMATCH with a clear error, never crash.
    auto dir = fs::temp_directory_path() / "demen_test_version_mismatch";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto region = dir / "r.0.0.dem";

    if (forge_empty_region(region, 0xFF) != DEMEN_VS_OK) return 1;

    demen_world_t world = 0;
    int rc = demen_world_open(dir.string().c_str(), &world);
    if (rc != DEMEN_VS_ERR_VERSION_MISMATCH) {
        std::fprintf(stderr, "expected version-mismatch, got %d\n", rc);
        return 2;
    }
    if (world != 0) {
        std::fprintf(stderr, "world handle leaked on version-mismatch\n");
        return 3;
    }
    return 0;
}

static int test_corrupt_magic_refused_cleanly() {
    // Related to #7: a file that isn't a DemEn region must also fail
    // cleanly at open time, not crash the process.
    auto dir = fs::temp_directory_path() / "demen_test_corrupt";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto region = dir / "r.0.0.dem";

    std::ofstream f(region, std::ios::binary);
    const char junk[32] = "NOT A REGION FILE";
    f.write(junk, sizeof(junk));
    f.close();

    demen_world_t world = 0;
    int rc = demen_world_open(dir.string().c_str(), &world);
    if (rc != DEMEN_VS_ERR_CORRUPT) {
        std::fprintf(stderr, "expected corrupt, got %d\n", rc);
        return 1;
    }
    return 0;
}

// =============================================================================
int main() {
    int failed = 0;
    struct Case { const char* name; int (*fn)(); };
    const Case cases[] = {
        { "magic_and_version_byte_present",    test_magic_and_version_byte_present    },
        { "mismatched_version_refused_cleanly", test_mismatched_version_refused_cleanly },
        { "corrupt_magic_refused_cleanly",      test_corrupt_magic_refused_cleanly      },
    };
    for (const auto& c : cases) {
        int rc = c.fn();
        std::printf("  %-42s %s\n", c.name, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0) ++failed;
    }
    return failed == 0 ? 0 : 1;
}

// =============================================================================
// Helpers
// =============================================================================
static int forge_empty_region(const fs::path& p, uint8_t version_byte) {
    // Emit an Appendix A §A.3 header: 8 KiB of zeros, magic at offset 0,
    // version byte at offset 8. Zero index/size tables. No column blobs.
    // Good enough for demen_world_open to probe magic+version and either
    // accept (happy path) or refuse (sad path).
    std::vector<uint8_t> bytes(4096 * 2, 0);
    const char magic[8] = {'D','E','M','E','N',0,0,0};
    std::memcpy(bytes.data(), magic, 8);
    bytes[8] = version_byte;

    // Leave the DEMROOT marker and bounds-tail zeroed on purpose — a forged
    // empty region does not claim to carry world bounds, so the reader will
    // fall back to zeroed params (which is fine; the version-byte test does
    // not touch bounds, and the corrupt-magic test ends in an earlier error).

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return DEMEN_VS_ERR_IO;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return f ? DEMEN_VS_OK : DEMEN_VS_ERR_IO;
}

static int read_byte(const fs::path& p, size_t off) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return -1;
    f.seekg(static_cast<std::streamoff>(off));
    int b = f.get();
    return b;
}
