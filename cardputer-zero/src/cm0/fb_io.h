// SquachWatch-CM0 -- the panel and the keyboard without a compositor.
//
// The device's app launcher (M5CardputerZero-APPLaunch) and the apps it
// starts draw straight to /dev/fb0 and read the keyboard's event device;
// there is no WAYLAND_DISPLAY in a launched app's environment. `squachd
// --fb` does the same:
//
//   fb0       panel-mipi-dbid, 320x170, 16 bpp RGB565 -- the firmware's own
//             frame format, so a frame is a row-by-row copy (stride aware).
//   keyboard  the evdev device named "tca8418c" (or SQUACH_KBD_DEV when not
//             AT_SECURE), grabbed (EVIOCGRAB) so keys do not also reach the
//             launcher or the labwc session behind it; released on close.
//
// Both are opened by fbIoOpen() BEFORE the UI thread switches to the user's
// file identity (user_fs.h): fb0 is root:video and the event device
// root:input, and that switch keeps root's supplementary groups.
#pragma once
#include <cstdint>
#include <vector>

namespace cm0 {

struct KeyEvent { uint16_t code; bool down; bool repeat; };

// Open fb0 and the keyboard. False (with the reason logged) if either fails.
bool fbIoOpen();
void fbIoClose();

// Copy a w x h RGB565 frame to the panel if it differs from the last one.
void fbPush(const std::vector<uint16_t>& px, int w, int h);
// Clear the panel (on exit, so the launcher does not redraw over our frame
// a strip at a time).
void fbClear();

// The keyboard's fd, for poll(); -1 when not open.
int  kbdFd();
// Drain pending key events (non-blocking).
void kbdRead(std::vector<KeyEvent>& out);

}   // namespace cm0
