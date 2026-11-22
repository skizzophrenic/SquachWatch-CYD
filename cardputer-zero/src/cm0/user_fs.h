// SquachWatch-CM0 -- whose files these are.
//
// The app runs as root under sudo today and as the user (file capabilities)
// from the launcher later. Either way, what it writes -- settings, the
// detection log, wardrive CSVs -- belongs to the PERSON: SUDO_USER under
// sudo, else the real user. One resolution, shared by every writer.
#pragma once
#include <string>
#include <sys/types.h>

namespace cm0 {

struct UserIds {
    std::string home;   // from the passwd entry, not $HOME; empty if none
    uid_t uid;
    gid_t gid;
    bool  sudo;         // running as root on SUDO_USER's behalf
};

// Resolved once. Under AT_SECURE (file capabilities) SUDO_USER is not
// trusted and the real uid's entry is used.
const UserIds& invokingUser();

// mkdir -p, 0700 on what it creates; under sudo a created directory is
// fchown'd to the user through an O_NOFOLLOW descriptor. True when `dir`
// exists as a real directory (not a symlink) afterwards.
bool makeUserDirs(const std::string& dir);

// The CALLING THREAD's filesystem identity becomes the user's (setfsgid,
// then setfsuid; Linux applies these per thread and glibc does not
// broadcast them). From then on the kernel checks this thread's file access
// as the user's: files it creates are theirs and a planted link to a root
// file fails, while sockets/capabilities and other threads are untouched,
// and an exec'd child (iw, ip) gets its fs ids reset to its euid. No-op
// unless sudo. Returns true when the switch happened.
bool useUserFsIds();

}   // namespace cm0
