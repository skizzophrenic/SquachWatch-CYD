#!/usr/bin/env python3
"""Exercise the actual UART ownership helper against a host hardware boundary.

The shims model GPIO/UART/logging effects; GNSS parsing uses production gnss.cpp.
Run directly with python3; no PlatformIO installation or device is required.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import textwrap

ROOT = Path(__file__).resolve().parents[1]
SHIMS = {
    "Arduino.h": r'''
        #pragma once
        #include <cstdint>
        #include <cstddef>
        #include <deque>
        #include <string>
        constexpr int INPUT = 1;
        constexpr uint32_t SERIAL_8N1 = 0x800001c;
        extern bool pinsInput[4], consoleOn, detached;
        extern unsigned pullsOff[4];
        extern bool bufferFail, uartFail;
        extern unsigned beginCalls;
        extern int begunRx, begunTx;
        extern uint32_t begunBaud, begunMode;
        extern size_t configuredBuffer;
        struct HardwareSerial {
            std::deque<unsigned char> input;
            bool running = false;
            void end() { running = false; consoleOn = false; }
            size_t setRxBufferSize(size_t n) {
                configuredBuffer = n;
                return bufferFail ? 0 : n;
            }
            void begin(uint32_t baud, uint32_t mode, int rx, int tx) {
                ++beginCalls; begunBaud = baud; begunMode = mode;
                begunRx = rx; begunTx = tx; running = !uartFail;
            }
            int available() const { return (int)input.size(); }
            int read() { if (input.empty()) return -1;
                int c = input.front(); input.pop_front(); return c; }
            explicit operator bool() const { return running; }
        };
        extern HardwareSerial Serial, Serial2;
        inline void pinMode(int pin, int mode) { pinsInput[pin] = mode == INPUT; }
    ''',
    "sdkconfig.h": "#define CONFIG_IDF_TARGET_ESP32 1\n",
    "esp_log.h": r'''
        #pragma once
        #include <cstdarg>
        constexpr int ESP_LOG_NONE = 0;
        extern int logLevel;
        extern int (*logSink)(const char*, va_list);
        inline void esp_log_level_set(const char*, int v) { logLevel = v; }
        inline void esp_log_set_vprintf(int (*fn)(const char*, va_list)) { logSink = fn; }
    ''',
    "esp32-hal-uart.h": r'''
        #pragma once
        #include <cassert>
        extern bool debugOff, detached;
        inline void uartSetDebug(void* v) { debugOff = v == nullptr; }
        inline void uartDetachPins(int uart, int rx, int tx, int cts, int rts) {
            assert(uart == 0 && rx == 3 && tx == 1 && cts == -1 && rts == -1);
            detached = true;
        }
    ''',
    "driver/gpio.h": r'''
        #pragma once
        constexpr int GPIO_NUM_1 = 1, GPIO_NUM_3 = 3;
        extern unsigned pullsOff[4];
        inline void gpio_pullup_dis(int p) { pullsOff[p] |= 1; }
        inline void gpio_pulldown_dis(int p) { pullsOff[p] |= 2; }
    ''',
}
HARNESS = r'''
    #include <Arduino.h>
    #include <esp_log.h>
    #include "gnss.h"
    #include <cstdio>
    #include <cstdlib>
    #include <cstdarg>
    bool pinsInput[4]{}, consoleOn = true, detached = false;
    unsigned pullsOff[4]{};
    bool debugOff = false, bufferFail = false, uartFail = false;
    unsigned beginCalls = 0;
    int begunRx = -2, begunTx = -2, logLevel = 4;
    uint32_t begunBaud = 0, begunMode = 0;
    size_t configuredBuffer = 0;
    int (*logSink)(const char*, va_list) = nullptr;
    HardwareSerial Serial, Serial2;
    #include "cyd_gps_uart.h"
    extern "C" void initVariant();
    static void check(bool ok, const char* text) {
        if (!ok) { std::fprintf(stderr, "FAIL: %s\n", text); std::exit(1); }
    }
    static int log(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        int n = logSink(fmt, args); va_end(args); return n;
    }
    static void quiet() {
        check(!consoleOn && detached && debugOff, "console UART remains detached and debug disabled");
        check(pinsInput[1] && pinsInput[3] && pullsOff[1] == 3 && pullsOff[3] == 3, "P1 pads released as inputs without pulls");
        check(logLevel == ESP_LOG_NONE && logSink && log("suppressed") == 0, "SDK output is discarded");
    }
    int main() {
        initVariant(); quiet();
        CydGpsUart::silenceConsole(); quiet();
    #if defined(CYD_GPS_ELECROW)
        CydGpsUart::begin();
        check(beginCalls == 1 && begunRx == 1 && begunTx == -1 && begunBaud == 9600 && begunMode == SERIAL_8N1,
              "physical GPS uses UART2 RX1/9600/8N1 with TX unassigned");
        check(configuredBuffer == 1024 && CydGpsUart::rxBufferReady() && CydGpsUart::receiving(), "bounded RX buffer starts");
        check(CydGpsUart::rxPin() == 1 && CydGpsUart::uartNumber() == 2 && CydGpsUart::baud() == 9600, "diagnostics expose actual configuration");
        for (unsigned i = 0; i < 700; ++i) Serial2.input.push_back('x');
        CydGpsUart::poll(100);
        check(CydGpsUart::receivedBytes() == 512 && Serial2.available() == 188, "one tick limits parsing work");
        CydGpsUart::poll(101);
        check(CydGpsUart::receivedBytes() == 700 && !Serial2.available(), "remaining input drains on the next tick");
        Gnss::reset();
        const char* s = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
        for (const char* p = s; *p; ++p) Serial2.input.push_back(*p);
        CydGpsUart::poll(102);
        check(Gnss::good() == 1 && Gnss::fresh(102), "UART bytes reach the production GNSS parser");
        bufferFail = true; uartFail = true;
        CydGpsUart::begin();
        check(!CydGpsUart::rxBufferReady() && !CydGpsUart::receiving(), "startup failure is visible to diagnostics");
    #endif
        std::puts("PASS: GPS UART startup and bounded receive contract");
    }
'''

with tempfile.TemporaryDirectory(prefix="cyd-gps-uart-") as folder:
    temp = Path(folder)
    for name, content in SHIMS.items():
        file = temp / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(textwrap.dedent(content))
    harness = temp / "harness.cpp"
    harness.write_text(textwrap.dedent(HARNESS))
    source = ROOT / "src/cyd_gps_uart.cpp"
    # The installed core supplies a weak C hook. Linking it alongside the app
    # catches both incorrect linkage and an app hook that does not override it.
    core = temp / "core.cpp"
    core.write_text('extern "C" __attribute__((weak)) void initVariant() {}\n')
    for profile in ("CYD_GPS_ELECROW",):
        exe = temp / profile
        cmd = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
               "-DARDUINO_ARCH_ESP32=1", "-DCYD=1", "-DCYD_GPS=1", f"-D{profile}=1", "-DCYD_GPS_RX=1",
               "-DCYD_GPS_BAUD=9600", "-DCYD_GPS_SERIAL=Serial2",
               f"-I{temp}", f"-I{ROOT / 'include'}", str(harness), str(core), str(ROOT / "src/gnss.cpp"), str(source)]
        subprocess.run(cmd + ["-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

    # Ordinary builds must not replace the core hook or introduce UART owners.
    obj = temp / "ordinary.o"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", f"-I{ROOT / 'include'}",
                    "-c", str(source), "-o", str(obj)], check=True)
    symbols = subprocess.run(["nm", "-g", str(obj)], check=True, text=True, capture_output=True).stdout
    assert "initVariant" not in symbols and "CydGpsUart" not in symbols, symbols
    print("PASS: ordinary build retains its default startup hook and console")
