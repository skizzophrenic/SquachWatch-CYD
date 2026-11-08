"""Host regression checks for short FT6336 packets and retries.

Run: python3 test/ws_c5_touch_read_test.py
Compiles the production driver against a minimal Wire/Arduino fake.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / "Arduino.h").write_text(r"""
#pragma once
#include <stdint.h>
#define LOW 0
#define INPUT 0
inline int irq = LOW;
inline int digitalRead(int) { return irq; }
inline void pinMode(int, int) {}
struct SerialFake { template<class... T> void printf(const char*, T...) {} };
inline SerialFake Serial;
""")
    (tmp / "Wire.h").write_text(r"""
#pragma once
#include <vector>
#include <stdint.h>
struct WireFake {
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint8_t> packet;
    unsigned reads = 0, pos = 0;
    void beginTransmission(int) {}
    void write(uint8_t) {}
    int endTransmission(bool = true) { return 0; }
    int requestFrom(int, int) {
        packet = reads < packets.size() ? packets[reads] : std::vector<uint8_t>{};
        ++reads; pos = 0; return packet.size();
    }
    bool available() { return pos < packet.size(); }
    int read() { return available() ? packet[pos++] : -1; }
    void end() {}
    void begin(int, int, unsigned) {}
};
inline WireFake Wire;
""")
    (tmp / "test.cpp").write_text(r"""
#include <cassert>
#include <iostream>
#include "ws_c5_touch.cpp"
namespace WsExio { bool begin() { return true; } }
int main() {
    uint16_t x = 999, y = 999;
    const std::vector<uint8_t> valid = {1, 0, 123, 1, 44, 0, 0};
    for (unsigned length = 0; length < 7; ++length) {
        Wire = {}; irq = LOW;
        Wire.packets.assign(3, std::vector<uint8_t>(valid.begin(), valid.begin() + length));
        assert(!WsTouch::read(x, y));
        assert(x == 999 && y == 999 && Wire.reads == 3);
    }
    Wire = {}; Wire.packets = {{1}, valid};
    assert(WsTouch::read(x, y) && x == 123 && y == 300 && Wire.reads == 2);
    Wire = {}; irq = 1;
    assert(!WsTouch::read(x, y) && Wire.reads == 0);
    Wire = {}; irq = LOW; Wire.packets.assign(3, {1, 1, 64, 0, 0, 0, 0});
    assert(!WsTouch::read(x, y)); // x=320 is outside the panel
    std::cout << "FT6336 packet/retry/INT checks passed\n";
}
""")
    exe = tmp / "test"
    subprocess.run(["c++", "-std=c++17", "-DWS_C5_35=1", "-DTFT_WIDTH=320",
                    "-DTFT_HEIGHT=480", "-I" + str(tmp), "-I" + str(ROOT / "include"),
                    "-I" + str(ROOT / "src"), str(tmp / "test.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
