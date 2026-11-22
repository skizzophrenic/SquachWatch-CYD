// sd_test -- the SD library over a folder (radio_stub/SD.h, sd_card.cpp),
// driven the way sd_log.cpp drives it.
#include <SD.h>

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
    // -- no root: no card (--shot / --selftest)
    cm0::sdSetRoot("");
    CHECK(!SD.begin(5, SPI, 4000000, "/sd", 5), "no root: begin refuses (no card)");
    CHECK(!SD.open("/x.log", FILE_APPEND), "no root: open fails");

    char tmpl[] = "/tmp/sd_test.XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) { std::perror("mkdtemp"); return 1; }
    const std::string root = dir;
    cm0::sdSetRoot(root);

    CHECK(SD.begin(5, SPI, 4000000, "/sd", 5), "root set: begin mounts");
    CHECK(SD.cardSize() > 0, "cardSize = the filesystem's size");

    // -- sd_log's append pattern: open, print, close, per row
    for (int i = 0; i < 3; i++) {
        File f = SD.open("/squachwatch-20261119.log", FILE_APPEND);
        CHECK(f, "append open");
        char line[32];
        snprintf(line, sizeof line, "row,%d\n", i);
        CHECK(f.print(line) == strlen(line), "print returns length");
        f.close();
        CHECK(!f, "closed File is false");
    }
    CHECK(slurp(root + "/squachwatch-20261119.log") == "row,0\nrow,1\nrow,2\n", "rows appended in order");
    {
        File f = SD.open("/squachwatch-20261119.log", FILE_READ);
        CHECK(f && f.size() == 18, "read open, size 18");
        CHECK(!strcmp(f.name(), "squachwatch-20261119.log"), "name() is the basename");
    }
    CHECK(SD.exists("/squachwatch-20261119.log") && !SD.exists("/nope.log"), "exists");
    {
        struct stat st;
        stat((root + "/squachwatch-20261119.log").c_str(), &st);
        CHECK((st.st_mode & 0777) == 0600, "created 0600");
    }
    {
        File w = SD.open("/w.txt", FILE_WRITE);
        w.print("abc"); w.close();
        File w2 = SD.open("/w.txt", FILE_WRITE);
        w2.print("z"); w2.close();
        CHECK(slurp(root + "/w.txt") == "z", "FILE_WRITE truncates");
    }

    // -- paths: only flat names at the root
    CHECK(!SD.open("/sub/x.log", FILE_APPEND), "a path with a subdirectory is refused");
    CHECK(!SD.open("/..", FILE_READ) && !SD.open("../x", FILE_APPEND), "'..' refused");
    CHECK(!SD.remove("/../etc"), "remove outside the folder refused");

    // -- links are not followed
    {
        const std::string victim = root + ".victim";
        FILE* v = fopen(victim.c_str(), "wb"); fputs("precious\n", v); fclose(v);
        CHECK(symlink(victim.c_str(), (root + "/squachwatch-link.log").c_str()) == 0, "link planted");
        File f = SD.open("/squachwatch-link.log", FILE_APPEND);
        CHECK(!f, "append through a symlink refused");
        CHECK(slurp(victim) == "precious\n", "link target untouched");
        unlink(victim.c_str());
    }

    // -- sd_log's wipe(): copy-assigning openNextFile() loop, collect, remove
    {
        File other = SD.open("/notes.txt", FILE_APPEND);
        other.print("keep me"); other.close();
        File d = SD.open("/");
        CHECK(d && d.isDirectory(), "open('/') is the directory");
        char victims[16][64];
        int n = 0, seen = 0;
        for (File f = d.openNextFile(); f && n < 16; f = d.openNextFile()) {
            seen++;
            const char* nm = f.name();
            if (strncmp(nm, "squachwatch-", 12) == 0) snprintf(victims[n++], sizeof victims[0], "/%s", nm);
            f.close();
        }
        d.close();
        CHECK(seen == 3, "listing: the 3 regular files (the symlink skipped)");
        CHECK(n == 1, "one squachwatch-* log to wipe");
        for (int i = 0; i < n; i++) SD.remove(victims[i]);
        CHECK(!SD.exists("/squachwatch-20261119.log") && SD.exists("/notes.txt"),
              "wipe removed the log, kept other files");
    }

    // cleanup
    if (DIR* d = opendir(dir)) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') unlink((root + "/" + e->d_name).c_str());
        closedir(d);
    }
    rmdir(dir);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
