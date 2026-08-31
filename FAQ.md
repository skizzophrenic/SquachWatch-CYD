# FAQ

Hey. It's me, the Sasquach. Yes, I know how to use GitHub. No, I don't know why that surprises people every time.

This is the FAQ for SquachWatch-CYD — the thing that turns a $15 screen into a pocket-sized "is someone watching me" detector. Here's how it all works, explained the way I'd explain it to you if you cornered me at a gas station at 2am, which, statistically, is likely.

Just want to flash a board and go? **[Open the web flasher](https://skizzophrenic.github.io/SquachWatch-CYD/)** — no build tools, no account, just a browser.

## What even is this?

SquachWatch-CYD sniffs the 2.4 GHz airwaves — WiFi and Bluetooth — for the specific fingerprints of surveillance gear: Flock Safety license-plate cameras, Axon body cams, Ray-Ban Meta glasses, card skimmers, AirTags/Tile/Samsung/Google trackers, Ring doorbells, gunshot detectors, drones, ALPR units, and generic covert cameras. Thirteen detection types total. If it broadcasts a recognizable signature, this thing has a decent shot at flagging it.

It's not magic. It's not X-ray vision. It's a $15 board doing pattern-matching on radio noise, wearing a mascot costume. But it's real pattern-matching on real, documented signatures — not vibes.

## What hardware do I need?

Two options right now:

- **The 2.8" CYD** (ESP32-2432S028R) — the original, the stable one, the one I'd actually recommend if you just want it to work. ~$15-20 on AliExpress/Amazon.
- **The 3.5" CYD** (Sunton ESP32-3248S035R) — bigger screen, bigger vibes, but it's genuinely still under construction. Touch doesn't work on it yet and I'm still chasing down a color quirk. If you want to tinker and don't mind rough edges, go for it. If you want it to just work today, get the 2.8".

Either way: no GPS, no buzzer, no extra modules. The board *is* the whole device. Plug it into USB-C and you're done.

## Do I need to build it myself?

No. That's what the [web flasher](https://skizzophrenic.github.io/SquachWatch-CYD/) is for — plug your board into a computer running Chrome, Edge, or Brave, hit Connect & Install, and the firmware goes straight from your browser onto the board. No compiler, no IDE, no me judging your PlatformIO setup (I would never. probably).

If you *do* want to build from source — maybe you're modifying something, maybe you just don't trust browsers with USB access, respectable — grab [PlatformIO](https://platformio.org/) and follow [docs/BUILD.md](docs/BUILD.md).

## How does detection actually work?

Every detection type has its own fingerprint — a WiFi OUI prefix, a BLE service UUID, a manufacturer ID, sometimes an SSID pattern. The board passively listens (it never transmits anything to provoke a response — this is 100% receive-only, no active probing) and checks every WiFi/BLE packet it overhears against that list. Match found? You get a full-screen ALERT: what it thinks it saw, how confident it is, the MAC, RSSI, channel, and a little radar widget because I have a flair for the dramatic.

Confidence matters — some signatures are rock-solid (Flock, Axon, Meta glasses), some are best-effort (AirTag, Raven). The ALERT screen tells you which is which so you're not treating a maybe like a certainty. Full technical breakdown, per-type, with sources: [docs/DETECTIONS.md](docs/DETECTIONS.md).

## Is this legal? Am I going to get in trouble for owning one?

Passively listening to radio broadcasts that devices are already sending into the public air is not the same as hacking into anything. You're not transmitting, not intercepting private communications, not breaking into a network. You're reading what's already being shouted into the void and I'm just built to notice.

That said — I'm a hairy cryptid mascot, not a lawyer. Know your local laws, use your brain, don't do anything stupid.

## What's with the walking, talking Sasquach on the screen?

That's Squachy. He's the whole point, honestly — the detection is the *why*, Squachy is the *fun*. He reacts to what the device sees, cracks jokes when nothing's happening, remembers how many times you've petted him, unlocks cosmetics the longer you use the device, and occasionally does something unhinged with confetti if the mood strikes. He grows through stages as your lifetime detection count climbs. Tap him. He likes that. Don't overthink it, he's a good guy.

Don't want him? **Settings → Boring Mode** turns off his on-screen presence entirely — detection features stay 100% intact, you just get a plain, no-mascot detector if that's more your speed. No judgment. Some people just want the tool.

## Does it log anything?

Yes, if you drop in a microSD card. Every detection gets appended to a daily CSV (`squachwatch-<day>.log`) — timestamp, type, RSSI, MAC, channel, vendor, SSID. No SD card, no logging — the device just runs live off the screen and its in-memory counters. No GPS on this board, so logs are local-timeline only, but still genuinely useful for "wait, was that camera here yesterday too" situations.

## Touch feels off / the screen rotated weird, help

Long-press the title bar (between the two corner icons) on the CLEAR or LOG screen to enter the calibration flow — follow the on-screen prompts. There's also a recovery move: hold the screen anywhere right after boot for about a second to wipe a bad calibration back to defaults, in case you calibrated it into oblivion.

Rotation lives in the top-right corner of the title bar — tap to cycle through all four orientations.

## Something's broken. Where do I yell about it?

[Open an issue](https://github.com/skizzophrenic/SquachWatch-CYD/issues). Tell me what board you've got, what you expected, what actually happened, and ideally a screenshot or the serial log if you can grab one. I read these. I fix things. Usually within the same session, if we're being honest, because I have the attention span of a golden retriever and apparently unlimited caffeine.

## Can I contribute?

Sure. PRs welcome, especially new detection signatures with real sources behind them (see [docs/DETECTIONS.md](docs/DETECTIONS.md) for the format — I take provenance seriously, I don't want to flag your neighbor's baby monitor as a Flock camera because someone guessed at an OUI). The whole project is MIT-licensed. Fork it, break it, make it weirder, send it back.

## Any relation to talkingsasquach.com?

Yes — that's me, the actual channel. This device and the whole SquachWare vaporwave aesthetic belong to that brand. If you like the look and feel of this thing, that's where the rest of it lives.

---

Stay squachy out there.
