// SquachWatch-CM0 -- wifi_mon.cpp: the USB adapter's monitor vif.
//
// See wifi_mon.h for the contract. Everything here is Linux-side; the
// firmware side (detection.cpp) is unchanged.
//
// Pieces:
//  1. findAdapter(): SQUACH_WIFI_IF, else the first Wi-Fi netdev whose
//     parent is a USB device. The CM0's onboard bcm43439 is SDIO (stays
//     managed for NTP/OTA); the sniffer's adapter is the USB one.
//  2. the monitor interface: a `sqwmon0` vif on the adapter (iw dev <if>
//     interface add). Drivers that will not take a second vif get the
//     fallback: the interface itself is converted to monitor, and its
//     original type and link state are restored on exit. The name is ours
//     alone, so the leftover-from-a-crash cleanup never touches another
//     tool's `mon0`.
//  3. the feed thread: nl80211 channel sets (the firmware asks through
//     esp_wifi_set_channel), AF_PACKET reads of raw 802.11 frames, a
//     radiotap walk for RSSI, the actual channel and the FCS flag, the
//     firmware's filter mask, and a queue the UI thread drains
//     (wifiMonPump) into the registered rx callback.
//
// Reference for the formats below:
//  - radiotap: https://www.radiotap.org/fields/defined (field numbers,
//    sizes, alignment) and the presence-word / namespace rules on
//    radiotap.org's front page. Channel = u16 frequency in MHz + u16 flags.
//  - mac80211 tags monitor frames skb->protocol = ETH_P_802_2
//    (net/mac80211/rx.c, ieee80211_rx_monitor), so the packet socket must
//    use ETH_P_ALL; a socket bound to any other protocol hears nothing.
//  - nl80211 is a generic-netlink family: its id is resolved at runtime
//    (CTRL_CMD_GETFAMILY "nl80211"), and every message carries a
//    genlmsghdr. WIPHY_FREQ is in MHz. Constants from <linux/nl80211.h>.
#include "wifi_mon.h"

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/auxv.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace {

using cm0::WifiScanAp;

const char* const MON_IF = "sqwmon0";

// ------------------------------------------------------------------ state

struct MonState {
    std::string adapter;      // the physical interface found
    std::string monIf;        // the monitor interface that is read
    bool        createdVif = false;  // monIf is a vif we added
    bool        converted = false;   // monIf == adapter (the fallback path)
    std::string origType;     // to restore after a convert ("managed")
    bool        origUp = false;      // the adapter's link state before start
    bool        adapterDowned = false;  // vif path: we took the adapter down
    int         sock = -1;
    int         ifindex = -1;
    int         nl = -1;      // the generic-netlink socket for channel sets
    uint16_t    nlFamily = 0; // nl80211's runtime family id (0 = unresolved)
    uint32_t    nlSeq = 1;
    pthread_t   thread = 0;
    bool        threadStarted = false;
    unsigned long long framesQueued = 0, framesDropped = 0, framesDelivered = 0;
};
MonState s_mon;

std::atomic<bool>     s_running{false};
std::atomic<wifi_promiscuous_rx_cb_t> s_rxCb{nullptr};
std::atomic<uint8_t>  s_wantChan{1};
std::atomic<uint8_t>  s_appliedChan{1};
std::atomic<bool>     s_promisc{false};
std::atomic<uint32_t> s_filterMask{0};
// Channels the driver refused (feed thread only; it is the only writer and
// reader). Cleared on each start.
bool s_setFailed[256] = {false};
// The refusal is logged once per start, not per channel. (The mt7921u
// refusing the whole plan was EBUSY from the adapter's own managed interface
// still being up beside the vif — see wifiMonStart.)
bool s_setRefusedLogged = false;
// The last nl80211 SET_WIPHY error (negative errno; feed thread only), for
// the refusal log.
int s_lastSetErr = 0;

// The hand-over queue: the feed thread pushes, the UI thread pumps. Each
// entry is already laid out as the callback's buffer:
// [wifi_promiscuous_pkt_t][frame (+4 zero FCS bytes when absent)].
// The payload pointer is set at delivery, so the vector may move freely.
struct QFrame {
    std::vector<uint8_t> buf;
    uint8_t type;   // 0 mgmt / 1 ctl / 2 data (802.11 frame type)
};
std::mutex         s_qMu;
std::deque<QFrame> s_q;
const size_t       Q_CAP = 1024;   // ~30 ms of a busy channel; beyond it, drop new

// ------------------------------------------------------------------ helpers

// ---- running iw / ip without a shell ----
//
// This process may hold CAP_NET_ADMIN/CAP_NET_RAW (`make caps`) and passes
// them to its children through the ambient set, so no child may be chosen
// or shaped by the caller: no shell (popen would run /bin/sh -c with the
// caller's PATH and any metacharacters in an interface name), the tool
// found only in fixed system directories and only if root-owned and not
// group/other-writable, an argv array, and a fixed minimal environment.

// A Linux interface name we are willing to put in an argv: 1-15 chars of
// [A-Za-z0-9_.-], not "." / "..". (No '/', so it cannot walk /sys paths.)
bool validIfName(const std::string& s) {
    if (s.empty() || s.size() > 15 || s == "." || s == "..") return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
            return false;
    return true;
}

std::string toolPath(const char* name) {
    static const char* const DIRS[] = { "/usr/sbin", "/usr/bin", "/sbin", "/bin" };
    for (const char* d : DIRS) {
        const std::string p = std::string(d) + "/" + name;
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 &&
            !(st.st_mode & (S_IWGRP | S_IWOTH)) && (st.st_mode & S_IXUSR))
            return p;
    }
    return "";
}

// fork + execve(argv[0] resolved by toolPath), stdout captured into `out`
// (if non-null), stdin/stderr to /dev/null. Returns the exit status, -1 when
// the tool is missing or the spawn failed. Everything the child touches is
// prepared before fork(): after it, only async-signal-safe calls (this
// process has threads).
int runTool(const std::vector<std::string>& args, std::string* out = nullptr) {
    if (args.empty()) return -1;
    const std::string path = toolPath(args[0].c_str());
    if (path.empty()) return -1;
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    static char envPath[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin";
    static char envLang[] = "LC_ALL=C";
    char* const envp[] = { envPath, envLang, nullptr };
    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC) != 0) return -1;
    const pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return -1; }
    if (pid == 0) {
        const int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) { dup2(dn, 0); dup2(dn, 2); }
        dup2(pfd[1], 1);
        execve(path.c_str(), argv.data(), envp);
        _exit(127);
    }
    close(pfd[1]);
    char buf[512];
    ssize_t n;
    while ((n = read(pfd[0], buf, sizeof buf)) != 0) {
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (out) out->append(buf, (size_t)n);
    }
    close(pfd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool pathExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// /sys/class/net/<if>/device resolves through the platform tree to
// .../usbX/... for a USB-attached adapter and .../mmc... for the onboard
// SDIO chip. USB parent = the sniffer's adapter. The device link is a
// *relative* symlink ("../../../1-1.2:1.0" on the CM0), so a single
// readlink misses the usb controller ancestor; realpath walks the chain.
bool hasUsbParent(const std::string& ifname) {
    char buf[PATH_MAX];
    char* r = realpath(("/sys/class/net/" + ifname + "/device").c_str(), buf);
    if (!r) return false;
    return std::string(buf).find("/usb") != std::string::npos;
}

// SQUACH_WIFI_IF wins (it is how the desktop's built-in Wi-Fi is
// addressed for testing); otherwise the first Wi-Fi netdev with a USB
// parent. Virtual interfaces have no device link, so they drop out.
//
// SQUACH_WIFI_IF is a developer override: it is ignored when the process
// runs privileged by file capabilities or setuid (AT_SECURE), and must be a
// valid interface name either way -- it ends up in iw/ip argv.
std::string findAdapter() {
    if (const char* env = std::getenv("SQUACH_WIFI_IF")) {
        if (getauxval(AT_SECURE)) {
            std::fprintf(stderr, "[wifi_mon] SQUACH_WIFI_IF ignored in a privileged (setcap) process\n");
        } else if (!validIfName(env)) {
            std::fprintf(stderr, "[wifi_mon] SQUACH_WIFI_IF is not a valid interface name\n");
            return "";
        } else {
            return pathExists(std::string("/sys/class/net/") + env) ? env : "";
        }
    }
    DIR* d = opendir("/sys/class/net");
    if (!d) return "";
    std::string found;
    struct dirent* e;
    while ((e = readdir(d)) && found.empty()) {
        std::string n = e->d_name;
        if (!validIfName(n) || n == MON_IF) continue;
        if (!pathExists("/sys/class/net/" + n + "/phy80211")) continue;
        if (hasUsbParent(n)) found = n;
    }
    closedir(d);
    return found;
}

// IFF_UP from /sys/class/net/<if>/flags (false when the interface is gone).
bool ifUp(const std::string& ifname) {
    bool up = false;
    if (FILE* f = std::fopen(("/sys/class/net/" + ifname + "/flags").c_str(), "r")) {
        unsigned flags = 0;
        if (std::fscanf(f, "%x", &flags) == 1) up = (flags & 0x1) != 0;
        std::fclose(f);
    }
    return up;
}

// ---- adapter loss (2026-11-22) ----
//
// The USB adapter can drop off the bus mid-run (seen on the CM0: a USB
// disconnect + re-enumeration, "Cannot enable. Maybe the USB cable is bad?",
// and an mt7921u oops). The monitor vif dies with the device; the adapter
// comes back as a fresh netdev. The bound packet socket then reports
// ENETDOWN at most once and goes quiet for good, so the feed thread checks
// the vif itself: alive = the name still resolves to the index the socket
// was bound to (a vif re-created under the same name has a new index).
bool linkAlive(const std::string& ifname, int ifindex) {
    if (ifname.empty() || ifindex <= 0) return false;
    return (int)if_nametoindex(ifname.c_str()) == ifindex;
}

// recv/poll errors that mean "look at the link now" (not a normal empty read).
bool lossErrno(int e) { return e == ENETDOWN || e == ENODEV || e == ENXIO; }

// Non-root runs: with `setcap cap_net_admin,cap_net_raw+ep squachd` the
// process has both caps permitted, but the `iw`/`ip` children it popen()s
// would not -- file caps do not cross exec. Moving them into the
// inheritable and then the ambient set makes the children inherit them.
// Harmless no-op as root or without the file caps.
void raiseAmbientNetCaps() {
    if (geteuid() == 0) return;
    struct __user_cap_header_struct hdr = { _LINUX_CAPABILITY_VERSION_3, 0 };
    struct __user_cap_data_struct data[2];
    memset(data, 0, sizeof data);
    if (syscall(SYS_capget, &hdr, data) != 0) return;
    const int caps[2] = { CAP_NET_ADMIN, CAP_NET_RAW };
    bool any = false;
    for (int c : caps)
        if (data[0].permitted & (1u << c)) { data[0].inheritable |= (1u << c); any = true; }
    if (!any) return;
    if (syscall(SYS_capset, &hdr, data) != 0) return;
    for (int c : caps)
        if (data[0].permitted & (1u << c))
            prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, c, 0, 0);
}

// ------------------------------------------------------------- channels

// The channel's centre frequency in MHz: 2.4 GHz 1-13 (ch 14 is a
// Japan-only channel this app never hops), and the 5 GHz 20 MHz plan in its
// three blocks 36-64, 100-144, 149-177. 0 = not a channel this sniffer
// tunes to: stay where you are.
uint32_t chanToMHz(uint8_t ch) {
    if (ch >= 1 && ch <= 13)    return 2407u + 5u * ch;
    if (ch >= 36 && ch <= 64)   return 5180u + 5u * (ch - 36);
    if (ch >= 100 && ch <= 144) return 5490u + 5u * (ch - 100);
    if (ch >= 149 && ch <= 177) return 5745u + 5u * (ch - 149);
    return 0;
}

// The inverse, for the radiotap channel field (MHz). A frequency outside
// the plan (6 GHz, a gap between 5 GHz blocks) carries no channel the
// firmware knows, so the applied one stands.
uint8_t frameChannel(uint32_t mhz, uint8_t applied) {
    if (mhz >= 2412u && mhz <= 2472u && (mhz - 2407u) % 5u == 0)
        return (uint8_t)((mhz - 2407u) / 5u);
    if (mhz == 2484u) return 14;
    if (mhz >= 5180u && mhz <= 5320u) return (uint8_t)(36u + (mhz - 5180u) / 5u);
    if (mhz >= 5490u && mhz <= 5710u) return (uint8_t)(100u + (mhz - 5490u) / 5u);
    if (mhz >= 5745u && mhz <= 5885u) return (uint8_t)(149u + (mhz - 5745u) / 5u);
    return applied;
}

// ------------------------------------------------------------- nl80211

void nlaPut(uint8_t* buf, size_t& off, uint16_t type, const void* data, uint16_t dlen) {
    struct nlattr* a = (struct nlattr*)(buf + off);
    a->nla_len  = (uint16_t)(NLA_HDRLEN + dlen);
    a->nla_type = type;
    memcpy(buf + off + NLA_HDRLEN, data, dlen);
    const size_t padded = NLA_ALIGN(a->nla_len);
    memset(buf + off + a->nla_len, 0, padded - a->nla_len);
    off += padded;
}

// nlmsghdr + genlmsghdr; returns the offset the attributes start at.
size_t genlHeader(uint8_t* buf, uint16_t family, uint8_t cmd, uint16_t flags, uint32_t seq) {
    struct nlmsghdr* nh = (struct nlmsghdr*)buf;
    nh->nlmsg_len   = 0;   // set by genlFinish
    nh->nlmsg_type  = family;
    nh->nlmsg_flags = flags;
    nh->nlmsg_seq   = seq;
    nh->nlmsg_pid   = 0;
    struct genlmsghdr* g = (struct genlmsghdr*)(buf + NLMSG_HDRLEN);
    g->cmd = cmd;
    g->version = 0;
    g->reserved = 0;
    return NLMSG_HDRLEN + GENL_HDRLEN;
}

size_t buildSetFreq(uint8_t* buf, uint16_t family, uint32_t seq, int ifindex, uint32_t mhz) {
    size_t off = genlHeader(buf, family, NL80211_CMD_SET_WIPHY,
                            NLM_F_REQUEST | NLM_F_ACK, seq);
    const uint32_t ifi = (uint32_t)ifindex;
    const uint32_t ct  = NL80211_CHAN_NO_HT;   // what `iw ... set freq <f>` sends
    nlaPut(buf, off, NL80211_ATTR_IFINDEX, &ifi, 4);
    nlaPut(buf, off, NL80211_ATTR_WIPHY_FREQ, &mhz, 4);
    nlaPut(buf, off, NL80211_ATTR_WIPHY_CHANNEL_TYPE, &ct, 4);
    ((struct nlmsghdr*)buf)->nlmsg_len = (uint32_t)off;
    return off;
}

// Send, then read until the reply for `seq`. With `ack` the result is the
// NLMSG_ERROR code (0 = success). Without, the first matching non-error
// message is copied to `reply`. -ETIMEDOUT after 300 ms. Replies to older
// sequence numbers (a request that timed out earlier) are skipped.
int nlTransact(int sock, const uint8_t* msg, size_t len, uint32_t seq,
               std::vector<uint8_t>* reply) {
    struct sockaddr_nl to;
    memset(&to, 0, sizeof to);
    to.nl_family = AF_NETLINK;
    if (sendto(sock, msg, len, 0, (struct sockaddr*)&to, sizeof to) < 0) return -errno;
    uint8_t in[8192];
    for (int tries = 0; tries < 8; tries++) {
        struct pollfd pfd = { sock, POLLIN, 0 };
        if (poll(&pfd, 1, 300) <= 0) return -ETIMEDOUT;
        const ssize_t n = recv(sock, in, sizeof in, 0);
        if (n < 0) { if (errno == EINTR) continue; return -errno; }
        int rem = (int)n;
        for (struct nlmsghdr* nh = (struct nlmsghdr*)in; NLMSG_OK(nh, rem); nh = NLMSG_NEXT(nh, rem)) {
            if (nh->nlmsg_seq != seq) continue;
            if (nh->nlmsg_type == NLMSG_ERROR) {
                const struct nlmsgerr* e = (const struct nlmsgerr*)NLMSG_DATA(nh);
                if (e->error != 0 || !reply) return e->error;
                continue;   // the ack after a data reply
            }
            if (reply && reply->empty())
                reply->assign((const uint8_t*)nh, (const uint8_t*)nh + nh->nlmsg_len);
            if (reply && !(nh->nlmsg_flags & NLM_F_MULTI)) return 0;
        }
    }
    return reply && !reply->empty() ? 0 : -ETIMEDOUT;
}

// CTRL_CMD_GETFAMILY "nl80211" -> the family id. 0 on failure.
uint16_t resolveNl80211(int sock, uint32_t& seq) {
    uint8_t buf[64];
    memset(buf, 0, sizeof buf);
    const uint32_t s = seq++;
    size_t off = genlHeader(buf, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, NLM_F_REQUEST, s);
    nlaPut(buf, off, CTRL_ATTR_FAMILY_NAME, "nl80211", 8);
    ((struct nlmsghdr*)buf)->nlmsg_len = (uint32_t)off;
    std::vector<uint8_t> rep;
    if (nlTransact(sock, buf, off, s, &rep) != 0 || rep.size() < NLMSG_HDRLEN + GENL_HDRLEN)
        return 0;
    size_t a = NLMSG_HDRLEN + GENL_HDRLEN;
    while (a + NLA_HDRLEN <= rep.size()) {
        const struct nlattr* na = (const struct nlattr*)(rep.data() + a);
        if (na->nla_len < NLA_HDRLEN || a + na->nla_len > rep.size()) break;
        if ((na->nla_type & NLA_TYPE_MASK) == CTRL_ATTR_FAMILY_ID && na->nla_len >= NLA_HDRLEN + 2) {
            uint16_t id;
            memcpy(&id, rep.data() + a + NLA_HDRLEN, 2);
            return id;
        }
        a += NLA_ALIGN(na->nla_len);
    }
    return 0;
}

bool netlinkSetFreq(uint32_t mhz) {
    if (s_mon.nl < 0 || !s_mon.nlFamily) { s_lastSetErr = -ENOPROTOOPT; return false; }
    uint8_t buf[64];
    memset(buf, 0, sizeof buf);
    const uint32_t seq = s_mon.nlSeq++;
    const size_t len = buildSetFreq(buf, s_mon.nlFamily, seq, s_mon.ifindex, mhz);
    s_lastSetErr = nlTransact(s_mon.nl, buf, len, seq, nullptr);
    return s_lastSetErr == 0;
}

// Fallback for a driver/kernel whose nl80211 path refused: iw's own
// request is the same command, but it is the reference implementation.
bool iwSetFreq(const std::string& ifname, uint32_t mhz) {
    return runTool({"iw", "dev", ifname, "set", "freq", std::to_string(mhz)}) == 0;
}

// ---------------------------------------------------- phy channel list

// `iw phy <phyN> info` frequency lines look like
//   "\t\t\t* 2412.0 MHz [1] (20.0 dBm)"      (or "2412 MHz" on older iw)
//   "\t\t\t* 5260.0 MHz [52] (20.0 dBm) (radar detection)"
//   "\t\t\t* 5865.0 MHz [173] (disabled)"
// A channel is usable when it is listed and not disabled. Monitor RX on a
// radar / no-IR channel is allowed, so those count. 6 GHz channel numbers
// overlap 2.4/5 ones, so a line only counts when its frequency is the one
// this sniffer's channel plan gives that number.
void parsePhyInfo(const char* text, bool usable[256]) {
    memset(usable, 0, 256 * sizeof(bool));
    const char* p = text;
    while (*p) {
        const char* eol = strchr(p, '\n');
        const size_t n = eol ? (size_t)(eol - p) : strlen(p);
        const std::string line(p, n);
        int mhz = 0;
        const char* lb = strchr(line.c_str(), '[');
        if (lb && std::sscanf(line.c_str(), " * %d", &mhz) == 1 &&
            strstr(line.c_str(), " MHz")) {
            const int ch = atoi(lb + 1);
            if (ch > 0 && ch < 256 && chanToMHz((uint8_t)ch) == (uint32_t)mhz &&
                !strstr(line.c_str(), "disabled"))
                usable[ch] = true;
        }
        p += n;
        if (*p) p++;
    }
}

bool s_phyProbed = false;
bool s_phyUsable[256] = {false};
// Set (release) once s_phyUsable holds a parsed answer, so the feed thread
// can read the table (acquire) without racing the UI thread's probe.
std::atomic<bool> s_phyReady{false};

// A channel the wiphy does not list as usable is blocked by the regulatory
// domain (US: 2.4 GHz 12/13 are "(disabled)"), not refused by the driver.
// Unknown until the phy has been probed.
bool regulatoryBlocked(uint8_t ch, bool phyKnown, const bool usable[256]) {
    return phyKnown && !usable[ch];
}

void probePhy() {
    if (s_phyProbed) return;
    s_phyProbed = true;
    std::string ifn = s_mon.adapter.empty() ? findAdapter() : s_mon.adapter;
    if (ifn.empty()) return;
    char target[PATH_MAX];
    ssize_t n = readlink(("/sys/class/net/" + ifn + "/phy80211").c_str(),
                         target, sizeof target - 1);
    if (n < 0) return;
    target[n] = 0;
    const char* base = std::strrchr(target, '/');   // .../ieee80211/phyN
    if (!base || !validIfName(base + 1)) return;
    std::string info;
    runTool({"iw", "phy", base + 1, "info"}, &info);
    parsePhyInfo(info.c_str(), s_phyUsable);
    // An empty answer (iw failed) must not read as "every channel blocked".
    bool any = false;
    for (int c = 0; c < 256 && !any; c++) any = s_phyUsable[c];
    if (any) s_phyReady.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------- radiotap

// Defined radiotap fields 0..27: {alignment, size}. 28 (TLVs) is
// variable-length and ends the walk; so does any bit past 27 in the
// radiotap namespace, whose size this table cannot know.
struct RtField { uint8_t align, size; };
const RtField RT_FIELDS[28] = {
    {8, 8},  // 0  TSFT
    {1, 1},  // 1  Flags
    {1, 1},  // 2  Rate
    {2, 4},  // 3  Channel: u16 MHz, u16 flags
    {1, 2},  // 4  FHSS
    {1, 1},  // 5  dBm antenna signal
    {1, 1},  // 6  dBm antenna noise
    {2, 2},  // 7  Lock quality
    {2, 2},  // 8  TX attenuation
    {2, 2},  // 9  dB TX attenuation
    {1, 1},  // 10 dBm TX power
    {1, 1},  // 11 Antenna
    {1, 1},  // 12 dB antenna signal
    {1, 1},  // 13 dB antenna noise
    {2, 2},  // 14 RX flags
    {2, 2},  // 15 TX flags
    {1, 1},  // 16 RTS retries
    {1, 1},  // 17 data retries
    {4, 8},  // 18 XChannel
    {1, 3},  // 19 MCS
    {4, 8},  // 20 A-MPDU status
    {2, 12}, // 21 VHT
    {8, 12}, // 22 timestamp
    {2, 12}, // 23 HE
    {2, 12}, // 24 HE-MU
    {2, 6},  // 25 HE-MU-other-user
    {1, 1},  // 26 0-length PSDU
    {2, 4},  // 27 L-SIG
};
enum { RT_FLAGS = 1, RT_CHANNEL = 3, RT_DBM_SIGNAL = 5,
       RT_NS_RADIOTAP = 29, RT_NS_VENDOR = 30, RT_EXT = 31 };
enum { RT_F_FCS = 0x10, RT_F_BADFCS = 0x40 };

struct RtInfo {
    const uint8_t* frame;
    size_t   frameLen;   // without the FCS, whether or not it was there
    int8_t   rssi;       // dBm; the first antenna-signal field (the combined one)
    bool     haveRssi;
    uint32_t mhz;        // 0 when no channel field
    bool     badFcs;
};

// Walk the presence words (bit 31 = another word follows; bits 29/30 switch
// the next word to the radiotap / a vendor namespace), then the data in
// presence order, aligning each field to its natural boundary relative to
// the header start. Returns false when the header itself is malformed.
bool parseRadiotap(const uint8_t* raw, size_t rawLen, RtInfo* out) {
    if (rawLen < 8 || raw[0] != 0) return false;
    const size_t len = (size_t)raw[2] | ((size_t)raw[3] << 8);
    if (len < 8 || len > rawLen) return false;

    // Presence words.
    std::vector<uint32_t> words;
    size_t off = 4;
    for (;;) {
        if (off + 4 > len) return false;
        const uint32_t w = (uint32_t)raw[off] | ((uint32_t)raw[off+1] << 8) |
                           ((uint32_t)raw[off+2] << 16) | ((uint32_t)raw[off+3] << 24);
        words.push_back(w);
        off += 4;
        if (!(w & (1u << RT_EXT))) break;
        if (words.size() > 16) return false;
    }

    uint8_t flags = 0;
    out->haveRssi = false;
    out->rssi = 0;
    out->mhz = 0;
    // Data, word by word. `vendor` = this word's bits belong to a vendor
    // namespace: its data is a 6-byte header (OUI, sub-ns, skip length)
    // plus `skip` bytes, all of which are stepped over. `base` = the bit
    // number of this word's bit 0 within its namespace (+32 per extension
    // word in the same namespace).
    bool vendor = false, stop = false;
    int base = 0;
    for (size_t wi = 0; wi < words.size() && !stop; wi++) {
        const uint32_t w = words[wi];
        if (vendor) {
            // One header per vendor namespace, however many extension
            // words it spans; its skip length covers all of their data.
            if (base != 0) goto next;
            off = (off + 1) & ~(size_t)1;
            if (off + 6 > len) break;
            const size_t skip = (size_t)raw[off+4] | ((size_t)raw[off+5] << 8);
            off += 6 + skip;
        } else {
            for (int b = 0; b < 29 && !stop; b++) {
                if (!(w & (1u << b))) continue;
                const int field = base + b;
                if (field >= 28) { stop = true; break; }
                const RtField f = RT_FIELDS[field];
                off = (off + f.align - 1) & ~(size_t)(f.align - 1);
                if (off + f.size > len) { stop = true; break; }
                if (field == RT_FLAGS) flags = raw[off];
                else if (field == RT_CHANNEL && !out->mhz)
                    out->mhz = (uint32_t)raw[off] | ((uint32_t)raw[off+1] << 8);
                else if (field == RT_DBM_SIGNAL && !out->haveRssi) {
                    out->rssi = (int8_t)raw[off];
                    out->haveRssi = true;
                }
                off += f.size;
            }
        }
    next:
        // What the next word means.
        if (w & (1u << RT_NS_RADIOTAP))    { vendor = false; base = 0; }
        else if (w & (1u << RT_NS_VENDOR)) { vendor = true;  base = 0; }
        else                               { base += 32; }
    }

    out->frame = raw + len;
    out->frameLen = rawLen - len;
    out->badFcs = (flags & RT_F_BADFCS) != 0;
    if (flags & RT_F_FCS) {
        if (out->frameLen < 4) return false;
        out->frameLen -= 4;
    }
    return true;
}

// ------------------------------------------------------------- the queue

// Parse, filter, and queue one radiotapped frame. Returns the frame type
// queued (0 mgmt / 1 ctl / 2 data) or -1 when dropped. The firmware's
// callback buffer is built here: the frame, then four zero bytes standing
// in for the FCS the ESP32 leaves on (detection.cpp's beacon IE walk stops
// four bytes before sig_len). A driver that delivered the real FCS has had
// it cut off above, so it is never counted twice.
int enqueueParsed(const uint8_t* raw, size_t rawLen, uint8_t applied) {
    RtInfo rt;
    if (!parseRadiotap(raw, rawLen, &rt)) return -1;
    if (rt.badFcs) return -1;
    if (rt.frameLen < 24) return -1;
    const uint8_t type = (rt.frame[0] & 0x0C) >> 2;
    const uint32_t mask = s_filterMask.load();
    switch (type) {
        case 0: if (!(mask & WIFI_PROMIS_FILTER_MASK_MGMT)) return -1; break;
        case 1: if (!(mask & WIFI_PROMIS_FILTER_MASK_CTL))  return -1; break;
        case 2: if (!(mask & WIFI_PROMIS_FILTER_MASK_DATA)) return -1; break;
        default: return -1;   // type 3 is reserved
    }
    if (rt.frameLen + 4 > 0xFFFF) return -1;
    QFrame q;
    q.type = type;
    q.buf.assign(sizeof(wifi_promiscuous_pkt_t) + rt.frameLen + 4, 0);
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)q.buf.data();
    pkt->rx_ctrl.sig_len = (uint16_t)(rt.frameLen + 4);
    // No signal field at all: report the floor, not 0 dBm (which the
    // firmware would read as "right next to you").
    pkt->rx_ctrl.rssi = rt.haveRssi ? rt.rssi : (int8_t)-100;
    pkt->rx_ctrl.channel = frameChannel(rt.mhz, applied);
    memcpy(q.buf.data() + sizeof(wifi_promiscuous_pkt_t), rt.frame, rt.frameLen);
    std::lock_guard<std::mutex> lk(s_qMu);
    if (s_q.size() >= Q_CAP) { s_mon.framesDropped++; return -1; }
    s_q.push_back(std::move(q));
    s_mon.framesQueued++;
    return (int)type;
}

// ----------------------------------------------------- the passive scan
//
// The firmware's raw Wi-Fi screen asks for WiFi.scanNetworks(async,
// no hidden, PASSIVE, 150 ms/channel). On the ESP32 the driver listens on
// each channel and lists the APs whose beacons it heard; this is the same
// thing on the monitor vif: the feed thread steps through `s_scanPlan`,
// dwelling `s_scanDwellMs` on each, and every beacon / probe response
// heard updates `s_scanTab`. Nothing is transmitted (the firmware chose
// passive precisely so the detector never announces itself).
//
// Shared state: plan/table/flags under s_scanMu. The UI thread starts and
// reads; the feed thread runs the sweep. The firmware's own scan results
// are read through the WiFi stub only after done (a snapshot copy).

// Defined with the thread below.
void teardown();
void markLost(const char* when);

const size_t SCAN_CAP = 64;
std::mutex                s_scanMu;
std::vector<uint8_t>      s_scanPlan;        // channels to visit
std::vector<WifiScanAp>   s_scanTab;
std::atomic<bool>         s_scanActive{false};  // feed thread owns the radio
std::atomic<bool>         s_scanDone{false};
uint16_t                  s_scanDwellMs = 150;

// Beacon (subtype 8) or probe response (5): add or refresh its AP. `mhz`
// from radiotap is the fallback channel when there is no DS Parameter Set
// IE (5 GHz beacons often omit it). Hidden networks (empty or all-NUL SSID)
// are skipped, as the firmware's show_hidden=false scan would.
// Returns true when the frame was an AP advertisement that was used.
bool scanAbsorb(const RtInfo& rt) {
    const uint8_t* f = rt.frame;
    const size_t   n = rt.frameLen;
    if (n < 36) return false;                    // 24 header + 12 fixed
    const uint8_t fc0 = f[0];
    if (((fc0 & 0x0C) >> 2) != 0) return false;  // management only
    const uint8_t sub = (fc0 & 0xF0) >> 4;
    if (sub != 8 && sub != 5) return false;
    WifiScanAp ap;
    memset(&ap, 0, sizeof ap);
    memcpy(ap.bssid, f + 16, 6);                 // addr3 = BSSID
    const uint16_t cap = (uint16_t)f[34] | ((uint16_t)f[35] << 8);
    ap.privacy = (cap & 0x0010) != 0;            // Capability: Privacy
    ap.rssi    = rt.haveRssi ? rt.rssi : (int8_t)-100;
    ap.channel = frameChannel(rt.mhz, 0);
    bool haveSsid = false;
    for (size_t i = 36; i + 2 <= n; ) {
        const uint8_t id = f[i], len = f[i + 1];
        if (i + 2 + len > n) break;
        const uint8_t* v = f + i + 2;
        if (id == 0 && !haveSsid) {
            haveSsid = true;
            const size_t l = len > 32 ? 32 : len;
            memcpy(ap.ssid, v, l);
            ap.ssid[l] = 0;
        } else if (id == 3 && len == 1 && v[0]) {
            ap.channel = v[0];                   // DS Parameter Set
        }
        i += 2 + len;
    }
    if (!haveSsid || ap.ssid[0] == 0) return false;   // hidden
    if (!ap.channel) return false;
    std::lock_guard<std::mutex> lk(s_scanMu);
    for (WifiScanAp& e : s_scanTab)
        if (memcmp(e.bssid, ap.bssid, 6) == 0) {
            if (ap.rssi > e.rssi) e.rssi = ap.rssi;   // strongest heard
            if (e.ssid[0] == 0) memcpy(e.ssid, ap.ssid, sizeof e.ssid);
            e.channel = ap.channel;
            e.privacy = ap.privacy;
            return true;
        }
    if (s_scanTab.size() < SCAN_CAP) s_scanTab.push_back(ap);
    return true;
}

void scanFinish() {
    std::lock_guard<std::mutex> lk(s_scanMu);
    std::stable_sort(s_scanTab.begin(), s_scanTab.end(),
                     [](const WifiScanAp& a, const WifiScanAp& b) { return a.rssi > b.rssi; });
    s_scanActive = false;
    s_scanDone = true;
}

// Tune for the sweep. Same path as the hop, but the hop's remembered
// refusals (s_setFailed) are not inherited: the sweep tries every channel
// of its plan afresh, so a transient refusal cannot empty a sweep. (The
// "0 network(s)" sweeps of 2026-11-22 that this was first written for were
// in fact the adapter dropping off the USB bus -- see linkAlive; the
// startup "refused" line was only the regulatory ch 12/13.)
bool scanTune(uint8_t ch) {
    const uint32_t mhz = chanToMHz(ch);
    if (!mhz) return false;
    return netlinkSetFreq(mhz) || iwSetFreq(s_mon.monIf, mhz);
}

// The sweep, on the feed thread. Promiscuous delivery to the firmware is
// off for its duration (the firmware switched it off before scanning), so
// frames only feed the table.
void scanRun(uint8_t* readBuf, size_t bufLen) {
    std::vector<uint8_t> plan;
    {
        std::lock_guard<std::mutex> lk(s_scanMu);
        plan = s_scanPlan;
    }
    const long dwellNs = (long)s_scanDwellMs * 1000000L;
    unsigned tuned = 0;
    for (uint8_t ch : plan) {
        if (!s_running.load() || !s_scanActive.load()) break;
        if (!scanTune(ch)) {
            // A dead vif refuses every set: stop here and say so, rather
            // than report an empty room.
            if (!linkAlive(s_mon.monIf, s_mon.ifindex)) {
                std::fprintf(stderr, "[wifi_scan] sweep stopped: the Wi-Fi adapter is gone "
                             "(tuned %u of %zu channel(s))\n", tuned, (size_t)plan.size());
                s_appliedChan = 0;
                scanFinish();
                markLost("during a sweep");
                return;
            }
            continue;
        }
        tuned++;
        struct timespec t0, now;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (;;) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            const long el = (now.tv_sec - t0.tv_sec) * 1000000000L + (now.tv_nsec - t0.tv_nsec);
            if (el >= dwellNs) break;
            struct pollfd pfd = { s_mon.sock, POLLIN, 0 };
            int left = (int)((dwellNs - el) / 1000000L);
            if (poll(&pfd, 1, left < 1 ? 1 : left) <= 0) continue;
            for (int i = 0; i < 64; i++) {
                const ssize_t n = recv(s_mon.sock, readBuf, bufLen, MSG_DONTWAIT);
                if (n < 0) break;
                RtInfo rt;
                if (n >= 8 && parseRadiotap(readBuf, (size_t)n, &rt) && !rt.badFcs)
                    (void)scanAbsorb(rt);
            }
        }
    }
    // The hop picks up again from whatever the firmware asks next: force a
    // retune by forgetting the applied channel. The count is the diagnostic:
    // tuned < plan means the driver refused sets this sweep (0 networks with
    // a tuned short of the plan is a radio state, not an empty room).
    std::fprintf(stderr, "[wifi_scan] sweep: tuned %u of %zu channel(s)\n",
                 tuned, (size_t)plan.size());
    s_appliedChan = 0;
    scanFinish();
}

// ------------------------------------------------------------- the thread

// Loss / recovery bookkeeping (feed thread only).
bool     s_lost = false;
bool     s_regLogged = false;          // the regulatory skip is logged once per start
bool     s_reupLogged = false;         // "adapter came back up" logged once per attach
bool     s_seenAdapter = false;        // recovery: an adapter was there last try
unsigned s_retryFailLogged = 0;        // recovery: attach failures logged (capped)
struct timespec s_lostAt = {0, 0};
struct timespec s_nextTry = {0, 0};
struct timespec s_nextCheck = {0, 0};
unsigned s_losses = 0;

long msSince(const struct timespec& t) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - t.tv_sec) * 1000L + (now.tv_nsec - t.tv_nsec) / 1000000L;
}

void afterMs(struct timespec& t, long ms) {
    clock_gettime(CLOCK_MONOTONIC, &t);
    t.tv_sec  += ms / 1000;
    t.tv_nsec += (ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
}

bool reached(const struct timespec& t) { return msSince(t) >= 0; }

bool attachMonitor(bool quiet);   // below, shared with wifiMonStart

// Reset the per-attach channel state: the vif's channel is unknown and
// every refusal belongs to the old attach.
void freshChannelState() {
    s_appliedChan = 0;
    memset(s_setFailed, 0, sizeof s_setFailed);
    s_setRefusedLogged = false;
    s_reupLogged = false;
}

// The vif is gone. Undo what is still there (teardown is safe on a vanished
// interface: the iw/ip calls just fail), so the adapter, if it is still
// present, is back in its original state before a re-attach reads that
// state again. Frames already queued are dropped with it.
void markLost(const char* when) {
    std::fprintf(stderr,
        "[wifi_mon] adapter lost %s: %s is gone (adapter %s; %llu frames so far). "
        "Wi-Fi detection is paused until a USB adapter is back -- check the "
        "kernel log for a USB disconnect\n",
        when, s_mon.monIf.empty() ? "the monitor interface" : s_mon.monIf.c_str(),
        s_mon.adapter.empty() ? "?" : s_mon.adapter.c_str(), s_mon.framesQueued);
    teardown();
    s_mon.adapter.clear();
    s_mon.ifindex = -1;
    s_lost = true;
    s_seenAdapter = false;
    s_retryFailLogged = 0;
    s_losses++;
    clock_gettime(CLOCK_MONOTONIC, &s_lostAt);
    afterMs(s_nextTry, 2000);
}

// One recovery step while lost: finish any sweep at once (there is no radio
// to sweep with), then every 2 s look for a USB adapter. One must be seen on
// two tries in a row (the driver loads its firmware and NetworkManager
// raises the new interface in the first seconds) before the vif is rebuilt.
void recoverStep() {
    if (s_scanActive.load()) {
        std::fprintf(stderr, "[wifi_scan] sweep skipped: no Wi-Fi adapter "
                     "(lost %ld s ago)\n", msSince(s_lostAt) / 1000);
        scanFinish();
    }
    struct timespec ts = {0, 200 * 1000 * 1000};
    nanosleep(&ts, nullptr);
    if (!reached(s_nextTry)) return;
    afterMs(s_nextTry, 2000);
    const bool present = !findAdapter().empty();
    const bool settled = present && s_seenAdapter;
    s_seenAdapter = present;
    if (!settled) return;
    if (!attachMonitor(true)) {
        if (s_retryFailLogged < 3) {
            s_retryFailLogged++;
            std::fprintf(stderr, "[wifi_mon] an adapter is back but the monitor "
                         "interface could not be rebuilt yet; retrying\n");
        }
        return;
    }
    freshChannelState();
    s_lost = false;
    afterMs(s_nextCheck, 1000);
    std::fprintf(stderr, "[wifi_mon] adapter back after %ld s: sniffer up on %s "
                 "(adapter %s); losses this run: %u\n",
                 msSince(s_lostAt) / 1000, s_mon.monIf.c_str(), s_mon.adapter.c_str(),
                 s_losses);
}

// Once a second while attached: the vif still alive, and the adapter's own
// managed interface still down (vif path). NetworkManager raises a freshly
// plugged adapter on its own; an up managed interface makes mac80211 refuse
// every retune (EBUSY), so it is taken down again.
// Returns false when the vif is gone (markLost has run).
bool healthCheck(bool force) {
    if (!force && !reached(s_nextCheck)) return true;
    afterMs(s_nextCheck, 1000);
    if (!linkAlive(s_mon.monIf, s_mon.ifindex)) {
        markLost("mid-run");
        return false;
    }
    if (s_mon.adapterDowned && ifUp(s_mon.adapter)) {
        if (!s_reupLogged) {
            s_reupLogged = true;
            std::fprintf(stderr, "[wifi_mon] %s came back up (NetworkManager?); "
                         "taking it down again so channel sets work\n",
                         s_mon.adapter.c_str());
        }
        // Refusals recorded while it was up were EBUSY from it, not the
        // channel (seen on the device right after a re-attach): forget them.
        if (runTool({"ip", "link", "set", s_mon.adapter, "down"}) == 0) {
            memset(s_setFailed, 0, sizeof s_setFailed);
            s_setRefusedLogged = false;
            s_appliedChan = 0;
        }
    }
    return true;
}

void* feedThread(void*) {
    static uint8_t readBuf[8192];
    afterMs(s_nextCheck, 1000);
    while (s_running.load()) {
        if (s_lost) { recoverStep(); continue; }
        if (!healthCheck(false)) continue;
        if (s_scanActive.load()) { scanRun(readBuf, sizeof readBuf); continue; }
        // Channel set: the firmware's hop clock writes s_wantChan through
        // the stub; apply on change. The poll below bounds the latency to
        // 100 ms, inside the firmware's 120 ms minimum dwell. A channel the
        // driver refuses (netlink and iw both) is remembered so it is not
        // retried every poll; the channel then simply stays where it was,
        // and frames keep their radiotap channel. A channel the regulatory
        // domain disables (the firmware's 2.4 GHz hop includes 12/13; the US
        // does not) is skipped without asking the driver.
        const uint8_t want = s_wantChan.load();
        if (want != s_appliedChan.load() && chanToMHz(want) && !s_setFailed[want] &&
            regulatoryBlocked(want, s_phyReady.load(std::memory_order_acquire), s_phyUsable)) {
            s_setFailed[want] = true;
            if (!s_regLogged) {
                s_regLogged = true;
                std::fprintf(stderr, "[wifi_mon] channel %u is disabled by the regulatory "
                             "domain; the hop skips such channels\n", (unsigned)want);
            }
        }
        if (want != s_appliedChan.load() && chanToMHz(want) && !s_setFailed[want]) {
            const uint32_t mhz = chanToMHz(want);
            const bool ok = netlinkSetFreq(mhz) || iwSetFreq(s_mon.monIf, mhz);
            if (ok) s_appliedChan = want;
            else if (!healthCheck(true)) continue;   // the vif died: not a refusal
            else {
                s_setFailed[want] = true;
                if (!s_setRefusedLogged) {
                    s_setRefusedLogged = true;
                    std::fprintf(stderr,
                        "[wifi_mon] channel sets refused by the driver (first: "
                        "channel %u, %u MHz, nl80211: %s); the channel stays "
                        "where it is and frames keep their true radiotap "
                        "channel\n",
                        (unsigned)want, (unsigned)mhz,
                        s_lastSetErr ? strerror(-s_lastSetErr) : "ok, iw failed");
                }
            }
        }
        // Promiscuous off (or no callback yet during setup): sleep the slot.
        if (!s_promisc.load() || !s_rxCb.load()) {
            struct timespec ts = {0, 50 * 1000 * 1000};
            nanosleep(&ts, nullptr);
            continue;
        }
        struct pollfd pfd = { s_mon.sock, POLLIN, 0 };
        const int pr = poll(&pfd, 1, 100);
        if (!s_running.load()) break;
        if (pr <= 0) continue;
        bool suspect = (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
        // Drain what is there, then go round to the channel check again.
        for (int i = 0; i < 64; i++) {
            const ssize_t n = recv(s_mon.sock, readBuf, sizeof readBuf, MSG_DONTWAIT);
            if (n < 0) { suspect = suspect || lossErrno(errno); break; }
            if (n >= 8) (void)enqueueParsed(readBuf, (size_t)n, s_appliedChan.load());
        }
        if (suspect) (void)healthCheck(true);
    }
    return nullptr;
}

// Undo whatever wifiMonStart did so far: the sockets, the vif or the
// converted interface. Used by stop and by every failure path of start.
void teardown() {
    if (s_mon.sock >= 0) { close(s_mon.sock); s_mon.sock = -1; }
    if (s_mon.nl >= 0)   { close(s_mon.nl);   s_mon.nl = -1; }
    if (s_mon.converted) {
        runTool({"ip", "link", "set", s_mon.adapter, "down"});
        runTool({"iw", "dev", s_mon.adapter, "set", "type", s_mon.origType});
        runTool({"ip", "link", "set", s_mon.adapter, s_mon.origUp ? "up" : "down"});
    } else if (s_mon.createdVif) {
        runTool({"ip", "link", "set", MON_IF, "down"});
        runTool({"iw", "dev", MON_IF, "del"});
    }
    if (s_mon.adapterDowned)
        runTool({"ip", "link", "set", s_mon.adapter, "up"});
    s_mon.adapterDowned = false;
    s_mon.converted = false;
    s_mon.createdVif = false;
    s_mon.monIf.clear();
    {
        std::lock_guard<std::mutex> lk(s_qMu);
        s_q.clear();
    }
}

bool failStart(const char* what) {
    if (what) std::perror(what);
    teardown();
    return false;
}

// Find the adapter, build the monitor interface, open and bind the sockets.
// On failure everything done so far is undone. Shared by wifiMonStart and
// the feed thread's recovery after an adapter loss (`quiet`: no "no adapter"
// line every retry).
bool attachMonitor(bool quiet) {
    s_mon.adapter = findAdapter();
    if (s_mon.adapter.empty()) {
        if (!quiet)
            std::fprintf(stderr,
                "[wifi_mon] no USB Wi-Fi adapter found "
                "(SQUACH_WIFI_IF=<if> to override); running on the empty "
                "stub radio\n");
        return false;
    }
    // Our own vif left behind by a crashed run must go first, or the add
    // below fails on a name clash. The name is ours alone.
    if (pathExists(std::string("/sys/class/net/") + MON_IF)) {
        runTool({"ip", "link", "set", MON_IF, "down"});
        runTool({"iw", "dev", MON_IF, "del"});
    }
    if (toolPath("iw").empty() || toolPath("ip").empty()) {
        std::fprintf(stderr, "[wifi_mon] iw/ip not found as root-owned tools in "
                             "/usr/sbin:/usr/bin:/sbin:/bin; running on the empty stub radio\n");
        return false;
    }
    // The adapter's link state, restored on stop (both paths).
    s_mon.origUp = ifUp(s_mon.adapter);
    if (runTool({"iw", "dev", s_mon.adapter, "interface", "add", MON_IF,
                 "type", "monitor"}) == 0) {
        s_mon.createdVif = true;
        s_mon.monIf = MON_IF;
        // mac80211 refuses to retune a monitor vif (EBUSY) while another
        // interface on the same radio is up — NetworkManager keeps the
        // dongle's managed interface up and scanning, so every hop failed
        // and frames followed NM's scans instead. The adapter is the
        // sniffer's alone (wlan0 carries the network), so take it down for
        // the run; teardown brings it back up. Verified on the CM0 with the
        // mt7921u: wlan1 up -> set freq EBUSY; down -> 20/20 sets over 40 s
        // across 2.4/5 GHz, and NM did not raise it again.
        if (s_mon.origUp) {
            if (runTool({"ip", "link", "set", s_mon.adapter, "down"}) == 0)
                s_mon.adapterDowned = true;
            else
                std::fprintf(stderr, "[wifi_mon] could not take %s down; channel "
                             "sets will likely be refused (EBUSY)\n", s_mon.adapter.c_str());
        }
    } else {
        // The driver will not take a second vif: convert the interface
        // itself, remembering its type and link state for the restore.
        // The type goes back into an argv, so only a known iw type name is
        // kept.
        std::string out;
        runTool({"iw", "dev", s_mon.adapter, "info"}, &out);
        s_mon.origType.clear();
        if (const char* t = strstr(out.c_str(), "type ")) {
            t += 5;
            while (*t && *t >= 'a' && *t <= 'z') s_mon.origType += *t++;
        }
        static const char* const KNOWN[] = { "managed", "ibss", "monitor", "mesh", "ocb", "ap" };
        bool known = false;
        for (const char* k : KNOWN) known = known || s_mon.origType == k;
        if (!known) s_mon.origType = "managed";
        if (runTool({"ip", "link", "set", s_mon.adapter, "down"}) != 0 ||
            runTool({"iw", "dev", s_mon.adapter, "set", "type", "monitor"}) != 0) {
            if (s_mon.origUp) runTool({"ip", "link", "set", s_mon.adapter, "up"});
            if (!quiet) std::fprintf(stderr,
                "[wifi_mon] %s will not take monitor mode%s; running on the "
                "empty stub radio\n", s_mon.adapter.c_str(),
                geteuid() == 0 ? "" : " (not root: run with sudo, or `make caps`)");
            return false;
        }
        s_mon.converted = true;
        s_mon.monIf = s_mon.adapter;
    }
    // A monitor vif is created down, and drivers deliver nothing while it
    // stays down. Bring it up regardless of the original link state
    // (restored on stop).
    if (runTool({"ip", "link", "set", s_mon.monIf, "up"}) != 0)
        std::fprintf(stderr, "[wifi_mon] could not bring %s up\n", s_mon.monIf.c_str());

    // The packet socket: every protocol (mac80211 tags monitor frames
    // ETH_P_802_2; see the header comment), bound to the monitor interface.
    s_mon.sock = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, (int)htons(ETH_P_ALL));
    if (s_mon.sock < 0) return failStart("[wifi_mon] AF_PACKET socket");
    const int ifi = (int)if_nametoindex(s_mon.monIf.c_str());
    if (ifi <= 0) return failStart("[wifi_mon] if_nametoindex");
    s_mon.ifindex = ifi;
    struct sockaddr_ll bnd;
    memset(&bnd, 0, sizeof bnd);
    bnd.sll_family = AF_PACKET;
    bnd.sll_protocol = htons(ETH_P_ALL);
    bnd.sll_ifindex = ifi;
    if (bind(s_mon.sock, (struct sockaddr*)&bnd, sizeof bnd) < 0)
        return failStart("[wifi_mon] bind");

    // The generic-netlink socket for channel sets. A failure here is not
    // fatal: the iw fallback still tunes.
    s_mon.nl = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
    if (s_mon.nl >= 0) {
        struct sockaddr_nl local;
        memset(&local, 0, sizeof local);
        local.nl_family = AF_NETLINK;
        if (bind(s_mon.nl, (struct sockaddr*)&local, sizeof local) == 0)
            s_mon.nlFamily = resolveNl80211(s_mon.nl, s_mon.nlSeq);
        if (!s_mon.nlFamily)
            std::fprintf(stderr, "[wifi_mon] nl80211 family not resolved; channel sets go through iw\n");
    }
    return true;
}

}   // namespace

namespace cm0 {

// -------------------------------------------------------------- lifecycle

bool wifiMonStart() {
    if (s_mon.threadStarted) return true;
    raiseAmbientNetCaps();
    if (!attachMonitor(false)) return false;
    s_running = true;
    freshChannelState();   // force the first set: the vif's channel is unknown
    s_regLogged = false;
    s_lost = false;
    s_losses = 0;
    if (pthread_create(&s_mon.thread, nullptr, feedThread, nullptr) != 0) {
        s_running = false;
        return failStart("[wifi_mon] pthread_create");
    }
    s_mon.threadStarted = true;
    std::fprintf(stderr, "[wifi_mon] sniffer up on %s (adapter %s)\n",
                 s_mon.monIf.c_str(), s_mon.adapter.c_str());
    return true;
}

void wifiMonStop() {
    if (!s_mon.threadStarted) return;
    s_running = false;
    pthread_join(s_mon.thread, nullptr);
    s_mon.threadStarted = false;
    teardown();
    std::fprintf(stderr, "[wifi_mon] down (%llu frames queued, %llu delivered, %llu dropped; "
                 "adapter lost %u time(s)%s)\n",
                 s_mon.framesQueued, s_mon.framesDelivered, s_mon.framesDropped,
                 s_losses, s_lost ? ", still gone at exit" : "");
}

size_t wifiMonPump(size_t maxFrames) {
    std::deque<QFrame> batch;
    {
        std::lock_guard<std::mutex> lk(s_qMu);
        if (s_q.empty()) return 0;
        if (s_q.size() <= maxFrames) batch.swap(s_q);
        else
            for (size_t i = 0; i < maxFrames; i++) {
                batch.push_back(std::move(s_q.front()));
                s_q.pop_front();
            }
    }
    const wifi_promiscuous_rx_cb_t cb = s_rxCb.load();
    if (!cb) return 0;
    size_t n = 0;
    for (QFrame& q : batch) {
        wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)q.buf.data();
        pkt->payload = q.buf.data() + sizeof(wifi_promiscuous_pkt_t);
        cb(q.buf.data(), q.type == 0 ? WIFI_PROMISC_RX_MGMT :
                         (q.type == 2 ? WIFI_PROMISC_RX_DATA : WIFI_PROMISC_RX_CTL));
        n++;
    }
    s_mon.framesDelivered += n;
    return n;
}

// ------------------------------------------------------------ the forwards

void wifiMonRegisterRx(wifi_promiscuous_rx_cb_t cb) { s_rxCb = cb; }
void wifiMonSetChannel(uint8_t ch) { if (ch) s_wantChan = ch; }
void wifiMonSetPromisc(bool on)    { s_promisc = on; }
void wifiMonSetFilter(uint32_t m)  { s_filterMask = m; }

// Probed on the sniffer's own wiphy -- never all of them, because the
// CM0's onboard brcmfmac card is dual-band and would answer for the wrong
// radio. The wiphy exists as soon as the adapter's netdev does, so the
// firmware's boot probe (inside setup(), after wifiMonStart) gets an honest
// answer. Probed once: the boot probe asks 28 times.
bool wifiMonChannelUsable(uint8_t ch) {
    probePhy();
    return s_phyUsable[ch];
}

// ------------------------------------------------------------ the scan API

bool wifiMonScanStart(uint16_t dwellMs) {
    if (!s_mon.threadStarted) return false;
    // The plan is built here, on the UI thread, from the same per-channel
    // regulatory answer the boot probe used (probePhy is not thread-safe).
    std::vector<uint8_t> plan;
    for (uint8_t ch = 1; ch <= 13; ch++)
        if (wifiMonChannelUsable(ch)) plan.push_back(ch);
#if SQW_WIFI_5G
    // The 20 MHz plan's three blocks (149 is not on the 36+4k grid).
    static const uint8_t BLOCKS[][2] = { {36, 64}, {100, 144}, {149, 177} };
    for (const auto& b : BLOCKS)
        for (unsigned ch = b[0]; ch <= b[1]; ch += 4)
            if (wifiMonChannelUsable((uint8_t)ch)) plan.push_back((uint8_t)ch);
#endif
    {
        std::lock_guard<std::mutex> lk(s_scanMu);
        s_scanPlan.swap(plan);
        s_scanTab.clear();
        s_scanDwellMs = dwellMs ? dwellMs : 150;
    }
    s_scanDone = false;
    s_scanActive = true;
    return true;
}

int wifiMonScanState() {
    if (s_scanActive.load()) return -1;
    if (!s_scanDone.load()) return -2;
    std::lock_guard<std::mutex> lk(s_scanMu);
    return (int)s_scanTab.size();
}

size_t wifiMonScanResults(WifiScanAp* out, size_t cap) {
    if (s_scanActive.load() || !s_scanDone.load()) return 0;
    std::lock_guard<std::mutex> lk(s_scanMu);
    const size_t n = s_scanTab.size() < cap ? s_scanTab.size() : cap;
    for (size_t i = 0; i < n; i++) out[i] = s_scanTab[i];
    return n;
}

void wifiMonScanClear() {
    s_scanActive = false;
    std::lock_guard<std::mutex> lk(s_scanMu);
    s_scanTab.clear();
    s_scanDone = false;
}

// ------------------------------------------------------------ test hooks

int wifiMonTestScanAbsorb(const uint8_t* raw, size_t rawLen) {
    RtInfo rt;
    if (!parseRadiotap(raw, rawLen, &rt) || rt.badFcs) return -1;
    if (!scanAbsorb(rt)) return 0;
    std::lock_guard<std::mutex> lk(s_scanMu);
    return (int)s_scanTab.size();
}

void wifiMonTestScanFinish() { scanFinish(); }

bool wifiMonTestLinkAlive(const char* ifname, int ifindex) {
    return linkAlive(ifname ? ifname : "", ifindex);
}

bool wifiMonTestLossErrno(int e) { return lossErrno(e); }

bool wifiMonTestRegulatoryBlocked(uint8_t ch, bool phyKnown, const bool usable[256]) {
    return regulatoryBlocked(ch, phyKnown, usable);
}

int wifiMonTestInject(const uint8_t* raw, size_t rawLen, uint8_t appliedChan) {
    const int t = enqueueParsed(raw, rawLen, appliedChan ? appliedChan : s_appliedChan.load());
    if (t < 0) return -1;
    return wifiMonPump(1) == 1 ? t : -1;
}

void wifiMonTestParsePhyInfo(const char* text, bool usable[256]) {
    parsePhyInfo(text, usable);
}

size_t wifiMonTestBuildSetFreq(uint8_t* out, size_t cap, uint16_t family,
                               int ifindex, uint32_t mhz) {
    if (cap < 64) return 0;
    memset(out, 0, 64);
    return buildSetFreq(out, family, 7, ifindex, mhz);
}

}   // namespace cm0
