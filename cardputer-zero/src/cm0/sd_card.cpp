// SquachWatch-CM0 -- the SD library over a folder (see radio_stub/SD.h).
#include "SD.h"
#include "user_fs.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

SDClass SD;

namespace cm0 {

struct SdHandle {
    int  fd = -1;          // a file
    DIR* dir = nullptr;    // or the root directory
    int  rootFd = -1;      // for openNextFile (owned by `dir` when set)
    std::string name;
    ~SdHandle() {
        if (fd >= 0) ::close(fd);
        if (dir) closedir(dir);   // closes rootFd too
    }
};

namespace {
std::string s_root;
bool s_loggedFail = false;

void failOnce(const char* what, const std::string& p, int e) {
    if (s_loggedFail) return;
    s_loggedFail = true;
    std::fprintf(stderr, "[sd] %s %s: %s\n", what, p.c_str(), strerror(e));
}

// The card root, never through a symlink. -1 when there is no card.
int openRoot() {
    if (s_root.empty()) return -1;
    const int fd = ::open(s_root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) failOnce("cannot open card folder", s_root, errno);
    return fd;
}

// "/name" or "name" -> "name"; anything that is not one plain component at
// the card's root -> "".
std::string leaf(const char* path) {
    if (!path) return std::string();
    if (*path == '/') path++;
    std::string n(path);
    if (n.empty() || n.size() > 255 || n == "." || n == ".." ||
        n.find('/') != std::string::npos) return std::string();
    return n;
}
}   // namespace

void sdSetRoot(const std::string& dir) { s_root = dir; }
const std::string& sdRoot() { return s_root; }

std::string sdDefaultRoot() {
    const char* env = getauxval(AT_SECURE) ? nullptr : getenv("SQUACHWATCH_SD");
    const UserIds& u = invokingUser();
    const std::string dir = (env && *env) ? std::string(env)
                          : (u.home.empty() ? std::string() : u.home + "/SquachWatch");
    if (dir.empty()) return dir;
    if (!makeUserDirs(dir)) { failOnce("cannot create card folder", dir, errno); return std::string(); }
    return dir;
}

}   // namespace cm0

// ------------------------------------------------------------------ File

File::operator bool() const { return _h && (_h->fd >= 0 || _h->dir); }

size_t File::size() const {
    if (!_h || _h->fd < 0) return 0;
    struct stat st;
    return fstat(_h->fd, &st) == 0 ? (size_t)st.st_size : 0;
}

const char* File::name() const { return _h ? _h->name.c_str() : ""; }

bool File::isDirectory() const { return _h && _h->dir; }

size_t File::write(const uint8_t* data, size_t len) {
    if (!_h || _h->fd < 0 || !data) return 0;
    size_t off = 0;
    while (off < len) {
        const ssize_t n = ::write(_h->fd, data + off, len - off);
        if (n < 0) { if (errno == EINTR) continue; break; }
        off += (size_t)n;
    }
    return off;
}

size_t File::print(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }

int File::read() {
    if (!_h || _h->fd < 0) return -1;
    uint8_t b;
    return ::read(_h->fd, &b, 1) == 1 ? b : -1;
}

size_t File::read(uint8_t* buf, size_t len) {
    if (!_h || _h->fd < 0 || !buf) return 0;
    for (;;) {
        const ssize_t n = ::read(_h->fd, buf, len);
        if (n < 0 && errno == EINTR) continue;
        return n > 0 ? (size_t)n : 0;
    }
}

bool File::seek(uint32_t pos) {
    return _h && _h->fd >= 0 && lseek(_h->fd, (off_t)pos, SEEK_SET) == (off_t)pos;
}

size_t File::println(const char* s) { return print(s) + write((const uint8_t*)"\r\n", 2); }

int File::available() {
    if (!_h || _h->fd < 0) return 0;
    const off_t cur = lseek(_h->fd, 0, SEEK_CUR);
    struct stat st;
    if (cur < 0 || fstat(_h->fd, &st) != 0) return 0;
    return st.st_size > cur ? (int)(st.st_size - cur) : 0;
}

// The next regular file in the root (links, directories and the like are
// skipped -- the firmware only ever looks for its own log files here).
File File::openNextFile() {
    if (!_h || !_h->dir) return File();
    while (struct dirent* e = readdir(_h->dir)) {
        if (e->d_name[0] == '.') continue;
        struct stat st;
        if (fstatat(_h->rootFd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) continue;
        const int fd = openat(_h->rootFd, e->d_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) continue;
        auto h = std::make_shared<cm0::SdHandle>();
        h->fd = fd;
        h->name = e->d_name;
        return File(h);
    }
    return File();
}

// --------------------------------------------------------------- SDClass

bool SDClass::begin(int, SPIClass&, uint32_t, const char*, uint8_t) {
    const int fd = cm0::openRoot();
    if (fd < 0) { _mounted = false; return false; }
    ::close(fd);
    _mounted = true;
    std::fprintf(stderr, "[sd] card = %s\n", cm0::sdRoot().c_str());
    return true;
}

uint64_t SDClass::cardSize() const {
    const int fd = cm0::openRoot();
    if (fd < 0) return 0;
    struct statvfs v;
    const uint64_t sz = fstatvfs(fd, &v) == 0 ? (uint64_t)v.f_blocks * v.f_frsize : 0;
    ::close(fd);
    return sz;
}

uint64_t SDClass::usedBytes() const {
    const int fd = cm0::openRoot();
    if (fd < 0) return 0;
    struct statvfs v;
    const uint64_t used = fstatvfs(fd, &v) == 0 ? (uint64_t)(v.f_blocks - v.f_bfree) * v.f_frsize : 0;
    ::close(fd);
    return used;
}

bool SDClass::exists(const char* path) const {
    if (!_mounted) return false;
    const std::string n = cm0::leaf(path);
    if (n.empty()) return false;
    const int fd = cm0::openRoot();
    if (fd < 0) return false;
    struct stat st;
    const bool ok = fstatat(fd, n.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0;
    ::close(fd);
    return ok;
}

bool SDClass::remove(const char* path) {
    if (!_mounted) return false;
    const std::string n = cm0::leaf(path);
    if (n.empty()) return false;
    const int fd = cm0::openRoot();
    if (fd < 0) return false;
    const bool ok = unlinkat(fd, n.c_str(), 0) == 0;
    ::close(fd);
    return ok;
}

File SDClass::open(const char* path, const char* mode) {
    if (!_mounted || !path || !mode) return File();
    const int root = cm0::openRoot();
    if (root < 0) return File();
    if (!strcmp(path, "/") || !*path) {
        DIR* d = fdopendir(root);
        if (!d) { ::close(root); return File(); }
        auto h = std::make_shared<cm0::SdHandle>();
        h->dir = d;
        h->rootFd = root;
        h->name = "/";
        return File(h);
    }
    const std::string n = cm0::leaf(path);
    int flags;
    switch (mode[0]) {
        case 'r': flags = O_RDONLY; break;
        case 'a': flags = O_WRONLY | O_CREAT | O_APPEND; break;
        case 'w': flags = O_WRONLY | O_CREAT | O_TRUNC; break;
        default:  flags = -1; break;
    }
    // "r+" / "w+" / "a+": the same, read and write (main.cpp rewrites
    // /gps-status.log's placeholders in place with "r+").
    if (flags >= 0 && strchr(mode, '+')) flags = (flags & ~(O_RDONLY | O_WRONLY)) | O_RDWR;
    if (n.empty() || flags < 0) { ::close(root); return File(); }
    const int fd = openat(root, n.c_str(), flags | O_NOFOLLOW | O_CLOEXEC, 0600);
    ::close(root);
    if (fd < 0) return File();
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { ::close(fd); return File(); }
    // Normally the UI thread already creates as the user (useUserFsIds);
    // this keeps a file theirs even if that switch did not happen.
    const cm0::UserIds& u = cm0::invokingUser();
    if (u.sudo && st.st_uid != u.uid) (void)fchown(fd, u.uid, u.gid);
    auto h = std::make_shared<cm0::SdHandle>();
    h->fd = fd;
    h->name = n;
    return File(h);
}
