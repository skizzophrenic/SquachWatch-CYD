// prefs_test -- the CM0 Preferences shim (sys_shim/Preferences.h) on the
// real prefs_store: round trip through a "restart", NVS typing, read-only
// opens, shared namespaces, atomic files, and the sim's file format.
#include <Preferences.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, what) do { g_checks++; if (cond) std::printf("ok  : %s\n", what); \
    else { g_failures++; std::printf("FAIL: %s\n", what); } } while (0)

// A process restart, as far as the store can tell: forget every namespace.
static void restart() { cm0::prefsNamespaces().clear(); }

static std::string slurp(const std::string& p) {
    std::string s;
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return s;
    char b[512]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    fclose(f);
    return s;
}

int main() {
    char tmpl[] = "/tmp/prefs_test.XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) { std::perror("mkdtemp"); return 1; }

    // -- in memory (persistence off, as --shot / --selftest)
    {
        cm0::prefsSetDir("");
        Preferences p;
        CHECK(p.begin("mem", false), "mem: begin");
        p.putUChar("x", 7);
        Preferences q;
        q.begin("mem", true);
        CHECK(q.getUChar("x", 0) == 7, "mem: a second object sees the first's write (one store per ns)");
        CHECK(cm0::prefsSaveCount() == 0, "mem: nothing written to disk");
        restart();
    }

    cm0::prefsSetDir(dir);
    // -- write, restart, read back: every type
    {
        Preferences p;
        CHECK(p.begin("settings", false), "disk: begin rw");
        p.putBool("meshtx", true);
        p.putUChar("tz", 19);
        p.putShort("cal", -321);
        p.putUShort("seq", 65000);
        p.putInt("off", -18000);
        p.putUInt("boots", 4000000000u);
        p.putULong("n", 123456u);
        CHECK(p.putString("phrase", "tall squachy, short fuse") == 24, "disk: putString returns length");
        const uint8_t blob[5] = {0x00, 0xFF, 0x10, 0xAB, 0x7F};
        CHECK(p.putBytes("ign", blob, 5) == 5, "disk: putBytes returns length");
        p.end();
    }
    restart();
    {
        Preferences p;
        CHECK(p.begin("settings", true), "restart: namespace exists, read-only begin ok");
        CHECK(p.getBool("meshtx") == true, "restart: bool");
        CHECK(p.getUChar("tz") == 19, "restart: uchar");
        CHECK(p.getShort("cal") == -321, "restart: short");
        CHECK(p.getUShort("seq") == 65000, "restart: ushort");
        CHECK(p.getInt("off") == -18000, "restart: int");
        CHECK(p.getUInt("boots") == 4000000000u, "restart: uint");
        CHECK(p.getULong("n") == 123456u, "restart: ulong");
        char s[64];
        CHECK(p.getString("phrase", s, sizeof s) == 24 && !strcmp(s, "tall squachy, short fuse"),
              "restart: string (spaces kept)");
        uint8_t b[8] = {0};
        CHECK(p.getBytesLength("ign") == 5 && p.getBytes("ign", b, sizeof b) == 5 &&
              b[0] == 0x00 && b[1] == 0xFF && b[3] == 0xAB && b[4] == 0x7F, "restart: bytes");
        // NVS typing: a uchar key read as uint is absent.
        CHECK(p.getUInt("tz", 99) == 99, "typed: uchar key through getUInt -> default");
        CHECK(p.isKey("tz") && !p.isKey("nope"), "isKey");
        CHECK(p.putUChar("tz", 1) == 0 && p.getUChar("tz") == 19, "read-only: put refused, value unchanged");
        char t[4];
        CHECK(p.getString("phrase", t, sizeof t) == 3 && !strcmp(t, "tal"), "getString truncates to maxLen-1");
    }
    // -- read-only open of a namespace never written: false (NVS_READONLY)
    {
        Preferences p;
        CHECK(!p.begin("pwrdiary", true), "read-only begin on a missing namespace fails");
        CHECK(p.begin("pwrdiary", false), "rw begin creates it");
    }
    // -- refusals that must not touch the stored value
    {
        Preferences p;
        p.begin("settings", false);
        const unsigned long before = cm0::prefsSaveCount();
        CHECK(p.putBytes("ign", "x", 0) == 0, "putBytes len 0 refused");
        CHECK(p.getBytesLength("ign") == 5, "...and the old blob is intact");
        CHECK(p.putString("phrase", "two\nlines") == 0, "string with a newline refused");
        CHECK(cm0::prefsSaveCount() == before, "refusals write nothing");
        CHECK(p.remove("cal") && !p.isKey("cal"), "remove");
    }
    restart();
    {
        Preferences p;
        p.begin("settings", true);
        CHECK(!p.isKey("cal") && p.getUChar("tz") == 19, "remove persisted, rest intact");
    }
    // -- the files: one per namespace, 0600, no temp left behind
    {
        const std::string f = std::string(dir) + "/settings.nvs";
        struct stat st;
        CHECK(stat(f.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600, "file settings.nvs, mode 0600");
        bool tmpLeft = false;
        if (DIR* d = opendir(dir)) {
            while (dirent* e = readdir(d)) if (strstr(e->d_name, ".tmp")) tmpLeft = true;
            closedir(d);
        }
        CHECK(!tmpLeft, "no .tmp file left after saves");
        const std::string text = slurp(f);
        CHECK(text.find("u tz 19\n") != std::string::npos &&
              text.find("s phrase tall squachy, short fuse\n") != std::string::npos,
              "format: the sim's '<type> <key> <value>' lines");
    }
    // -- a file the sim wrote (SQUACHSIM_NVS) loads; junk lines are skipped
    {
        const std::string f = std::string(dir) + "/clock.nvs";
        FILE* w = fopen(f.c_str(), "wb");
        fputs("b tzset 1\nu tz 7\ngarbage\nq bad 1\ns name Big Foot\r\n", w);
        fclose(w);
        restart();
        Preferences p;
        p.begin("clock", true);
        char s[32];
        p.getString("name", s, sizeof s);
        CHECK(p.getBool("tzset") && p.getUChar("tz") == 7 && !strcmp(s, "Big Foot") && !p.isKey("bad"),
              "sim file: loads, CRLF trimmed, unknown types skipped");
    }
    // -- namespace names cannot leave the directory
    {
        Preferences p;
        CHECK(!p.begin("../evil", false) || (p.putUChar("x", 1), true), "odd name: begin may succeed...");
        struct stat st;
        CHECK(stat((std::string(dir) + "/../evil.nvs").c_str(), &st) != 0, "...but nothing is written outside the dir");
        CHECK(!p.begin("sixteen_chars_xx", false), "NVS 15-char name limit");
    }

    // -- symlinks are never followed (root writes into the user's dir under
    //    sudo: a planted link must not redirect a write or a read)
    {
        const std::string victim = std::string(dir) + "/victim";
        FILE* v = fopen(victim.c_str(), "wb"); fputs("precious\n", v); fclose(v);
        const std::string tmpLink = std::string(dir) + "/settings.nvs.tmp";
        CHECK(symlink(victim.c_str(), tmpLink.c_str()) == 0, "symlink planted at settings.nvs.tmp");
        restart();
        Preferences p;
        p.begin("settings", false);
        CHECK(p.putUChar("tz", 20) == 1, "save with a planted temp link succeeds...");
        CHECK(slurp(victim) == "precious\n", "...and the link target is untouched");
        struct stat st;
        CHECK(lstat(tmpLink.c_str(), &st) != 0, "...the planted link is gone");
        const std::string nsLink = std::string(dir) + "/linked.nvs";
        FILE* w = fopen(victim.c_str(), "wb"); fputs("u secret 42\n", w); fclose(w);
        CHECK(symlink(victim.c_str(), nsLink.c_str()) == 0, "symlink planted as linked.nvs");
        restart();
        Preferences q;
        q.begin("linked", false);
        CHECK(!q.isKey("secret"), "a symlinked namespace file is not read");
        unlink(nsLink.c_str());
        unlink(victim.c_str());
    }
    // -- a store directory others can write into is refused
    {
        chmod(dir, 0777);
        restart();
        const unsigned long before = cm0::prefsSaveCount();
        Preferences p;
        p.begin("settings", false);
        p.putUChar("tz", 21);
        CHECK(cm0::prefsSaveCount() == before, "world-writable store dir: nothing saved");
        chmod(dir, 0700);
    }

    // cleanup
    if (DIR* d = opendir(dir)) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') unlink((std::string(dir) + "/" + e->d_name).c_str());
        closedir(d);
    }
    rmdir(dir);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
