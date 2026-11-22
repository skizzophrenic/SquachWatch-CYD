// SquachWatch-CM0 -- the Preferences file store (see prefs_store.h).
#include "prefs_store.h"
#include "user_fs.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string s_dir;
unsigned long s_saves = 0;
bool s_loggedFail = false;

void failOnce(const char* what, const std::string& path, int err) {
    if (s_loggedFail) return;
    s_loggedFail = true;
    std::fprintf(stderr, "[prefs] %s %s: %s (settings will not be saved)\n",
                 what, path.c_str(), strerror(err));
}

// A namespace name becomes a file name: NVS names are short identifiers,
// but nothing here should be able to walk out of the directory.
bool safeName(const std::string& ns) {
    if (ns.empty() || ns.size() > 32) return false;
    for (char c : ns)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

// Under sudo this code is root working inside a directory the user owns,
// so nothing here goes through a path the user could swap for a symlink:
// ownership is set on open descriptors (fchown), never chown(path); the
// store directory is opened O_NOFOLLOW and checked before each use; files
// are created O_EXCL|O_NOFOLLOW and renamed relative to that descriptor.

// The uid the store must belong to: the person's (user_fs.h).
uid_t expectedUid() { return cm0::invokingUser().uid; }

// The store directory as a descriptor, refusing a symlink, a directory not
// owned by the expected user, or one others can write into. -1 on refusal
// (logged once).
int openStoreDir() {
    const int dfd = open(s_dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dfd < 0) { failOnce("cannot open", s_dir, errno); return -1; }
    struct stat st;
    if (fstat(dfd, &st) != 0 || st.st_uid != expectedUid() || (st.st_mode & 022)) {
        close(dfd);
        failOnce("refusing (wrong owner or group/world-writable)", s_dir, EPERM);
        return -1;
    }
    return dfd;
}

}   // namespace

namespace cm0 {

std::string prefsDefaultDir() {
    const bool secure = getauxval(AT_SECURE) != 0;
    const char* env = secure ? nullptr : getenv("SQUACHWATCH_STATE");
    const UserIds& u = invokingUser();
    std::string dir = (env && *env) ? std::string(env)
                    : (u.home.empty() ? std::string() : u.home + "/.local/state/squachwatch/nvs");
    if (dir.empty()) return dir;
    if (!makeUserDirs(dir)) { failOnce("cannot create", dir, errno); return std::string(); }
    return dir;
}

void prefsSetDir(const std::string& dir) { s_dir = dir; }
const std::string& prefsDir() { return s_dir; }
unsigned long prefsSaveCount() { return s_saves; }

bool prefsLoad(const std::string& ns, std::string& blob) {
    blob.clear();
    if (s_dir.empty() || !safeName(ns)) return false;
    const int dfd = openStoreDir();
    if (dfd < 0) return false;
    const int fd = openat(dfd, (ns + ".nvs").c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    close(dfd);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return false; }
    char chunk[1024];
    for (;;) {
        const ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        blob.append(chunk, (size_t)n);
    }
    close(fd);
    return true;
}

bool prefsSave(const std::string& ns, const std::string& blob) {
    if (s_dir.empty() || !safeName(ns)) return false;
    const std::string name = ns + ".nvs";
    const std::string tmp  = name + ".tmp";
    const int dfd = openStoreDir();
    if (dfd < 0) return false;
    // A stale temp (a crash mid-save) -- or anything planted under its
    // name -- goes first; then the temp is created fresh, never followed.
    (void)unlinkat(dfd, tmp.c_str(), 0);
    const int fd = openat(dfd, tmp.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) { const int e = errno; close(dfd); failOnce("cannot write", s_dir + "/" + tmp, e); return false; }
    auto fail = [&](const char* what) {
        const int e = errno;
        close(fd); unlinkat(dfd, tmp.c_str(), 0); close(dfd);
        failOnce(what, s_dir + "/" + tmp, e);
        return false;
    };
    const UserIds& u = invokingUser();
    if (u.sudo && fchown(fd, u.uid, u.gid) != 0) return fail("chown failed");
    size_t off = 0;
    while (off < blob.size()) {
        const ssize_t n = write(fd, blob.data() + off, blob.size() - off);
        if (n < 0) { if (errno == EINTR) continue; return fail("write failed"); }
        off += (size_t)n;
    }
    if (fsync(fd) != 0) return fail("fsync failed");
    if (close(fd) != 0) { const int e = errno; unlinkat(dfd, tmp.c_str(), 0); close(dfd);
                          failOnce("close failed", s_dir + "/" + tmp, e); return false; }
    if (renameat(dfd, tmp.c_str(), dfd, name.c_str()) != 0) {
        const int e = errno; unlinkat(dfd, tmp.c_str(), 0); close(dfd);
        failOnce("rename failed", s_dir + "/" + name, e); return false;
    }
    (void)fsync(dfd);   // the rename is only durable once the directory is
    close(dfd);
    s_saves++;
    return true;
}

}   // namespace cm0
