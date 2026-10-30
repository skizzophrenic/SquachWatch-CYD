// Only sdWipe() needs Arduino File after the descriptor writer conversion.
// Wipe is outside this test; filesystem writes use real POSIX calls instead.
#pragma once
class File {
public:
    explicit operator bool() const { return false; }
    File openNextFile() { return {}; }
    const char* name() const { return ""; }
    void close() {}
};
struct SdWipeShim {
    File open(const char*) { return {}; }
    bool remove(const char*) { return false; }
};
inline SdWipeShim SD;
