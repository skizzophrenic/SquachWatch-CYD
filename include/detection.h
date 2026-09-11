// SquachWatch-CYD — DetectionEngine public API
#pragma once
#if SQUACH_MESH
#include "squachmesh.h"
#endif
#include "state.h"
#include "sd_log.h"
#include "remote_id.h"
#include <Preferences.h>

// A single unfiltered BLE sighting from the manual raw scanner (see
// startRawBleScan() below) -- every device seen, not just ones
// matching a known surveillance signature like the log's Detection
// does.
struct RawBleResult {
    uint8_t mac[6];
    int8_t  rssi;
    char    name[24];   // empty if the device didn't advertise one
};

#if SQUACH_MESH
// Phase 0: does advertising cost anything worth caring about?
//
// TWO questions, not one, and the second only turned up on reading
// platformio.ini. The obvious one is duty cycle -- the BLE scan runs at a 99%
// window, so a transmitter has to steal listening time. The one underneath it
// is heap: the broadcaster role was compiled OUT because its overhead made
// the CLEAR screen's frame-buffer realloc on rotate fail, and turning it back
// on may bring that with it.
//
// The measurement counts ADVERTS SEEN, not detections. Detections are rare
// and depend on what happens to walk past; adverts are thousands a minute
// anywhere populated, so the number is stable in seconds rather than hours.
//
// And the two arms ALTERNATE on a short cycle rather than running once each.
// The RF environment changes minute to minute -- a phone goes by, a bus
// passes -- so measuring off for five minutes and then on for five compares
// two environments, not two configurations. Alternating makes drift hit both
// arms equally.
namespace MeshProbe {
    struct Stats {
        uint16_t offRate;      // adverts/sec x10, advertising off
        uint16_t onRate;       // adverts/sec x10, advertising on
        int16_t  deltaPct;     // (on - off) / off, percent
        uint16_t cycles;       // completed on-arms; the sample size
        bool     advOn;        // which arm is running right now
        uint16_t advMs;        // the interval being measured
        uint32_t heapFreeKb;
        uint32_t heapBlockKb;  // largest contiguous -- the rotate-realloc canary
    };
    void  begin();
    void  tick(uint32_t now);
    void  noteAdvert();        // called from the BLE scan callback
    Stats stats();
    // True once the measurement has enough arms and has stopped flipping.
    bool  concluded();
}

// The radio half. Advertises who we are and listens for somebody else doing
// the same. Deliberately separate from the detection pipeline: a peer must
// never become a Detection -- the HACKER work kept bare Espressif out of the
// signature tables precisely so SquachWatches would not flag each other, and
// this would reintroduce that from the other side.
namespace Mesh {
    void begin();
    void tick(uint32_t now);

    // Called from the BLE scan callback with the raw manufacturer-data blob.
    // Returns true if it was one of ours, so the caller can stop looking.
    bool onManufacturerData(const uint8_t* d, size_t len, const uint8_t* mac, uint32_t now);

    // The visitor, or nullptr. Goes stale on its own if the peer walks away.
    const SquachMesh::Peer* peer();
    const uint8_t*          peerMac();
    // How many SquachWatches have been heard in the last twelve seconds --
    // the visitor and everybody else. For the small "+2" beside him.
    uint8_t                 squadCount(uint32_t now);
    // Every one of them, with what their advert said they look like -- for the
    // SQUAD screen. Sorted by address so the order holds still frame to frame.
    struct SquadMember {
        uint8_t          mac[6];
        SquachMesh::Peer peer;
        uint32_t         seen;
    };
    uint8_t                 squadList(uint32_t now, SquadMember* out, uint8_t cap);
    // Make this board the visitor. The one-visitor rule still holds; this only
    // says who wins it. The guest already here leaves on the next advert the
    // chosen one sends, and if the chosen one goes quiet, first-come applies.
    void                    preferPeer(const uint8_t mac[6]);
    bool                    advertising();
    // Our own advert payload, as the radio should send it (src/mesh.cpp).
    size_t                  buildSelf(uint8_t* out);
    // The radio half, called from tick(): detection.cpp on the device,
    // sim/meshsim.cpp in the emulator.
    void                    radioTick(uint32_t now);
}
#endif

// The BLE scan is restarted once a minute, which is what frees NimBLE's record
// of every device that never answered a scan request -- see scanFlushTick() in
// detection.cpp for the leak that closes. These say what each restart gave
// back, so the diagnostics screen can show whether it is doing anything.
struct ScanFlushStats {
    uint32_t count;        // restarts since boot
    uint32_t lastFreed;    // heap bytes the last one gave back
    uint32_t totalFreed;   // ...and all of them together
};
ScanFlushStats scanFlushStats();

class DetectionEngine {
public:
    bool     init();
    void     loop();
    void     clearLog();
    uint8_t  logCount() const { return _logCount; }
    const Detection* logAt(uint8_t idx) const;     // 0 = newest
    const Detection* latest() const { return _latest; }
    uint16_t countByType(DetectionType t) const { return _typeCounts[(uint8_t)t]; }

    // Lifetime total across reboots (persisted to NVS), unlike the
    // live _typeCounts above which decay when a detection goes stale.
    uint32_t lifetimeTotal() const { return _lifetimeTotal; }

    // Lifetime count PER TYPE, surviving reboots -- the live _typeCounts
    // decay as detections go stale, so they cannot answer "how many Flock
    // cameras have I ever logged". This is the number a long-haul reward
    // would be built on. Returns 0 for an out-of-range type.
    uint32_t lifetimeTypeCount(DetectionType t) const {
        return ((uint8_t)t < (uint8_t)DetectionType::COUNT)
               ? _lifetimeByType[(uint8_t)t] : 0u;
    }

    // Settings-menu "reset stats" action: zeroes the persisted lifetime
    // total and the live per-type counters. Does not touch the log
    // itself — that's clearLog()'s job.
    void resetLifetime();

    // Called from the promiscuous WiFi Rx callback (IRAM_ATTR context).
    // Posts a 6-byte MAC + RSSI for later processing in loop(). ssid is
    // optional (nullptr/empty for probe requests and data frames, which
    // don't carry one) — beacon frames pass the AP's SSID so it can be
    // matched against the SSID-prefix table as a fallback when the OUI
    // itself doesn't match anything (e.g. an Axon/Flock unit in pairing
    // mode, running on a WiFi module OUI we don't otherwise recognize).
    // `pwnagotchi` says the SSID field is carrying a pwnagotchi's own name
    // rather than a network name -- see pwnagotchiName(). It travels as a
    // flag instead of being re-derived in processWiFiQ() because the JSON
    // it came from is in the frame, and the frame is gone by then.
    void IRAM_ATTR postWiFi(const uint8_t* mac, int8_t rssi, uint8_t channel,
                            const char* ssid = nullptr, bool encrypted = false,
                            bool pwnagotchi = false);

    // Called from the promiscuous WiFi Rx callback (IRAM_ATTR context)
    // when a deauthentication management frame is seen. A single
    // frame is completely normal WiFi traffic (a phone disconnecting,
    // an AP restarting) -- it's a BURST that indicates an actual
    // attack, which processDeauthQ() (run from loop()) is what
    // actually decides, via a rolling window + cooldown rather than
    // firing per frame.
    void IRAM_ATTR postDeauth(const uint8_t* mac, int8_t rssi, uint8_t channel);

    // Called from the BLE scan callback when a hit is found.
    void postBle(Detection d);

    // ---- Remote ID ---------------------------------------------------
    // Feeds one raw advertisement through the ASTM F3411 decoder. Safe to
    // call for every advert; it returns immediately for anything that is
    // not a Remote ID message.
    //
    // Only ONE aircraft is kept, the most recent, and it resets whenever a
    // different MAC starts talking. A drone sends Basic ID, Location and
    // System as separate adverts, so a single accumulating record is what
    // turns three partial messages into one useful answer -- and one
    // record is the right number for a screen this size, where the
    // question is "what is that thing" rather than "catalogue the sky".
    void mergeRemoteId(const uint8_t* mac, const uint8_t* payload, uint8_t len);
    const RemoteId::Info& remoteId() const { return _rid; }
    const uint8_t*        remoteIdMac() const { return _ridMac; }

    // Called from the BT Classic inquiry callback when a name match hits.
    void postBtClassic(Detection d);

    // Called from the BLE scan callback while a raw BLE scan is active
    // (see startRawBleScan()) -- every advertisement, not just known
    // signatures.
    void postRawBle(RawBleResult r);

    // ---- Manual raw scanner (CLEAR screen's SCAN button picker) -----
    // Pauses the continuous signature-matched scan above entirely and
    // dedicates the radio to a single focused sweep -- only one of
    // these (or the continuous scan) is ever active at a time, which
    // is what keeps this from needing its own separate memory budget
    // on top of the continuous scan's already-measured heap usage.
    //
    // BLE stays on the same always-running NimBLE scan (just points its
    // callback at postRawBle() instead of the signature matcher) for a
    // fixed focused dwell; WiFi runs a real one-shot WiFi.scanNetworks()
    // AP sweep instead of the continuous promiscuous-frame sniffer.
    // stopRawScan() ends whichever is active and resumes the continuous
    // scan; safe to call even when neither is running.
    void     startRawBleScan();
    bool     rawBleScanDone() const;
    uint8_t  rawBleCount() const { return _rawBleCount; }
    const RawBleResult* rawBleAt(uint8_t idx) const;   // insertion order, nullptr if idx is out of range

    void     startRawWifiScan();
    bool     rawWifiScanDone() const;
    uint8_t  rawWifiCount() const;
    const char* rawWifiSsid(uint8_t idx) const;        // "" if idx is out of range
    int8_t   rawWifiRssi(uint8_t idx) const;
    uint8_t  rawWifiChannel(uint8_t idx) const;
    bool     rawWifiOpen(uint8_t idx) const;            // true = no encryption
    const uint8_t* rawWifiBssid(uint8_t idx) const;     // nullptr if idx is out of range -- for watchWifi()

    void     stopRawScan();

    // ---- Watched target ("stalker tracker") --------------------------
    // Session-only (not persisted to NVS -- resets on reboot). One
    // target at a time; setting a new one replaces whatever was being
    // watched before. Set from the raw-scan results screen (long-press
    // a row). Checked against every BLE advertisement / WiFi frame
    // already being parsed for the continuous scan, regardless of
    // whether it matches a known vendor signature -- watching fires
    // even for a completely generic/unknown device, since that's the
    // whole point. Note: BLE addresses on many modern devices (AirTags,
    // iPhones) rotate periodically specifically to defeat this kind of
    // tracking-by-MAC, so this isn't foolproof for a determined target
    // -- it still catches most consumer gear, which doesn't rotate.
    enum class WatchKind : uint8_t { NONE, BLE, WIFI };
    void watchBle(const uint8_t* mac, const char* name);
    void watchWifi(const uint8_t* bssid, const char* ssid);
    void clearWatch();
    WatchKind watchKind() const { return _watchKind; }
    const char* watchLabel() const { return _watchLabel; }

    // Recent signal-strength history for the current watch target --
    // sampled independently of the alert cooldown above (every ~2s the
    // target is actually seen, not just once per 30s alert), so it
    // fills in fast enough to show a real trend the first time someone
    // looks at the watch-alert screen. Reset whenever the watched
    // target changes. idx 0 = oldest, ascending -- a sparkline reads
    // left-to-right as time moving forward, the opposite convention
    // from logAt()'s newest-first list.
    uint8_t watchRssiCount() const { return _watchRssiCount; }
    int8_t  watchRssiAt(uint8_t idx) const;

    // True exactly once per hit (consumed on read) -- main.cpp polls
    // this once per CLEAR-loop tick to trigger the dedicated
    // watch-alert screen. Cooldown-gated (see WATCH_COOLDOWN_MS) so a
    // target that just sits nearby doesn't re-fire every single
    // advertisement/frame.
    bool watchHitPending();

    // ---- Hunt target (HUNT MODE's live gauge) -------------------------
    // A second, completely independent slot from the watch target above
    // -- picking HUNT on a device no longer overwrites whatever's being
    // passively WATCHed (or vice versa), so you can leave a watch
    // running in the background and go fox-hunt something else entirely
    // without losing it. Same session-only lifetime, same one-target-
    // at-a-time replacement rule, just its own mac/label/RSSI history.
    // No alert flag/cooldown of its own -- HUNT MODE is a screen you're
    // actively looking at already, so there's nothing to pop up over.
    void huntBle(const uint8_t* mac, const char* name);
    void huntWifi(const uint8_t* bssid, const char* ssid);
    void clearHunt();
    WatchKind huntKind() const { return _huntKind; }
    const char* huntLabel() const { return _huntLabel; }
    uint8_t huntRssiCount() const { return _huntRssiCount; }
    int8_t  huntRssiAt(uint8_t idx) const;

    // Called from the BLE scan callback (every advertisement, any
    // mode) -- public for the same reason postBle()/postRawBle() are:
    // the callback lives in a separate class, not a DetectionEngine
    // member.
    void checkWatchBle(const uint8_t* mac, int8_t rssi);
    void checkHuntBle(const uint8_t* mac, int8_t rssi);

    // SD log helper accessor.
    SdLog& sd() { return _sd; }

    // Rough per-channel activity level for the spectrum-waterfall
    // background (0..100, quiet..very active) — not a real dBm
    // reading, just how strong/recent the loudest frame seen on that
    // channel during the last hop cycle was, decaying over time.
    // channel is 1..13; anything else returns 0.
    uint8_t channelActivity(uint8_t channel) const {
        return (channel >= 1 && channel <= 13) ? _channelActivity[channel] : 0;
    }

private:
    // Bumped well past the screen's visible rows on purpose -- ui_log.cpp
    // already renders off logCount()/logAt() dynamically with its own
    // scrollbar, so a bigger ring buffer is pure history depth for
    // free, no UI code to touch. ~200 entries costs ~11KB of RAM
    // against a ~250KB free budget -- a rounding error.
    static const uint8_t  LOG_CAP       = 200;
    static const uint8_t  WIFI_Q_CAP    = 8;
    static const uint32_t STALE_MS      = 60000;
    static const uint32_t ALERT_GRACE_MS= 200;
    static const uint8_t  RAW_BLE_CAP   = 20;

    // Raw (unfiltered) BLE scan results -- see startRawBleScan(). Not a
    // ring buffer like _log: entries are looked up by MAC and updated
    // in place, so this reads as "everything currently visible" rather
    // than a chronological history.
    RawBleResult _rawBle[RAW_BLE_CAP];
    uint8_t      _rawBleCount = 0;

    struct WiFiQEntry {
        uint8_t mac[6];
        int8_t  rssi;
        uint8_t channel;
        char    ssid[33];  // empty string if none (see postWiFi)
        bool    encrypted; // beacon Privacy bit; meaningless without an ssid
        bool    pwnagotchi;// ssid holds a pwnagotchi's name, not a network's
    };

    // WiFi mailbox (filled in IRAM, drained in loop)
    volatile WiFiQEntry _wifiQ[WIFI_Q_CAP];
    volatile uint8_t    _wifiQHead = 0;
    volatile uint8_t    _wifiQTail = 0;

    // Deauth mailbox (filled in IRAM, drained in loop) -- see
    // postDeauth()/processDeauthQ(). Separate from _wifiQ above since
    // this feeds a rolling burst-window counter, not the OUI/SSID
    // signature matcher.
    static const uint8_t  DEAUTH_Q_CAP       = 8;
    static const uint8_t  DEAUTH_THRESHOLD   = 6;      // frames within...
    static const uint32_t DEAUTH_WINDOW_MS   = 3000;   // ...this window...
    static const uint32_t DEAUTH_COOLDOWN_MS = 15000;  // ...then this long before re-firing.
    struct DeauthQEntry {
        uint8_t mac[6];
        int8_t  rssi;
        uint8_t channel;
    };
    // ---- Evil-twin / rogue-AP tracking --------------------------
    // First BSSID seen beaconing each SSID, with the security posture it
    // advertised. A later beacon for that SSID from different hardware
    // *and* disagreeing about encryption is flagged EVILTWIN.
    //
    // The encryption mismatch is the real test, and it exists because
    // the obvious one doesn't work. "Same SSID, different BSSID" is not
    // a rogue -- it is also every mesh network and every dual-band AP,
    // and confirmed on a real mesh here it fires constantly. Even "same
    // SSID, different OUI" over-fires, because vendors ship across
    // several OUI blocks.
    //
    // What a mesh never does is disagree with itself about security:
    // every node on one SSID advertises the same Privacy bit. An evil
    // twin usually must disagree -- cloning a WPA network without the
    // key gets an attacker nothing, so the practical attack is an open
    // twin of an encrypted network, which is exactly what a captive
    // "evil portal" is.
    //
    // The cost, stated plainly: an attacker who advertises matching
    // encryption walks past this. That attacker also can't complete a
    // handshake, so it's a much rarer attack than the one this catches.
    //
    // Fixed table, oldest-evicted -- an unbounded map of every SSID in
    // range is exactly the kind of growth that crashed the BLE path.
    static const uint8_t  AP_CAP = 24;
    struct ApEntry {
        char    ssid[33];
        uint8_t bssid[6];
        bool    encrypted;
    };
    ApEntry  _aps[AP_CAP];
    uint8_t  _apCount = 0;
    uint8_t  _apNext  = 0;          // round-robin eviction cursor
    // Returns true if this beacon looks like an evil twin of an SSID
    // already on file. Records the SSID on first sighting.
    bool noteApBeacon(const uint8_t* bssid, const char* ssid, bool encrypted);

    volatile DeauthQEntry _deauthQ[DEAUTH_Q_CAP];
    volatile uint8_t      _deauthQHead = 0;
    volatile uint8_t      _deauthQTail = 0;
    uint8_t  _deauthWinCount   = 0;
    uint32_t _deauthWinStart   = 0;
    uint32_t _deauthLastFireMs = 0;

    // Watched target -- see the public watchBle()/watchWifi() section
    // above.
    static const uint32_t WATCH_COOLDOWN_MS = 30000;
    WatchKind _watchKind = WatchKind::NONE;
    uint8_t   _watchMac[6] = {0};
    char      _watchLabel[24] = "";
    uint32_t  _watchLastHitMs = 0;
    bool      _watchHitFlag   = false;
    void checkWatchWifi(const uint8_t* mac, int8_t rssi);   // called from processWiFiQ()

    // Watch RSSI history ring buffer -- see watchRssiCount()/watchRssiAt()
    // above. 40 samples at the ~2s sample throttle is a bit over a
    // minute of trend, plenty for "closer or farther" at a glance;
    // costs 40 bytes of RAM.
    static const uint8_t  WATCH_RSSI_CAP       = 40;
    static const uint32_t WATCH_RSSI_SAMPLE_MS = 2000;
    int8_t   _watchRssiHist[WATCH_RSSI_CAP] = {0};
    uint8_t  _watchRssiHead  = 0;
    uint8_t  _watchRssiCount = 0;
    uint32_t _watchRssiLastMs = 0;
    void recordWatchRssi(int8_t rssi);

    // Hunt target -- see the public huntBle()/huntWifi() section above.
    // Deliberately a whole separate mac/label/history from the watch
    // fields above rather than reusing them, so HUNT and WATCH can
    // point at two different devices at once. checkHuntWifi() is called
    // from processWiFiQ() the same way checkWatchWifi() is.
    WatchKind _huntKind = WatchKind::NONE;
    uint8_t   _huntMac[6] = {0};
    char      _huntLabel[24] = "";
    int8_t    _huntRssiHist[WATCH_RSSI_CAP] = {0};
    uint8_t   _huntRssiHead  = 0;
    uint8_t   _huntRssiCount = 0;
    uint32_t  _huntRssiLastMs = 0;
    void checkHuntWifi(const uint8_t* mac, int8_t rssi);
    void recordHuntRssi(int8_t rssi);

    // The most recently decoded Remote ID broadcast, and whose it is.
    RemoteId::Info _rid;
    uint8_t        _ridMac[6] = {0, 0, 0, 0, 0, 0};

    // Detection log
    Detection  _log[LOG_CAP];
    uint8_t    _logCount = 0;            // number of valid entries (<= LOG_CAP)
    uint8_t    _logHead  = 0;            // next slot to write
    Detection* _latest   = nullptr;      // pointer into _log or null
    uint32_t   _latestChangeMs = 0;
    DetectionType _lastAlertType = DetectionType::UNKNOWN;

    // Live counters (one per DetectionType)
    uint16_t _typeCounts[(uint8_t)DetectionType::COUNT] = {0};

    SdLog       _sd;
    Preferences _prefs;
    uint32_t    _lifetimeTotal = 0;
    // Persisted as ONE blob rather than a key per type. NVS allocates in
    // 32-byte entries, so 14 separate uint32 keys would burn 14 entries
    // where the whole array fits in three, and it means one write per
    // detection instead of one per type.
    uint32_t    _lifetimeByType[(uint8_t)DetectionType::COUNT] = {0};
    void        saveLifetimeByType();

    // Promiscuous mode only ever receives on whatever channel the
    // radio is currently tuned to — without actively hopping, the
    // sniffer stays parked on one channel and misses anything
    // transmitting on the other 12. See hopChannel().
    uint8_t     _wifiChannel = 1;
    uint32_t    _lastHopMs   = 0;

    // Index 1..13; 0 is unused. Fed from every captured mgmt/data
    // frame in processWiFiQ() (not just ones that match a known
    // signature) so it reflects real ambient channel activity, then
    // decayed over time in loop().
    uint8_t     _channelActivity[14] = {0};
    uint32_t    _channelLastMs[14]   = {0};

    void pushLog(const Detection& d);
    void processWiFiQ();
    void processDeauthQ();
    void expireStale();
    void hopChannel();
    void decayChannelActivity();
};
