// Minimal assertions. Deliberately not a framework: these tests exist to
// be run by anyone with a compiler and no setup, and a dependency would
// undo that.
#pragma once
#include <cstdio>
#include <cmath>

inline int g_fails = 0;
inline const char* g_suite = "";

inline void suite(const char* name) {
    g_suite = name;
    printf("\n%s\n", name);
}

inline void ck(const char* what, bool ok) {
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) g_fails++;
}

// Coordinates are the reason this file has a float comparison at all.
// They round-trip through a fixed-point integer, so the tolerance is
// about the encoding's own resolution rather than about floating point
// being untrustworthy.
inline void ckf(const char* what, float got, float want, float tol) {
    const bool ok = fabsf(got - want) <= tol;
    printf("  %-52s %s (%.5f vs %.5f)\n", what, ok ? "ok" : "FAIL", got, want);
    if (!ok) g_fails++;
}

inline int report() {
    printf("\n%s\n", g_fails ? "*** FAILURES ***" : "all passed");
    return g_fails ? 1 : 0;
}
