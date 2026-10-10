// Pure decision check: catches loss of retries when generation is unchanged.
// The actual NimBLE advertising return values are verified by firmware build
// and, for real controller failures, by LAB diagnostics/serial logs.
#include "mesh_tx_policy.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace MeshTxPolicy;
    assert(attempt(true, true, 100, 0));
    assert(!attempt(true, true, 200, 100));   // limit retries
    assert(attempt(true, true, 601, 100));   // retry failed singleton frame
    assert(!attempt(false, true, 601, 100)); // never advertise without consent
    assert(!attempt(true, false, 601, 100));
    assert(attempt(true, true, 1101, 601));
    std::puts("PASS: advertising retry backoff and consent gate");
}
