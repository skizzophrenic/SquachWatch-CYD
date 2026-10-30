// Concurrent readers must see one complete publication including its source.
// This tests the actual GNSS publication path, not a substitute lock/cache.
#include "gnss.h"
#include "wardrive_capture.h"
#include "test_util.h"
#include <atomic>
#include <thread>

int main() {
    suite("Published GNSS position and source are inseparable");
    Gnss::reset();
    std::atomic<bool> done{false}, bad{false};
    std::atomic<unsigned> reads{0};
    std::thread reader([&] {
        do {
            const auto s = Gnss::snapshot();
            if (s.position.valid) {
                const auto& f = s.position;
                bool coherent = false;
                if (s.source == Gnss::Source::BENCH)
                    coherent = f.lat7 == 430000000 && f.lon7 == -740000000 && f.altM == 0 && f.accM == 50 && f.epoch == 1793188800u && f.atMs == 1000;
                if (s.source == Gnss::Source::PHONE)
                    coherent = f.lat7 == 420000000 && f.lon7 == -750000000 && f.altM == 0 && f.accM == 7 && f.epoch == 1793188801u && f.atMs == 2000;
                if (!coherent) bad.store(true);
                reads.fetch_add(1);
            }
        } while (!done.load());
    });
    for (int n = 0; n < 50000; ++n) {
        Gnss::fake(430000000, -740000000, 1793188800u, 1000);
        Gnss::reset();
        Gnss::phone(420000000, -750000000, 7, 1793188801u, 2000);
    }
    done.store(true); reader.join();
    ck("reader observed publications", reads.load() > 0);
    ck("no torn position, timestamp, metadata or source", !bad.load());

    suite("Radio ring publication and reuse");
    Wardrive::Q q;
    std::atomic<bool> ringBad{false};
    std::thread producer([&] {
        for (uint32_t n = 1; n <= 100000; ++n) {
            Wardrive::Pending p{}; p.observedMs = n; p.epoch = n + 100;
            p.lat7 = (int32_t)n; p.lon7 = -(int32_t)n;
            while (!q.push(p)) std::this_thread::yield();
        }
    });
    for (uint32_t n = 1; n <= 100000; ++n) {
        Wardrive::Pending p;
        while (!q.pop(p)) std::this_thread::yield();
        if (p.observedMs != n || p.epoch != n + 100 || p.lat7 != (int32_t)n || p.lon7 != -(int32_t)n) ringBad.store(true);
    }
    producer.join();
    ck("100000 complete records remain in order across ring wraps", !ringBad.load());
    printf("CYD Pending %zu bytes; one Q %zu bytes; both queues %zu bytes\n", sizeof(Wardrive::Pending), sizeof(Wardrive::Q), 2 * sizeof(Wardrive::Q));
    return report();
}
