// SquachWatch-CM0 -- whose files these are (see user_fs.h).
#include "user_fs.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <sys/auxv.h>
#include <sys/fsuid.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cm0 {

const UserIds& invokingUser() {
    static UserIds u = [] {
        UserIds r{std::string(), getuid(), getgid(), false};
        const bool secure = getauxval(AT_SECURE) != 0;
        if (geteuid() == 0 && !secure) {
            // sudo sets SUDO_USER; the passwd entry is the authority for the
            // home and ids, not $HOME (sudo may or may not have reset it).
            const char* su = getenv("SUDO_USER");
            if (su && *su && strcmp(su, "root") != 0)
                if (const struct passwd* pw = getpwnam(su)) {
                    r.home = pw->pw_dir; r.uid = pw->pw_uid; r.gid = pw->pw_gid; r.sudo = true;
                    return r;
                }
        }
        if (const struct passwd* pw = getpwuid(getuid())) {
            r.home = pw->pw_dir; r.uid = pw->pw_uid; r.gid = pw->pw_gid;
        }
        return r;
    }();
    return u;
}

bool makeUserDirs(const std::string& dir) {
    const UserIds& u = invokingUser();
    size_t pos = 0;
    while (pos <= dir.size()) {
        size_t nx = dir.find('/', pos);
        if (nx == std::string::npos) nx = dir.size();
        const std::string cur = dir.substr(0, nx);
        pos = nx + 1;
        if (cur.empty()) continue;
        if (mkdir(cur.c_str(), 0700) == 0) {
            if (u.sudo) {
                const int fd = open(cur.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (fd < 0) return false;
                const bool ok = fchown(fd, u.uid, u.gid) == 0;
                close(fd);
                if (!ok) return false;
            }
        } else if (errno != EEXIST) return false;
    }
    struct stat st;
    return lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool useUserFsIds() {
    const UserIds& u = invokingUser();
    if (!u.sudo) return false;
    // gid first: once the fs uid is not 0 the thread may lose the right to
    // pick an arbitrary fs gid. Both calls return the previous value; read
    // back with an invalid id to confirm.
    setfsgid(u.gid);
    setfsuid(u.uid);
    const bool ok = (uid_t)setfsuid((uid_t)-1) == u.uid && (gid_t)setfsgid((gid_t)-1) == u.gid;
    std::fprintf(stderr, "[fs] file access as uid %u gid %u on the UI thread%s\n",
                 (unsigned)u.uid, (unsigned)u.gid, ok ? "" : " -- FAILED");
    return ok;
}

}   // namespace cm0
