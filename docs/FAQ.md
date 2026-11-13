# FAQ

Hey. It's me, the Sasquach. Yes, I know how to use GitHub. No, I don't know why that surprises people every time.

This is the FAQ for SquachWatch, the firmware that turns a cheap ESP32 screen into a pocket-sized "is someone watching me" detector. Answers first, jokes second. Mostly.

Just want to flash a board and go? **[Open the web flasher](https://squachwatch.com/)**. Want to poke at it before you buy anything? **[Try the emulator in your browser](https://squachwatch.com/emulator/)**.

## Contents

- [What is this thing?](#what-is-this-thing)
- [Which board should I buy?](#which-board-should-i-buy)
- [How do I install it?](#how-do-i-install-it)
- [How do I update it?](#how-do-i-update-it)
- [Does it transmit anything?](#does-it-transmit-anything)
- [What does it store, and who can read it?](#what-does-it-store-and-who-can-read-it)
- [Battery and power](#battery-and-power)
- [Does it make any noise?](#does-it-make-any-noise)
- [An alert popped up. Now what?](#an-alert-popped-up-now-what)
- [False positives](#false-positives)
- [SquachMesh and squads](#squachmesh-and-squads)
- [Touch is off, the screen is white, the colours are wrong](#touch-is-off-the-screen-is-white-the-colours-are-wrong)
- [GPS and LoRa (watches only)](#gps-and-lora-watches-only)
- [What's with the Sasquach on the screen?](#whats-with-the-sasquach-on-the-screen)
- [Is this legal?](#is-this-legal)
- [Bugs, contributing and the license](#bugs-contributing-and-the-license)

## What is this thing?

SquachWatch listens to the WiFi and Bluetooth around you and flags the specific fingerprints of surveillance gear. Seventeen detection types: Flock Safety cameras, Axon body cams and TASERs, camera glasses (Ray-Ban Meta and friends), Bluetooth card skimmers, Raven gunshot detectors, AirTags, Samsung, Google and Tile trackers, Remote ID drones, plate readers, generic and covert IP cameras, Ring doorbells, deauth floods, evil-twin access points, iBeacons (off by default) and hacking hardware like a Flipper Zero or a Pwnagotchi.

It's not magic and it's not X-ray vision. It's a small board doing pattern-matching on radio noise, wearing a mascot costume. But it matches real, documented signatures, each with a confidence grade, not vibes. The full per-type breakdown, with sources, lives in [DETECTIONS.md](DETECTIONS.md).

## Which board should I buy?

**The short answer: the 2.8" CYD, ESP32-2432S028R.** It's the original, it's cheap, it's everywhere, and it's what I test on most. If you want a bigger, nicer screen without a BETA label, the 3.2" capacitive (ESP32-2432S032C) is the next step up.

Everything on the [web flasher](https://squachwatch.com/), with the names it uses:

| Board | Touch | On the flasher | Good to know |
|---|---|---|---|
| 2.8" CYD, ST7789 (ESP32-2432S028R) | Resistive | Yes | The usual screen. The 80 MHz box is ticked by default. |
| DIYmalls 2.8" (JC2432W328C) | Capacitive | Yes | Named flasher option; shares the ST7789 `cyd` / `cyd-fast` firmware, which detects touch at startup. [Hardware and battery notes](DIYMALLS-28.md). |
| 2.8" CYD, ILI9341 (ESP32-2432S028R) | Resistive | Yes | The older screen on the same board. Also the build for the ESP32-32E 2.8" (E32R28T). Leave 80 MHz unticked. |
| 3.2" resistive (Freenove FNK0103) | Resistive | Yes | |
| 3.2" capacitive (ESP32-2432S032C) | Capacitive | Yes (NEW) | Already runs at 80 MHz. |
| 3.5" resistive (ESP32-3248S035R) | Resistive | BETA | 80 MHz only. Also the build for the ESP32-32E 3.5" (E32R35T). Says BETA on its own boot screen. |
| 3.5" capacitive (ESP32-3248S035C) | Capacitive | BETA | 80 MHz only. Contributed by VV-B0Y. |
| AWOK 2.4" (ESP32 Marauder v6.1) | Resistive | Yes | No rotate button; it stays in one orientation. |
| RL Phantom 2.4" (ESP32-2432S024R) | Resistive | Yes | Only the resistive one (R). The capacitive one isn't on the flasher. |
| Freenove ESP32-S3 2.8" (FNK0104) | Capacitive | BETA | The release notes name the FNK0104B, the touch version. |
| Elecrow CrowPanel Advance 7.0 | Capacitive | BETA | Draws the small-screen layout doubled. Has a buzzer. Notes in [CROWPANEL7.md](CROWPANEL7.md). |
| RockBase NM-CYD-C5 2.8" (ESP32-C5) | Resistive | BETA | The only board that scans 5 GHz WiFi too. Contributed by quietradio. Notes in [NM-CYD-C5.md](NM-CYD-C5.md). |
| M5Stack StickS3 | None, two buttons | BETA | Tiny 1.14" screen, landscape only. Front click next, front hold select, side click back, side hold home. Shake and knock poke Squachy. Controls in [SMALL-BOARDS.md](SMALL-BOARDS.md). |
| M5Stack Cardputer ADV | None, keyboard | BETA | Not the original Cardputer. Arrows move, ENTER presses, ESC goes back, hold ESC for home, letters are hotkeys. Optional EXT screen and Grove GPS for wardriving. Controls in [SMALL-BOARDS.md](SMALL-BOARDS.md). |
| LilyGo T-Watch S3 / S3 Plus | Capacitive | BETA | On your wrist. Battery, buzz, LoRa listening; the S3 Plus adds GPS. |
| M5Stack Tab5 | Capacitive | Test builds only (`?lab=1`) | Experimental. A 5" tablet whose radio is a separate chip that needs our firmware first. Notes in [TAB5.md](TAB5.md). |

Not sure which one you've got? Type the code on the back into the flasher's box. `JC2432W328C` selects the named DIYmalls option; `ESP32-2432S028R` selects the original CYD entry. For that CYD family, the last letter is the touch: R is resistive (press), C is capacitive (tap).

BETA means it runs the whole firmware but has had less time on real hardware than the 2.8". It works; it just hasn't earned the right to be boring yet.

## How do I install it?

1. Plug the board into a computer with a USB cable that carries data (charge-only cables are the number one cause of "it won't connect").
2. Open [squachwatch.com](https://squachwatch.com/) in Firefox, Chrome, Edge or another Chromium browser. It has to be a computer: phones and tablets can't flash, whatever browser they run.
3. Pick your board, hit **CONNECT & INSTALL**, pick the port, wait.

No compiler, no IDE, no account.

**Board doesn't show up as a port?** Most CYDs use a CH340 USB chip, and Windows usually needs its driver. The flasher links it for Windows, macOS and Linux.

**Shows up but won't connect?** Hold the **BOOT** button on the back while you plug the cable in, then try again. Some boards need that to go into download mode.

**On the newest build and stuck in a boot loop?** The flasher can install an older release too. Flash the one before it and [tell me](https://github.com/skizzophrenic/SquachWatch-CYD/issues).

On first boot it'll walk you through a touch calibration, a colour check and a quick intro from Squachy. Then it's detecting.

Want to build from source instead? Grab [PlatformIO](https://platformio.org/) and follow [BUILD.md](BUILD.md).

## How do I update it?

Go to **Settings > SYSTEM > UPDATE FIRMWARE**. The screen shows the version you're running and offers:

- **UPDATE OVER WIFI.** Joins one of your saved WiFi networks (or asks you to pick one and type the password), checks squachwatch.com, and shows UPDATE AVAILABLE or YOU'RE UP TO DATE. Tap INSTALL. Detection and Bluetooth stay off until the board restarts.
- **UPDATE OVER BLUETOOTH (BETA).** Only on the S3 boards (T-Watch, StickS3, Cardputer ADV, Freenove S3, CrowPanel); the CYDs dropped it in v1.22.0 to free up memory. Open [squachwatch.com/update](https://squachwatch.com/update/) in Chrome or Edge on a computer, or Chrome on Android, and type the code the board shows. Brave blocks website Bluetooth, and iPhone and iPad browsers can't do it at all.
- **UPDATE SQUAD.** Once one board is updated, this tells every squad board in range to update to the same version. More on that in [SquachMesh and squads](#squachmesh-and-squads).
- **SWITCH TO.** Goes back to the version in the other firmware slot. Your settings stay.

Or just run the web flasher again. That always works, and it's the answer to any "it won't update" problem.

**Things worth knowing:**

- Every update has to be signed for your exact board, or it's refused. An older release than the one you're running is refused too; reinstalling the same one is allowed, because that's a repair.
- After a new version starts, **leave it on for 30 seconds** so it can confirm itself. If it doesn't make it, it rolls back to the old one on its own.
- If you have a saved WiFi network, the board checks for a newer release for a few seconds at boot and tells you (it never installs on its own). The SYSTEM row then reads UPDATE. Don't want that? **UPDATE CHECK** on the SYSTEM page turns it off.
- Since v1.33.0, a failed WiFi update says why: the network wants you to sign in first, the network blocks squachwatch.com, the site didn't answer in time, the site is having trouble, the site wants a secure link the board can't make, the release can't be installed over the air yet, or the board ran out of memory. Each one says what to try, and some add a short code in brackets, like "(name lookup failed)", which is worth pasting into a bug report. When the router can't look up squachwatch.com, the board tries the site's fixed addresses on its own. Every failure message also tells you whether your current version is untouched (it almost always is).
- **Stuck on v1.13 through v1.19?** WiFi updates on those versions stall partway through. Use the USB flasher once and WiFi updates work again after that.

## Does it transmit anything?

Yes, a little, and I'd rather you hear it from me. Older versions of this FAQ said "100% receive-only". That stopped being true a while ago. Here's everything that goes out and how to stop it:

| What | When | How to turn it off |
|---|---|---|
| **Bluetooth scan requests.** To get a device's name, the scanner sometimes asks it for more ("active" scanning). It starts passive and only asks while the room is quiet enough. Like any Bluetooth scanner that asks, the board itself is briefly visible to anything listening for those requests. | While detecting | There's no menu switch for this. |
| **A short WiFi visit at boot.** If you've saved a WiFi network, the board joins it for a few seconds at boot, asks squachwatch.com for the latest version of its build, sets its clock over the internet, and lets go before detection starts. | Every boot, only with a saved network | **UPDATE CHECK** to OFF (Settings > SYSTEM), or remove your networks under **WIFI NETWORKS**. |
| **WiFi for an update you asked for.** | Only when you tap UPDATE OVER WIFI, or accept a squad update | Don't tap it. |
| **A Bluetooth update server.** S3 boards only. | Only while the UPDATE OVER BLUETOOTH screen is open | Leave that screen. |
| **SquachMesh adverts.** A Bluetooth advert every 1.5 seconds with a fixed address, your Squachy's name and outfit. Messages, heads-ups and squad updates ride on it. | Only with **TRANSMIT** on | It's **off by default**, behind a warning screen that literally says THIS MAKES YOU TRACKABLE. Settings > SQUACHMESH > TRANSMIT. |

The WiFi detection itself just listens, and so does the manual **WIFI SCAN**: both ask the radio for a *passive* sweep, which hears the beacons networks send out anyway without sending anything itself.

That last part is new, and the old behaviour belongs in this table even though it is now gone. Until this release the manual WiFi sweep used Arduino's default scan, which is an **active** one: it sent a probe request on every channel, with this board's address in it, every time you opened that screen. It was never a deliberate choice -- the setting is the third argument of a function that was being called with one -- and this table, which promises you everything that goes out, never mentioned it. It is passive now, and `tools/check_emissions.sh` fails the build if any scan in the firmware goes back to active without a line saying why.

LoRa on the watches only listens. Nothing else talks.

**PRIVACY MODE** (Settings > SYSTEM) is not a radio switch. It's for filming: addresses show as AA:BB:CC:XX:XX:XX and device and network names are cut to three characters, on the screen only. The log keeps the real values.

## What does it store, and who can read it?

**On the board:**

- **The LOG, across restarts.** Since v1.12.0 the board keeps a "black box" in its flash: about 1,800 sightings and the last hundred or so boots, including what happened if one of them was a crash. After a restart the LOG comes back with the newest sighting of up to 200 devices, in grey so it sits quietly under what's live. **CLR** on the LOG screen clears it.
- **A microSD card, if you put one in.** Every detection is also appended to a daily CSV file on the card. No card, no file; the black box still works.
- **Your settings**, the ignore list, saved WiFi passwords (up to six networks) and, if you use SquachMesh, your squad phrase and key.
- **On the T-Watch S3 Plus with WARDRIVE on**, a log of every network and device it heard with where it heard them. See [GPS and LoRa](#gps-and-lora-watches-only).

None of this leaves the board unless you plug it in and ask for it.

**Who can read it:** anyone holding the board, unless you lock it. **Settings > SECURITY** has:

- **PIN LOCK** with a 4, 6 or 8 digit PIN, **AUTO-LOCK** (on sleep, or after 1 to 30 idle minutes) and **LOCK AT BOOT**. Detection keeps running while locked; **ALERTS LOCKED** decides how much an alert shows to a stranger (FULL, TYPE ONLY or NONE).
- **DURESS PIN.** A second PIN that looks like it unlocks the board, but first wipes it: the squad phrase and key, saved WiFi passwords, the ignore list, the black box log and crash history, the SD card's log files and the watch's wardrive log. Then it restarts quietly and comes up looking like an ordinary, empty SquachWatch. Your other settings survive the wipe.
- **WIPE AFTER 10.** The same wipe after ten wrong guesses.

Be honest with yourself about what the PIN does: it stops a snoop who picks the board up. It does not stop someone with a USB cable and some time, because the flash can be read directly. The board tells you this when you set the PIN, too.

## Battery and power

**Most boards run off USB.** Plug them into a power bank and off you go. Settings > SYSTEM > **LAST RUN** tells you how long the previous power-up lasted, which is the easiest way to find out what your battery is really worth.

**POWER SAVER** (Settings) has the knobs: a master switch, SCREEN TIMEOUT, how far to dim, the frame rate when idle, the CPU clock, and whether an alert wakes the screen. It's off by default on everything except the watch.

**CHARGE MODE** (Settings, every board except the watch) turns off the radios, the status light and the screen so a battery charges faster. Tap the dark screen to see how long it's been charging; tap again while that shows to wake the board.

**The BOOT button** on the CYD-style boards: a short press turns the screen off, and only another press turns it back on. That's **POCKET MODE**: a touch won't wake it, so a board in a pocket or a bag stays dark. Alerts still light it for their card and then it goes dark again. Hold BOOT for about a second to start CHARGE MODE. On the Freenove ESP32-S3 2.8" and 3.5", which have a battery connector, holding BOOT is the power switch instead: at about a second it says KEEP HOLDING, at three it powers off, and a press of BOOT turns it back on. CHARGE MODE is on the Settings list there.

**The speaker.** The Freenove ESP32-S3 2.8" and 3.5" have one. SOUND in Settings is OFF (the default), ALERTS (a device you've never seen, a watch-list hit, a low battery) or ALL (those plus a squad message, a visitor, throws and landings, C1iPPY, the toaster and the lamp noticing the cable). VOLUME is 0-100% on its - and + buttons; each press plays a sample. Nothing plays while the screen is dimmed or at night (11 pm to 5 am), except a watch-list hit.

**The battery lamp.** On the Freenove ESP32-S3 2.8" and 3.5" and the T-Watch, a little lava lamp under the menu button is the battery: the lava is the charge. On the cable, bubbles stream up and a rainbow runs through the cap and base; under 10% it flickers. The Freenove boards work out the cable from the battery voltage jumping when you plug in, so it can take a few seconds to notice. Boards with no battery don't show a lamp.

The DIYmalls JC2432W328C has a physical BAT connector and battery switch, but the board's battery-sensing circuit and charger have not been verified. SquachWatch does not show a battery percentage for it, and USB power must not be taken as evidence that USB charging is supported. See the [JC2432W328C hardware guide](DIYMALLS-28.md) before connecting a battery.

**On the T-Watch:**

- POWER SAVER is on by default. On battery the screen times out whatever you set; on the charger it stays lit like a desk clock.
- The crown turns the screen off, and only the crown turns it back on (POCKET MODE).
- **WATCH SETTINGS** (the first row in Settings) shows the BATTERY and has the battery knobs: RADIOS (how much of the time WiFi gets; Bluetooth is always on by default), BLE LISTEN, SLEEP CPU and BUZZ.
- **TAGS + RINGS** is set to LOG ONLY by default, so trackers and Ring cameras are logged without waking the screen or buzzing. A day out otherwise means a lot of AirTags in passing cars.

## Does it make any noise?

Mostly no. Two exceptions:

- **The T-Watch buzzes.** A double tap per alert (at most one every 10 seconds), three long buzzes for a device you're watching, and one buzz for a new LoRa message. **BUZZ** in WATCH SETTINGS sets it to OFF, HIGH, MED (the default) or LOW.
- **The CrowPanel 7" has a buzzer.** It's off by default. **BUZZER** under BEHAVIOR set to NEW ONLY gives one chirp for a device the board hasn't seen before, never at night (11 pm to 5 am, once the clock is set), never while the screen is dimmed, and never at boot.

Nothing else makes a sound. The CYD's RGB status light is the loudest it gets, and **STATUS LIGHT** under APPEARANCE turns that off too.

## An alert popped up. Now what?

A full-screen alert means one detection matched one signature. It tells you what it thinks it saw, how confident that match is, the signal strength, and the address. FIRST OF ITS KIND means this board has never caught that type before; AT NIGHT means what it sounds like. The card closes when you tap it, or by itself after ten seconds.

The buttons:

- **MORE INFO**: what this thing is and why it matched.
- **IGNORE**: never alert on this exact device again. It's still logged and counted, with an IGNORED tag on its LOG row. Tap it again (it reads MUTED) or use Settings > IGNORED to undo it.
- **SNOOZE**: quiet about this device for a while. Tap once for 10 minutes, twice for 30, three times for an hour, four times for 4 hours. The button shows the time, and it locks in three seconds after your last tap. After the first tap the card says TAP AGAIN: LONGER. For good, use IGNORE.
- **HUNT**: a hot/cold fox hunt for that device, GETTING WARMER or GETTING COLDER as you move. STOP HUNT ends it.

**Too many alerts?**

- **AUTO SNOOZE** (Settings > BEHAVIOR): OFF, AFTER 5 or AFTER 10. Past its allowance a device has to come 7 dB closer than it ever has to alert again, and the allowance comes back after thirty minutes. Made for the doorbell across the street that takes your screen every time you walk past.
- **ALERT FILTER**: ALL, MED+ or HIGH ONLY. Lower-confidence matches still go in the LOG; they just don't take over the screen.
- **TYPE FILTER**: switch whole detection types on or off.
- **Spam floods.** If a type suddenly produces a pile of addresses that are heard once and never again (somebody's tracker spammer, for example), you get one alert with a "SPAM: N FAKE TAGS" banner, and then that type is logged quietly until five minutes pass without more.

**Keeping an eye on one device.** Long-press any row in the LOG and pick **WATCH**. It works on any device, even one that matched nothing. When it comes back you get **LOCKED ON**: a radar scope with the signal and whether it's getting CLOSER or FURTHER, and it stays up until you tap it. REMOVE FROM WATCH LIST stops watching. It's one device at a time and it resets when the board restarts. A watched device always gets through AUTO SNOOZE and the spam filter. Fair warning: many phones and trackers change their Bluetooth address on purpose to defeat exactly this, so it's best on gear that doesn't.

The counters along the top show what's in range right now; tap one to open the closest device of that kind.

## False positives

They happen, and the confidence grade is there so you can tell a solid match from a maybe.

- **High** means the signature has been checked against several independent sources. **Low** means one source, or a signature that other things share. For example, several Flock WiFi prefixes belong to chip makers whose chips are in lots of other gadgets, so those matches are graded Low. If those bother you, set ALERT FILTER to MED+.
- **Why isn't my phone flagged?** Because phones aren't on the list. It matches specific signatures (a Flock prefix, an AirTag's Find My payload, a body cam's network name), not "anything Apple" or "anything nearby". An ordinary phone doesn't match any of them.
- **AirPods showing up as AIRTAG?** Known and accepted. A freshly powered AirTag sends the same kind of advert AirPods do, and catching the tag mattered more. [DETECTIONS.md](DETECTIONS.md) has the details.
- **Your own AirTag or doorbell** is a true positive every single time. IGNORE it.
- Another SquachWatch, an ESP32 Marauder or a nyanBOX is the same chip, and none of them is flagged as hacking hardware on that alone.

**Found a real false positive?** [Open an issue](https://github.com/skizzophrenic/SquachWatch-CYD/issues) with the type, the confidence, the first half of the address, the name if it had one, and what the thing actually was. Turn on PRIVACY MODE before you take a photo of the screen.

## SquachMesh and squads

SquachMesh lets SquachWatches see each other. When two meet, the other board's Squachy walks onto your screen for a visit. With a squad, you can also send messages.

It's opt-in and has two switches under **Settings > SQUACHMESH**:

- **DETECT** only listens for other boards. It sends nothing.
- **TRANSMIT** broadcasts your Squachy. Before it turns on, a warning screen explains exactly what goes out: an advert every 1.5 seconds with a Bluetooth address that never changes, plus your Squachy's name and outfit. Anyone with a scanner can log that address with a time and place, and because it never changes, those sightings join up. That's the same trick this device warns you about, so think about it.

**A squad** is a group of boards that share a five-word phrase. On the PHRASE screen, ROLL makes a new one; read it to a friend and they ENTER the same five words. Or skip the reading: **ADD TO SQUAD** (from INVITE on the SQUAD screen) adds a board that's right next to you, and both screens show four digits to compare so nobody in the middle can sneak in. With **SHOW PHRASE** off, the phrase is never shown, and the squad only grows in person.

A squad gets you:

- **Messages**: ready-made lines or up to 48 typed characters. SEND on the SQUAD screen opens the message screen, and you'll see READ when they open it.
- **HEADS-UP**: when your board catches something serious (Flock, Axon, a skimmer, Raven, a plate reader, a deauth flood, an evil twin or a hacking tool, never trackers), your squad gets an eight-second banner. On by default; HEADS-UP in the SquachMesh menu turns it off.
- **Update nudges**: a board that hears a squad member running a newer version tells you who. **UPDATE SQUAD** updates every squad board in range at once (each one gets a countdown with NOW and SKIP, and can optionally borrow your WiFi). A board only accepts this with **REMOTE UPDATE** on, in Settings > SECURITY. It's on by default.

**What's encrypted:** messages, read receipts, heads-ups, squad hellos and update nudges (including a shared WiFi password) are sealed with AES-128-CCM, using a key stretched from your phrase. **What isn't:** the advert itself (the fixed address, name and outfit), and the fact that you sent something and when. Anyone with your five words can read your squad's messages, so share them like a house key.

## Touch is off, the screen is white, the colours are wrong

**Taps land in the wrong place.** Go to **Settings > SYSTEM > CALIBRATE TOUCH** and tap and hold each of the five targets for about a second. That's the same calibration you get at first boot (since v1.17.0), where **SKIP** keeps the old one if it was fine. One calibration covers all four rotations.

**Calibrated it into oblivion?** Restart the board and hold your finger anywhere on the screen while it says "Hold anywhere now to reset touch calibration...". No aiming needed.

**Taps sometimes don't count?** A tap in the menus has to be short. A touch held for more than about half a second without moving is ignored, so a slow thumb is not a tap.

**Rotation:** the icon in the top-right corner turns the screen. **ROTATION LOCK** under APPEARANCE hides it. The Settings icon is top-left.

**Solid white screen after flashing a 2.8"?** It's not broken. Flash again with the 80 MHz box unticked. Still white? Your board has the other display driver, so pick the other 2.8" option (ILI9341 instead of ST7789, or the other way round).

**Colours wrong (blue where red should be, or everything looks like a negative)?** **Settings > SYSTEM > CHECK COLORS** puts red, green and blue on screen with two buttons, INVERT and ORDER; tap until they match. The same two settings live under APPEARANCE as **INVERT COLORS** and **COLOR ORDER**. The check also runs once on first boot.

## GPS and LoRa (watches only)

**GPS** is on the **T-Watch S3 Plus**, and on the **Cardputer ADV** with a Grove GPS (see [SMALL-BOARDS.md](SMALL-BOARDS.md)). **WARDRIVE** in WATCH SETTINGS turns the GPS and the log on together: every WiFi network and Bluetooth device it hears, with position, logged only while it has a fresh fix. It holds up to 125,000 sightings across restarts. **DOWNLOAD WIGLE FILE** on the web flasher (desktop Chrome or Edge) saves it as a file you can upload to [wigle.net](https://wigle.net/uploads). The log stays on the watch until you clear it, and a duress wipe takes it with everything else. The watch also sets its clock from GPS. Wardriving was marked not road-tested when it shipped in v1.25.0, so treat it as new.

**SEEN AT.** Anything the LOG kept with a GPS position shows it: long-press the device in the LOG and it says SEEN AT and where.

**LoRa** is on both the T-Watch S3 and the S3 Plus, and it only listens. It decodes the public channels of Meshtastic and MeshCore, using the EU 868 or US 915 band depending on your time zone. **LORA** in WATCH SETTINGS picks BOTH, MESHTASTIC, MESHCORE or OFF, and **LORA CHATS** shows what was said, one tab per network. More in [LORA.md](LORA.md).

No other board has GPS or LoRa, and I won't ask you to solder one on.

## What's with the Sasquach on the screen?

That's Squachy. The detection is the *why*, Squachy is the *fun*. He reacts to what the board catches, cracks jokes when nothing's happening, grows through stages as your lifetime catch count climbs, and earns outfits and pets along the way. Some of those you earn by catching things; some are hidden, and I'm not telling you where. Tap him. He likes that.

Other things hiding in Settings: the SQUACHY-DEX (a card per detection type with your record), BINGO, Squachy's diary, and DESK MODE, a clock for your desk.

**Don't want him?** **Settings > BEHAVIOR > BORING MODE** turns him off. Detection is exactly the same; you just get a plain detector, and a plain BLACK background if you want it. His rows in Settings stay visible, greyed out, so you can find your way back.

## Is this legal?

Listening to radio that devices are already broadcasting into public air is not hacking into anything. It doesn't break into networks or decrypt anyone's traffic. As covered [above](#does-it-transmit-anything), it does send a few things of its own, the same kind of thing any phone does when it looks for Bluetooth devices or joins WiFi.

That said, I'm a hairy cryptid mascot, not a lawyer. Know your local laws, use your brain, don't do anything stupid.

## Bugs, contributing and the license

**Something's broken?** [Open an issue](https://github.com/skizzophrenic/SquachWatch-CYD/issues). Tell me the board, the version (it's on the UPDATE FIRMWARE screen), what you expected, and what actually happened. A photo helps. If it crashed, the crash card on the next boot and **Settings > SYSTEM > DIAGNOSTICS** have the details, and a serial log is gold (the speed for your board is the `monitor_speed` in [platformio.ini](../platformio.ini)). Your board not on the flasher? Same place.

**Want to contribute?** PRs welcome, especially new detection signatures with real sources behind them. See [DETECTIONS.md](DETECTIONS.md) for the format. I take provenance seriously: I don't want to flag your neighbour's baby monitor as a Flock camera because someone guessed at a prefix. Several boards on the list above came from contributors, too.

**License:** SquachWatch is **GPL-3.0** (see [LICENSE](../LICENSE)). Fork it, break it, make it weirder. If you ship a changed version, share your source under the same license.

**Any relation to talkingsasquach.com?** Yes. That's me, the actual channel. This device and the whole SquachWare vaporwave look belong to that brand. More about the project is in the [README](../README.md).

---

Stay squachy out there.
