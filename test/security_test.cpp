// SquachWatch-CYD — the PIN lock's arithmetic: lengths, the backoff, the
// duress PIN, the ten-guess wipe, and that a reboot changes none of it.
//
// Through the emulator's Preferences shim, pointed at a directory, so a
// "reboot" -- Security::begin() again -- reads back only what was written to
// storage, exactly as the device's NVS would.
#include "test_util.h"
#include "security.h"
#include "ignore_list.h"
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

using Security::Check;

static bool contains(const uint8_t* hay, size_t n, const char* needle) {
    const size_t m = strlen(needle);
    for (size_t i = 0; i + m <= n; i++)
        if (memcmp(hay + i, needle, m) == 0) return true;
    return false;
}

static bool phraseStored() {
    Preferences m;
    m.begin("meshtalk", true);
    return m.isKey("phrase");
}

static void storePhrase() {
    Preferences m;
    m.begin("meshtalk", false);
    m.putString("phrase", "GIBSON MOTHMAN PHREAK NESSIE ZEROCOOL");
}

int main() {
    mkdir("out", 0755);
    mkdir("out/secnvs", 0755);
    (void)!system("rm -f out/secnvs/*.nvs");
    setenv("SQUACHSIM_NVS", "out/secnvs", 1);
    Security::begin();

    suite("Off until a PIN is set");
    {
        ck("no PIN at first", !Security::enabled());
        Security::lock();
        ck("and lock() cannot lock a device without one", !Security::locked());
    }

    suite("Setting a PIN");
    {
        Security::setPinLength(Security::PinLen::SIX);
        ck("the length is chosen first", Security::pinLength() == 6);
        ck("a PIN of another length is refused", !Security::setPin("1234"));
        ck("the chosen length is taken", Security::setPin("123456") && Security::enabled());
        Security::setPinLength(Security::PinLen::FOUR);
        ck("and the length is fixed once a PIN exists", Security::pinLength() == 6);
        ck("verify() accepts it", Security::verify("123456"));
        ck("and refuses anything else", !Security::verify("654321") && !Security::verify("12345"));
    }

    suite("The digits are never stored");
    {
        Preferences p;
        p.begin("security", true);
        uint8_t h[32];
        const size_t n = p.getBytes("hash", h, sizeof h);
        ck("a 32-byte hash is", n == 32);
        ck("and the digits are not in it", !contains(h, n, "123456"));
    }

    suite("Unlocking");
    {
        Security::lock();
        ck("it locks", Security::locked());
        ck("a wrong PIN leaves it locked",
           Security::check("000000", 1000) == Check::WRONG && Security::locked());
        ck("the right one unlocks", Security::check("123456", 1000) == Check::OK && !Security::locked());
        ck("and clears the count", Security::failCount() == 0);
    }

    suite("Five free guesses, then a wait that doubles");
    {
        Security::lock();
        const uint32_t t = 2000;
        for (int i = 0; i < 4; i++) Security::check("111111", t);
        ck("no wait after four", Security::lockoutRemainingMs(t) == 0);
        Security::check("111111", t);
        ck("thirty seconds after the fifth", Security::lockoutRemainingMs(t) == 30000);
        ck("even the right PIN is refused while waiting",
           Security::check("123456", t + 1000) == Check::WRONG && Security::locked());
        ck("the wait runs down", Security::lockoutRemainingMs(t + 30000) == 0);
        Security::check("111111", t + 30000);
        ck("sixty after the sixth", Security::lockoutRemainingMs(t + 30000) == 60000);
    }

    suite("A reboot resets none of it");
    {
        Security::begin();
        ck("it comes back locked", Security::locked());
        ck("with the wait served from boot", Security::lockoutRemainingMs(50) == 60000);
        ck("and then the right PIN unlocks", Security::check("123456", 50 + 60000) == Check::OK);
        Security::setLockAtBoot(true);
        Security::begin();
        ck("LOCK AT BOOT locks a clean boot", Security::locked());
        Security::check("123456", 70000);
        Security::setLockAtBoot(false);
        Security::begin();
        ck("and without it a clean boot is open", !Security::locked());
    }

    suite("The duress PIN");
    {
        ck("cannot be the real PIN", !Security::setDuress("123456"));
        ck("must be the same length", !Security::setDuress("9999"));
        ck("is set", Security::setDuress("999999") && Security::hasDuress());
        ck("is recognised, and the real PIN is not it",
           Security::isDuress("999999") && !Security::isDuress("123456"));
        storePhrase();
        const uint8_t mac[6] = { 1, 2, 3, 4, 5, 6 };
        IgnoreList::add(mac);
        Security::lock();
        ck("entering it unlocks, as the real one would",
           Security::check("999999", 80000) == Check::DURESS && !Security::locked());
        ck("the message phrase is gone", !phraseStored());
        ck("the ignore list is empty", !IgnoreList::contains(mac));
        ck("the duress PIN is spent", !Security::hasDuress());
        ck("the real PIN is untouched", Security::verify("123456"));
        Security::begin();
        ck("and a reboot finds no duress PIN either", !Security::hasDuress());
    }

    suite("Ten wrong guesses wipe, when asked to");
    {
        Security::setWipeOnFail(true);
        storePhrase();
        Security::lock();
        uint32_t t = 100000;
        Check r = Check::WRONG;
        for (int i = 0; i < 10; i++) {
            t += Security::lockoutRemainingMs(t);
            r = Security::check("222222", t);
        }
        ck("the tenth wipes", r == Check::WIPED);
        ck("the phrase is gone", !phraseStored());
        ck("and it is still locked", Security::locked());
        ck("with the count cleared, so the owner is not made to wait",
           Security::failCount() == 0 && Security::lockoutRemainingMs(t) == 0);
        ck("the real PIN still opens it", Security::check("123456", t) == Check::OK);
    }

    suite("Without WIPE AFTER 10, guessing never wipes");
    {
        Security::setWipeOnFail(false);
        storePhrase();
        Security::lock();
        uint32_t t = 200000;
        for (int i = 0; i < 12; i++) {
            t += Security::lockoutRemainingMs(t);
            Security::check("333333", t);
        }
        ck("twelve wrong and the phrase is still there", phraseStored());
        t += Security::lockoutRemainingMs(t);
        Security::check("123456", t);
    }

    suite("Turning it off");
    {
        Security::disable();
        ck("no PIN, no duress, unlocked",
           !Security::enabled() && !Security::hasDuress() && !Security::locked());
        Security::begin();
        ck("and it stays off across a reboot", !Security::enabled());
    }

    return report();
}
