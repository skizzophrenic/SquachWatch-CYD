// Exercise the real writer with a constrained contiguous startup heap.
// Host malloc has ample memory, so account actual nothrow buffer allocations
// against one 12,276-byte region, with 48 bytes of overhead per allocation.
// This is a conservative budget fixture, not an ESP32 allocator emulator.
#include <new>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "Arduino.h"
#include <esp_heap_caps.h>
#include "gnss.h"
#include "test_util.h"

namespace StartupHeap {
static bool recording = false;
static size_t reserved = 0;
static size_t external = 0;
static void* allocate(size_t n) noexcept {
    void* p = std::malloc(n);
    if (p && recording) reserved += n + 48;
    return p;
}
}
void* operator new(size_t n, const std::nothrow_t&) noexcept { return StartupHeap::allocate(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return StartupHeap::allocate(n); }
static size_t constrainedHeap(uint32_t) {
    const size_t used = StartupHeap::reserved + StartupHeap::external;
    return used < 12276 ? 12276 - used : 0;
}
#define heap_caps_get_largest_free_block constrainedHeap
#include "../src/wardrive_sd.cpp"
#undef heap_caps_get_largest_free_block

static void nav(const char* body, uint32_t now) {
    unsigned char sum = 0;
    for (const char* p = body; *p; ++p) sum ^= *p;
    char line[128];
    snprintf(line, sizeof line, "$%s*%02X", body, sum);
    Gnss::sentence(line, now);
}
static void position(uint32_t now) {
    nav("GNRMC,120000.00,A,4000.0000,N,07400.0000,W,0,0,281026,,,A", now);
    nav("GNGGA,120000.00,4000.0000,N,07400.0000,W,1,06,1.0,5.0,M,0.0,M,,", now);
}
static void tick(uint32_t now) {
    SimClock::nowMs = now;
    Wardrive::sdTick(now, true);
}
static void note(uint8_t id) {
    const uint8_t mac[] = {2, 3, 4, 5, 6, id};
    Wardrive::noteWifi(mac, "heap-test", 0, 6, -48);
}

int main() {
    SimClock::virtualTime = true;
    char path[] = "/tmp/sqw-sd-heap-XXXXXX";
    const char* card = mkdtemp(path);
    if (!card) return 1;
    setenv("SQUACHSIM_SD", card, 1);
    Gnss::reset();
    position(1000);
    SimClock::nowMs = 1000;
    StartupHeap::recording = true;
    Wardrive::sdBegin();
    StartupHeap::recording = false;
    const size_t bootReserved = StartupHeap::reserved;

    suite("Early Elecrow reservation without enabling recording");
    ck("working buffers are allocated at startup", bootReserved > 0);
    ck("startup reservation leaves wardriving off", !Wardrive::enabled());
    note(6);
    ck("reservation alone cannot record observations", Wardrive::written() == 0);
    Wardrive::setEnabled(true);
    StartupHeap::recording = true;
    tick(1000);
    StartupHeap::recording = false;
    ck("enabling recording reuses startup allocations", StartupHeap::reserved == bootReserved);

    suite("Elecrow wardrive under a constrained startup heap");
    ck("buffer allocation leaves enough reserve to start logging",
       Wardrive::sdState(1000) == Wardrive::SdState::LOGGING);
    note(7); tick(1100); tick(3101);
    ck("an observed network reaches the card", Wardrive::written() == 1);
    std::string csv;
    if (Wardrive::sdFileName()[0]) {
        std::ifstream in(std::string(card) + Wardrive::sdFileName());
        csv.assign(std::istreambuf_iterator<char>(in), {});
    }
    ck("the saved row retains its receiver coordinates",
       csv.find(",40.0000000,-74.0000000,5,4,") != std::string::npos);

    StartupHeap::external = 2000;
    tick(4000); note(8); tick(4100);
    ck("additional memory pressure still pauses new recording",
       Wardrive::sdState(4100) == Wardrive::SdState::LOW_MEMORY && Wardrive::written() == 1);
    StartupHeap::external = 0;
    position(7000); tick(7000); note(9); tick(7100); tick(9101);
    ck("recording recovers when the reserve becomes available", Wardrive::written() == 2);

    Wardrive::setEnabled(false); tick(9200);
    StartupHeap::recording = true;
    Wardrive::setEnabled(true); tick(9300);
    StartupHeap::recording = false;
    ck("OFF and ON retain the reservation without reallocating", StartupHeap::reserved == bootReserved);
    note(7); tick(9400); tick(11401);
    ck("a new session does not inherit the previous repeat cache", Wardrive::written() == 3);
    Wardrive::setEnabled(false); tick(11402);
    std::filesystem::remove_all(card);
    return report();
}
