// SquachWatch-CM0 -- the Arduino SD library, backed by a folder.
//
// The Cardputer Zero has no separate card: its microSD is the root
// filesystem. So "the card" is a directory (`~/SquachWatch/` by default,
// set by sdSetRoot() from the live harness) and the card's "/" is that
// directory. Only flat names at the root are reachable -- which is all the
// firmware writes (sd_log.cpp: /squachwatch-YYYYMMDD.log). A path with
// another '/', or "..", is refused; nothing follows a symlink.
//
// The surface sd_log.cpp uses on the plain (non-SD_MMC) path: begin(),
// cardSize(), exists(), remove(), open() with FILE_READ/FILE_APPEND/
// FILE_WRITE (and "r+", main.cpp's /gps-status.log rewrite since v1.43),
// and "/" as a directory; File's bool, size, print, println, write, read,
// seek, name, close and openNextFile. File copies share one open handle (the
// firmware copy-assigns in its openNextFile() loop), closed with the last
// copy or close().
//
// With no root set (--shot, --selftest) begin() refuses: no card, as
// before. Wardrive CSVs do not come through here -- the firmware's
// non-ESP32 branch fopen()s under $SQUACHSIM_SD, which the harness points
// at the same folder.
#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <SPI.h>

#define FILE_READ   "r"
#define FILE_WRITE  "w"
#define FILE_APPEND "a"

namespace cm0 {
// The card folder (empty = no card). Before setup(): SdLog::begin() runs there.
void sdSetRoot(const std::string& dir);
const std::string& sdRoot();
// The live default: $SQUACHWATCH_SD (unless AT_SECURE) else ~/SquachWatch
// of the invoking user, created 0700. Empty when it cannot be made.
std::string sdDefaultRoot();
struct SdHandle;   // sd_card.cpp
}

class File {
public:
    File() {}
    operator bool() const;
    size_t size() const;
    void close() { _h.reset(); }
    const char* name() const;
    size_t write(const uint8_t* data, size_t len);
    size_t write(const char* data, size_t len) { return write((const uint8_t*)data, len); }
    size_t write(uint8_t b) { return write(&b, 1); }
    size_t print(const char* s);
    int read();
    size_t read(uint8_t* buf, size_t len);
    bool seek(uint32_t pos);
    size_t println(const char* s);   // s and "\r\n", as Arduino's Print writes it
    int available();
    File openNextFile();
    bool isDirectory() const;

private:
    friend class SDClass;
    explicit File(std::shared_ptr<cm0::SdHandle> h) : _h(std::move(h)) {}
    std::shared_ptr<cm0::SdHandle> _h;
};

class SDClass {
public:
    // The real Arduino signature; nothing here is SPI.
    bool begin(int csPin = -1, SPIClass& spi = SPI, uint32_t clock = 4000000,
               const char* path = "/sd", uint8_t files = 5);
    bool end() { bool was = _mounted; _mounted = false; return was; }
    bool isMounted() const { return _mounted; }
    uint64_t cardSize() const;
    // The firmware's SD LS / SDTRY dev commands (main.cpp, SQW_DEVTOOLS) --
    // unreachable here, but compiled: bytes used on the card folder's
    // filesystem, and the ESP32 core's card type (2 = CARD_SDHC) when mounted.
    uint64_t usedBytes() const;
    uint8_t cardType() const { return _mounted ? 2 : 0; }
    bool exists(const char* path) const;
    bool remove(const char* path);
    bool mkdir(const char* path, bool parents = true) { (void)path; (void)parents; return false; }
    File open(const char* path, const char* mode = FILE_READ);

private:
    bool _mounted = false;
};

extern SDClass SD;
