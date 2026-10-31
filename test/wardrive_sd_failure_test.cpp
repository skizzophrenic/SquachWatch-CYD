// Real CYD writer, real filesystem and GNSS. Inject descriptor failures
// so partial headers/rows exercise production failure, accounting and rotation.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include "Arduino.h"
#include <esp_heap_caps.h>
#include "test_util.h"
static std::string card;
static int shortWriteAt = 0;
static unsigned opened = 0, closed = 0, fsOperations = 0;
// Pause a real callback at its observation clock read to interleave loop ticks
// deterministically, without copying the producer or exposing test-only APIs.
static size_t largestBlock = 110 * 1024;
static size_t captureHeap(uint32_t) { return largestBlock; }
static void (*duringCapture)() = nullptr;
static uint32_t captureMillis() {
    const uint32_t now = millis();
    if (duringCapture) {
        const auto callback = duringCapture;
        duringCapture = nullptr;
        callback();
    }
    return now;
}
#if defined(SQW_SD_ESP32_HOST)
static int lockDepth = 0;
static bool lockViolation = false;
namespace FramePush {
    void busLock() { ++lockDepth; }
    void busUnlock() { if (lockDepth != 1) lockViolation = true; --lockDepth; }
}
static void checkLocked() { ++fsOperations; if (lockDepth != 1) lockViolation = true; }
static std::string hostPath(const char* path) {
    if (std::string(path).compare(0, 4, "/sd/") != 0) lockViolation = true;
    return card + std::string(path).substr(3);
}
static int faultPathStat(const char* path, struct stat* st) {
    checkLocked();
    return stat(hostPath(path).c_str(), st);
}
#define ESP32 1
#else
static void checkLocked() { ++fsOperations; }
static std::string hostPath(const char* path) { return path; }
#endif
static bool failClose = false;
static bool failSync = false, failStat = false, failSeek = false, raceOpen = false;
static std::string racedPath;
static off_t faultSeek(int fd, off_t offset, int whence) {
    checkLocked();
    if (failSeek) { errno = EIO; return -1; }
    return lseek(fd, offset, whence);
}
static int faultSync(int fd) {
    checkLocked();
    if (failSync) { errno = EIO; return -1; }
    return fsync(fd);
}
static int faultStat(int fd, struct stat* st) {
    checkLocked();
    if (failStat) { errno = EIO; return -1; }
    return fstat(fd, st);
}
static int faultOpen(const char* vfsPath, int flags, mode_t mode = 0) {
    checkLocked();
    const std::string full = hostPath(vfsPath);
    const char* path = full.c_str();
    if (raceOpen) {
        raceOpen = false; racedPath = path;
        const int raced = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (raced >= 0) { write(raced, "prior capture", 13); close(raced); }
    }
    const int fd = open(path, flags, mode);
    if (fd >= 0) ++opened;
    return fd;
}
static int faultClose(int fd) {
    checkLocked(); ++closed;
    const int result = close(fd);
    if (failClose) { errno = EIO; return -1; }
    return result;
}
static ssize_t faultWrite(int fd, const void* p, size_t n) {
    checkLocked();
    if (shortWriteAt && --shortWriteAt == 0) return write(fd, p, n / 2);
    return write(fd, p, n);
}
#if defined(SQW_SD_ESP32_HOST)
#define stat(...) faultPathStat(__VA_ARGS__)
#endif
#define lseek faultSeek
#define fsync faultSync
#define fstat faultStat
#define open faultOpen
#define write faultWrite
#define close faultClose
#define heap_caps_get_largest_free_block captureHeap
#define millis captureMillis
#include "../src/wardrive_sd.cpp"
#undef millis
#undef heap_caps_get_largest_free_block
#undef stat
#undef lseek
#undef fsync
#undef fstat
#undef open
#undef write
#undef close

static uint32_t startWritten, startDropped;
static void nav(const char* body, uint32_t ms) {
    char buf[128]; unsigned char cs = 0;
    for (const char* p = body; *p; ++p) cs ^= *p;
    snprintf(buf, sizeof buf, "$%s*%02X", body, cs);
    Gnss::sentence(buf, ms);
}
static void fix(uint32_t ms) {
    SimClock::nowMs = ms;
    nav("GNRMC,120000.00,A,4000.0000,N,07400.0000,W,0,0,281026,,,A", ms);
    nav("GNGGA,120000.00,4000.0000,N,07400.0000,W,1,06,1.0,5.0,M,0.0,M,,", ms);
}
static void start() {
    Wardrive::setEnabled(false); Wardrive::sdTick(0, true);
    if (!card.empty()) std::filesystem::remove_all(card);
    char path[] = "/tmp/sqw-sd-failure-XXXXXX";
    card = mkdtemp(path); setenv("SQUACHSIM_SD", card.c_str(), 1);
    Gnss::reset(); fix(1000); largestBlock = 110 * 1024; shortWriteAt = 0; failClose = false; failSync = false; failStat = false; failSeek = false; raceOpen = false; racedPath.clear();
    startWritten = Wardrive::written(); startDropped = Wardrive::dropped();
    Wardrive::setEnabled(true); Wardrive::sdTick(1000, true);
}
static void note(uint8_t id = 7) {
    const uint8_t mac[] = {2, 3, 4, 5, 6, id};
    Wardrive::noteWifi(mac, "radio", 0, 6, -48);
}
static void flushAt(uint32_t ms) {
    SimClock::nowMs = ms; Wardrive::sdTick(ms, true);
    SimClock::nowMs = ms + 2001; Wardrive::sdTick(ms + 2001, true);
#if defined(SQW_SD_ESP32_HOST)
    if (lockDepth != 0) lockViolation = true;
#endif
}
static std::string read(const std::string& path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
static bool writeError(uint32_t ms) {
    return Wardrive::sdState(ms) == Wardrive::SdState::WRITE_ERROR;
}
static std::string captures() {
    std::string csv;
    for (const auto& entry : std::filesystem::directory_iterator(card)) csv += read(entry.path().string());
    return csv;
}
static void absentTick(uint32_t ms) {
#if !defined(SQW_SD_ESP32_HOST)
    unsetenv("SQUACHSIM_SD");
#endif
    Wardrive::sdTick(ms, false);
}
static void pauseDuringCapture() {
    largestBlock = 1024;
    Wardrive::sdTick(1600, true);
}
static unsigned captureStopOperations;
static bool reenableDuringCapture, removeDuringCapture;
static void stopDuringCapture() {
    captureStopOperations = fsOperations;
    if (removeDuringCapture) absentTick(1100);
    else { Wardrive::setEnabled(false); Wardrive::sdTick(1100, true); }
    if (reenableDuringCapture) {
        Wardrive::setEnabled(true);
        setenv("SQUACHSIM_SD", card.c_str(), 1);
        Wardrive::sdTick(1101, true);
    } else if (removeDuringCapture) absentTick(1101);
    else Wardrive::sdTick(1101, true);
    ck("in-flight shutdown waits before any filesystem operation", fsOperations == captureStopOperations);
}
int main() {
    SimClock::virtualTime = true;
#if defined(SQW_SD_RESERVE_AT_BOOT)
    Wardrive::sdBegin();
#endif
    suite("OFF drains observations accepted before the next loop tick");
    start(); note(7); Gnss::reset();
    Wardrive::setEnabled(false); Wardrive::sdTick(8000, true);
    ck("OFF persists an undrained observation using its original snapshot", Wardrive::written() == startWritten + 1 && captures().find("02:03:04:05:06:07,radio,") != std::string::npos && captures().find(",40.0000000,-74.0000000,5,4,") != std::string::npos);
    ck("successful OFF does not count the persisted row as dropped", Wardrive::dropped() == startDropped);
    const unsigned afterOff = fsOperations;
    Wardrive::sdTick(8001, true);
    ck("repeated OFF neither writes nor counts again", fsOperations == afterOff && Wardrive::written() == startWritten + 1 && Wardrive::dropped() == startDropped);

    suite("OFF drains both full radio queues through multiple batches");
    start();
    for (uint8_t id = 1; id <= 7; ++id) {
        note(id);
        const uint8_t mac[] = {4, 3, 4, 5, 6, id};
        Wardrive::noteBle(mac, "beacon", -51, true, 76);
    }
    Wardrive::setEnabled(false); Wardrive::sdTick(1100, true);
    ck("all fourteen accepted WiFi and BLE rows reach the card", Wardrive::written() == startWritten + 14 && Wardrive::dropped() == startDropped && captures().find("04:03:04:05:06:07,beacon,") != std::string::npos);

    suite("Media removal accounts buffered and undrained observations");
    start(); note(7); Wardrive::sdTick(1100, true); note(8);
    const unsigned beforeRemoval = fsOperations;
    absentTick(1200);
    ck("removal counts both the buffered row and queued observation", Wardrive::dropped() == startDropped + 2 && Wardrive::written() == startWritten);
    ck("removal never accesses the missing filesystem", fsOperations == beforeRemoval && captures().empty());
    absentTick(1201);
    ck("repeated missing-card ticks do not double count", Wardrive::dropped() == startDropped + 2 && fsOperations == beforeRemoval);
    setenv("SQUACHSIM_SD", card.c_str(), 1);
    fix(1300); Wardrive::sdTick(1300, true); note(7); flushAt(1400);
    ck("remount accepts a fresh re-observation of a discarded device", Wardrive::written() == startWritten + 1 && Wardrive::dropped() == startDropped + 2);

    suite("OFF preserves accepted work even while memory is paused");
    start(); duringCapture = pauseDuringCapture; note(7);
    ck("low heap pauses new admission before the accepted callback completes", Wardrive::sdState(1600) == Wardrive::SdState::LOW_MEMORY);
    Wardrive::setEnabled(false); Wardrive::sdTick(1700, true);
    ck("stop drains previously accepted rows despite the producer pause", Wardrive::written() == startWritten + 1 && Wardrive::dropped() == startDropped);

    suite("Delayed producers retain their old session until shutdown completes");
    for (bool reenable : {false, true}) {
        start(); reenableDuringCapture = reenable; removeDuringCapture = false;
        duringCapture = stopDuringCapture; note(7);
        Wardrive::sdTick(1200, true);
        ck("accepted in-flight observation is persisted once after quiescence", Wardrive::written() == startWritten + 1 && Wardrive::dropped() == startDropped && captures().find("02:03:04:05:06:07,") != std::string::npos);
        ck("completed OFF ends its session even after rapid reenable", std::string(Wardrive::sdFileName()).empty());
        Wardrive::setEnabled(true); Wardrive::sdTick(1300, true); note(8); flushAt(1400);
        ck("new session contains only its fresh observation", Wardrive::written() == startWritten + 2 && read(card + Wardrive::sdFileName()).find("02:03:04:05:06:07,") == std::string::npos && read(card + Wardrive::sdFileName()).find("02:03:04:05:06:08,") != std::string::npos);
    }
    suite("Delayed media-loss teardown cannot write after a quick remount");
    start(); note(8); Wardrive::sdTick(1050, true);
    reenableDuringCapture = true; removeDuringCapture = true;
    duringCapture = stopDuringCapture; note(7);
    const unsigned beforeDeferredRemoval = fsOperations;
    Wardrive::sdTick(1200, true); Wardrive::sdTick(1201, true);
    ck("latched media loss discards buffered and late accepted work exactly once", Wardrive::written() == startWritten && Wardrive::dropped() == startDropped + 2);
    ck("quick remount never writes discarded old-media observations", fsOperations == beforeDeferredRemoval && captures().empty());

    suite("Partial write failure and re-observation recovery");
    for (int failedCall : {1, 2}) { // partial header, then partial CSV row
        start(); shortWriteAt = failedCall; note(); flushAt(1100);
        const std::string failedName = Wardrive::sdFileName();
        const std::string damaged = read(card + failedName);
        ck("failed batch is dropped, never counted as written", Wardrive::written() == startWritten && Wardrive::dropped() == startDropped + 1);
        ck("write failure is visible even after GNSS goes stale", writeError(7000));
        fix(3200); note(); flushAt(3300);
        const std::string recoveredName = Wardrive::sdFileName();
        const std::string csv = read(card + recoveredName);
        ck("same device can recover immediately after failed write", Wardrive::written() == startWritten + 1);
        ck("a successful batch clears the write error", Wardrive::sdState(5301) == Wardrive::SdState::LOGGING);
        ck("recovery rotates away from partial file", recoveredName != failedName && read(card + failedName) == damaged);
        ck("recovery file has intact header and full observation", csv.find("WigleWifi-1.6,") == 0 && csv.find("02:03:04:05:06:07,radio,") != std::string::npos);
    }
    suite("Open failure remains visible until a real successful write");
    start(); std::filesystem::remove_all(card); note(); flushAt(1100);
    ck("unwritable mounted card is an error, not LOGGING", writeError(3101));
    std::filesystem::create_directory(card);
    Wardrive::sdTick(3102, true);
    ck("restoring a directory alone does not claim recovery", writeError(3102));
    fix(3200); note(); flushAt(3300);
    ck("open failure does not suppress the next observation", Wardrive::written() == startWritten + 1 && Wardrive::sdState(5301) == Wardrive::SdState::LOGGING);

    suite("Checked descriptor close failure");
    start(); failClose = true; note(); flushAt(1100);
    ck("descriptor close error cannot count the batch as written", Wardrive::written() == startWritten && Wardrive::dropped() == startDropped + 1 && writeError(3101));
    failClose = false; fix(3200); note(); flushAt(3300);
    ck("close failure permits a later real observation", Wardrive::written() == startWritten + 1);

    suite("Driver synchronization and metadata errors");
    start(); failSync = true; note(); flushAt(1100);
    const std::string unsynchronized = Wardrive::sdFileName();
    ck("full write lengths do not hide a driver sync failure", Wardrive::written() == startWritten && Wardrive::dropped() == startDropped + 1 && writeError(3101));
    failSync = false; fix(3200); note(); flushAt(3300);
    ck("sync failure permits recovery in a fresh file", Wardrive::written() == startWritten + 1 && unsynchronized != Wardrive::sdFileName());
    start(); failStat = true; note(); flushAt(1100);
    ck("metadata error prevents a false successful batch", Wardrive::written() == startWritten && Wardrive::dropped() == startDropped + 1 && writeError(3101));
    failStat = false; fix(3200); note(); flushAt(3300);
    ck("metadata failure releases the descriptor for recovery", Wardrive::written() == startWritten + 1);

    start(); failSeek = true; note(); flushAt(1100);
    ck("seek failure cannot write at an unverified offset", Wardrive::written() == startWritten && Wardrive::dropped() == startDropped + 1 && writeError(3101));
    failSeek = false; fix(3200); note(); flushAt(3300);
    ck("seek failure releases the descriptor for recovery", Wardrive::written() == startWritten + 1);

    suite("Collision after filename selection");
    start(); raceOpen = true; note(); flushAt(1100);
    ck("new capture creation cannot overwrite a racing file", !racedPath.empty() && read(racedPath) == "prior capture" && writeError(3101) && Wardrive::written() == startWritten);
    fix(3200); note(); flushAt(3300);
    ck("racing collision recovers without altering the prior capture", !racedPath.empty() && read(racedPath) == "prior capture" && Wardrive::written() == startWritten + 1);

    suite("A whole failed batch can be observed again");
    start(); shortWriteAt = 2; note(7); note(8); flushAt(1100);
    ck("each row in a partial batch is counted as dropped", Wardrive::written() == startWritten && Wardrive::dropped() == startDropped + 2 && writeError(3101));
    fix(3200); note(7); note(8); flushAt(3300);
    ck("both failed devices can recover inside the dedup window", Wardrive::written() == startWritten + 2);

    suite("Partial append rotates an established session");
    start(); note(7); flushAt(1100);
    const std::string established = Wardrive::sdFileName();
    fix(3200); shortWriteAt = 1; note(8); flushAt(3300);
    const std::string damagedAppend = read(card + established);
    ck("failed append preserves earlier written count", Wardrive::written() == startWritten + 1 && Wardrive::dropped() == startDropped + 1 && writeError(5301));
    fix(5400); note(8); flushAt(5500);
    ck("append recovery leaves the damaged session untouched", established != Wardrive::sdFileName() && read(card + established) == damagedAppend && Wardrive::written() == startWritten + 2);

    suite("A removed session cannot be recreated without a header");
    start(); note(7); flushAt(1100);
    const std::string removed = Wardrive::sdFileName();
    std::filesystem::remove(card + removed);
    fix(3200); note(8); flushAt(3300);
    ck("missing existing capture produces a visible error", writeError(5301) && Wardrive::written() == startWritten + 1 && !std::filesystem::exists(card + removed));
    fix(5400); note(8); flushAt(5500);
    ck("next observation creates a fresh capture with its header", Wardrive::written() == startWritten + 2 && read(card + Wardrive::sdFileName()).find("WigleWifi-1.6,") == 0);

    suite("Rapid sessions do not append into prior captures");
    start(); note(); flushAt(1100);
    const std::string firstName = Wardrive::sdFileName();
    const std::string firstCsv = read(card + firstName);
    Wardrive::setEnabled(false); Wardrive::sdTick(3102, true);
    fix(3200); Wardrive::setEnabled(true); Wardrive::sdTick(3200, true);
    note(8); flushAt(3300);
    ck("same UTC second restart gets a distinct filename", firstName != Wardrive::sdFileName());
    ck("previous session is byte-for-byte preserved", read(card + firstName) == firstCsv);
    ck("new session contains only its own device", read(card + Wardrive::sdFileName()).find("02:03:04:05:06:07,") == std::string::npos);

    suite("Media absence has its own state");
    unsetenv("SQUACHSIM_SD"); Wardrive::sdTick(5400, false);
    ck("media removal reports NO_CARD", Wardrive::sdState(5400) == Wardrive::SdState::NO_CARD);
    setenv("SQUACHSIM_SD", card.c_str(), 1);
    fix(5500); Wardrive::sdTick(5500, true); shortWriteAt = 1;
    note(9); flushAt(5600);
    unsetenv("SQUACHSIM_SD"); Wardrive::sdTick(7700, false);
    ck("NO_CARD takes precedence over the last write error", Wardrive::sdState(7700) == Wardrive::SdState::NO_CARD);
    setenv("SQUACHSIM_SD", card.c_str(), 1); Wardrive::sdTick(7800, true);
    ck("remount alone retains unresolved write failure", writeError(7800));
    Wardrive::setEnabled(false); Wardrive::sdTick(7801, true);
    ck("disabled state clears the session error", Wardrive::sdState(7801) == Wardrive::SdState::OFF);
    char tooSmall[8];
    ck("path truncation is rejected explicitly", !Wardrive::cardPath("/wigle-20261028-120000.csv", tooSmall, sizeof tooSmall) && errno == ENAMETOOLONG);
    ck("every opened descriptor is closed exactly once", opened == closed);
#if defined(SQW_SD_ESP32_HOST)
    ck("all ESP32 filesystem operations hold the frame lock", !lockViolation && lockDepth == 0);
#endif
    std::filesystem::remove_all(card);
    return report();
}
