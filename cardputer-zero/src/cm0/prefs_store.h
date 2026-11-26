// SquachWatch-CM0 -- where Preferences (the firmware's NVS) live on disk.
//
// The ESP32's NVS survives power loss mid-write; a plain fopen("wb") does
// not (truncate first, write after -- pull the battery between the two and
// every setting in that namespace is gone). So a namespace is written to a
// temp file in the same directory, fsync'd, renamed over the old one, and
// the directory fsync'd: a reader sees the old file or the new one, never
// half of either.
//
// Persistence is OFF until prefsSetDir() is called: --shot and --selftest
// run on in-memory preferences (deterministic, and the selftest's squad-TX
// toggle must never land in the user's real settings). The live app turns
// it on with prefsDefaultDir().
#pragma once
#include <string>

namespace cm0 {

// The store directory for the live app: $SQUACHWATCH_STATE when set (and
// the process is not AT_SECURE), else <home>/.local/state/squachwatch/nvs
// where <home> is SUDO_USER's when running under sudo -- the settings
// belong to the person, not to root. Created 0700 (owned by that user).
// Empty when no home can be found.
std::string prefsDefaultDir();

// Enable persistence in `dir` (empty = off). Must be called before setup():
// namespaces load at Preferences::begin().
void prefsSetDir(const std::string& dir);
const std::string& prefsDir();

// One namespace's text blob. Load: false when absent/unreadable (first
// run). Save: atomic replace as above; false (logged once) on failure.
bool prefsLoad(const std::string& ns, std::string& blob);
bool prefsSave(const std::string& ns, const std::string& blob);

// Saves since start (the write rate is a wear question on an SD card).
unsigned long prefsSaveCount();

}   // namespace cm0
