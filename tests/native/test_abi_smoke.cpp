// Phase 0 smoke test: demen_core exports demen_abi_version and it returns the
// expected constant. If this fails, the interop boundary is broken and no
// other test can be trusted.
#include "demen/abi.hpp"

#include <cstdio>
#include <cstdlib>

int main() {
    const uint32_t v = demen_abi_version();
    if (v != DEMEN_ABI_VERSION) {
        std::fprintf(stderr,
            "ABI version mismatch: got 0x%08X, expected 0x%08X\n",
            v, DEMEN_ABI_VERSION);
        return 1;
    }
    std::printf("ABI smoke test OK (0x%08X).\n", v);
    return 0;
}
