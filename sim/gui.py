#!/usr/bin/env python3
"""Browser GUI for SquachWatch-Sim.

Runs inside WSL (where the emulator binaries live) and serves a page the
Windows browser can open -- which sidesteps the fact that Windows 10 has
no WSLg, so an SDL window would need an X server or a native toolchain.

    python3 gui.py            # then open http://localhost:842

Two modes:

  LIVE     drives ./squachsim-live, which runs the firmware's real
           setup()/loop(). Clicking the canvas is a finger on the glass:
           coordinates go back through the firmware's own touch mapping,
           and every transition, debounce and long-press is the
           firmware's own. Use this for "does the UI actually work".

  GALLERY  drives ./squachsim, the one-shot renderer: pick a screen, a
           background and a theme and get an animation loop of it. No
           navigation, no state -- it calls one screen's Tick() directly,
           so it can show screens that are awkward to reach by hand.
           Use this for "does this screen look right".

Frames come over the wire as raw RGB888 and go into a canvas via
ImageData, so there's no PNG encode on this side and no decode on the
other. Stdlib only.
"""

import collections
import http.server
import json
import os
import re
import shutil
import socketserver
import subprocess
import tempfile
import threading
import urllib.parse

PORT = int(os.environ.get("SQUACHSIM_PORT", "842"))
HERE = os.path.dirname(os.path.abspath(__file__))
BINARY = os.path.join(HERE, "squachsim")
LIVE_BINARY = os.path.join(HERE, "squachsim-live")

# Same default the Makefile uses -- the two repos sit side by side.
SQUACHWATCH = os.environ.get("SQUACHWATCH", os.path.dirname(HERE))

# The live device's NVS. A real directory rather than a temp one on
# purpose: settings, pet counts and "has seen the walkthrough" survive a
# restart of the GUI exactly like they survive a power cycle on
# hardware. Factory reset deletes it.
NVS_DIR = os.path.join(HERE, ".nvs")

SCREENS = ["clear", "log", "alert", "settings", "diary", "hunt",
           "rawscan", "watchalert", "colorcheck", "boot"]

# Mirrors Settings::Background in include/settings.h -- order matters,
# the index is what --bg takes.
BACKGROUNDS = ["MATRIX RAIN", "STARFIELD", "FLYING TOASTERS", "AQUARIUM",
               "TERMINAL LOG", "FIREFLIES", "FIRE", "SNOWFALL",
               "RF SPECTRUM", "WIREFRAME TUNNEL", "SYNTHWAVE"]

# Theme::kPalettes in src/theme.cpp.
THEMES = ["VAPRW4VE", "CYB3RGR33N", "AMB3RTERM", "BUBBL3GUM", "GH0ST", "BL00D"]

# Fallback only. DetectionType gains members often enough that a
# hardcoded copy here would go stale, so it's read from the firmware's
# own enum below and this is just what to show if that ever fails.
FALLBACK_TYPES = [
    (1, "FLOCK"), (2, "AXON"), (3, "META"), (4, "SKIMMER"), (5, "RAVEN"),
    (6, "AIRTAG"), (7, "DRONE"), (8, "ALPR"), (9, "CAMERA"),
    (10, "SAMSUNG_TAG"), (11, "GOOGLE_TAG"), (12, "TILE"), (13, "RING"),
    (14, "DEAUTH"),
]


def detection_types():
    """[[index, NAME], ...] parsed out of include/state.h's DetectionType.

    UNKNOWN and COUNT are dropped -- neither is a thing you can trigger.
    """
    try:
        path = os.path.join(SQUACHWATCH, "include", "state.h")
        with open(path, encoding="utf-8", errors="replace") as f:
            src = f.read()
        block = re.search(r"enum class DetectionType\s*:[^{]*\{(.*?)\}", src, re.S).group(1)
        found = [[int(v), n] for n, v in re.findall(r"(\w+)\s*=\s*(\d+)", block)
                 if n not in ("UNKNOWN", "COUNT")]
        if found:
            return sorted(found)
    except Exception:
        pass
    return [list(t) for t in FALLBACK_TYPES]


TYPES = detection_types()


# ---------------------------------------------------------------- gallery

def render(params):
    """Run the one-shot emulator and return (width, height, count, raw)."""
    screen = params.get("screen", ["clear"])[0]
    if screen not in SCREENS:
        raise ValueError(f"unknown screen {screen!r}")

    portrait = params.get("portrait", ["0"])[0] == "1"
    seq = max(1, min(120, int(params.get("seq", ["45"])[0])))
    frames = max(1, min(600, int(params.get("frames", ["90"])[0])))

    cmd = [BINARY, screen, "--sequence", str(seq), "--frames", str(frames)]
    if portrait:
        cmd.append("--portrait")
    if params.get("onboard", ["0"])[0] == "1":
        cmd.append("--onboard")
    for flag, key in (("--bg", "bg"), ("--theme", "theme")):
        val = params.get(key, [""])[0]
        if val != "":
            cmd += [flag, str(int(val))]

    # A temp file rather than stdout: the emulator already writes raw
    # frames to a path, and this keeps its stdout free for the geometry
    # line we parse below.
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        raw_path = tmp.name
    try:
        cmd += ["--raw", raw_path]
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=60, cwd=HERE)
        if out.returncode != 0:
            raise RuntimeError(out.stderr.strip() or "emulator failed")
        # "raw 320x240 rgb888 frames=45 -> /tmp/..."
        parts = out.stdout.split()
        dims = next(p for p in parts if "x" in p and p[0].isdigit())
        w, h = (int(v) for v in dims.split("x"))
        count = int(next(p for p in parts if p.startswith("frames=")).split("=")[1])
        with open(raw_path, "rb") as f:
            return w, h, count, f.read()
    finally:
        try:
            os.unlink(raw_path)
        except OSError:
            pass


# ------------------------------------------------------------------- live

class LiveDevice:
    """The persistent squachsim-live process, as one virtual device.

    Every request that touches the process holds `lock`, so the
    command/frame protocol on the pipe can't interleave even though the
    HTTP server is threaded. The firmware's Serial output arrives on
    stderr and is drained continuously by a reader thread -- left in the
    pipe buffer it would eventually fill and block the emulator
    mid-loop().
    """

    def __init__(self):
        self.lock = threading.RLock()
        self.proc = None
        self.log = collections.deque(maxlen=500)
        self.log_seq = 0          # total lines ever produced, for /live/log
        self.generation = 0       # bumped per start, so a stale drain thread exits
        self.mesh = "{}"          # the virtual peer's status, off the last frame

    def start(self, wipe=False):
        with self.lock:
            self.stop()
            if wipe:
                shutil.rmtree(NVS_DIR, ignore_errors=True)
            os.makedirs(NVS_DIR, exist_ok=True)
            self.log.clear()
            self.log_seq = 0
            self.generation += 1
            gen = self.generation
            env = dict(os.environ, SQUACHSIM_NVS=NVS_DIR)
            self.proc = subprocess.Popen(
                [LIVE_BINARY], cwd=HERE, env=env,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            threading.Thread(target=self._drain,
                             args=(self.proc, gen), daemon=True).start()

    def stop(self):
        with self.lock:
            if not self.proc:
                return
            try:
                self.proc.stdin.write(b"Q\n")
                self.proc.stdin.flush()
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()
            self.proc = None

    def _drain(self, proc, gen):
        for line in iter(proc.stderr.readline, b""):
            if gen != self.generation:
                return
            self.log.append(line.decode("utf-8", "replace").rstrip("\r\n"))
            self.log_seq += 1

    def tail(self, since):
        """Lines produced after `since`, plus the new sequence number."""
        first = self.log_seq - len(self.log)
        start = max(0, since - first)
        return list(self.log)[start:], self.log_seq

    def _readexact(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.proc.stdout.read(n - len(buf))
            if not chunk:
                raise RuntimeError("emulator exited: " + " | ".join(list(self.log)[-3:]))
            buf += chunk
        return bytes(buf)

    def catalog(self):
        """The virtual peer's pickers, from the firmware's own tables."""
        with self.lock:
            if not self.proc or self.proc.poll() is not None:
                self.start()
            self.proc.stdin.write(b"K\n")
            self.proc.stdin.flush()
            line = self.proc.stdout.readline().decode("utf-8", "replace")
            if not line.startswith("CAT "):
                raise RuntimeError(f"bad catalog reply {line!r}")
            return line[4:].strip()

    def exchange(self, cmds):
        """Send commands, return (w, h, state, raw) for the LAST frame.

        One frame comes back per `S`, and intermediate ones are read and
        dropped -- that's what lets a caller interleave press/step/
        release/step in a single round trip and still get exactly one
        image back.
        """
        steps = sum(1 for c in cmds if c[:1] == "S")
        if steps == 0:
            raise ValueError("no step command")
        with self.lock:
            if not self.proc or self.proc.poll() is not None:
                self.start()
            self.proc.stdin.write("".join(c + "\n" for c in cmds).encode())
            self.proc.stdin.flush()
            result = None
            for _ in range(steps):
                header = self.proc.stdout.readline()
                if not header:
                    raise RuntimeError("emulator exited: " + " | ".join(list(self.log)[-3:]))
                # At most six fields: the last is the mesh status, which is
                # JSON and may carry spaces.
                parts = header.decode("ascii", "replace").rstrip("\r\n").split(None, 5)
                if len(parts) < 5 or parts[0] != "FRM":
                    raise RuntimeError(f"bad frame header {header!r}")
                w, h, nbytes, state = int(parts[1]), int(parts[2]), int(parts[3]), parts[4]
                self.mesh = parts[5] if len(parts) > 5 else "{}"
                result = (w, h, state, self._readexact(nbytes))
            return result


DEVICE = LiveDevice()


def rebuild():
    """Recompile both binaries against the current firmware checkout.

    The emulator compiles SquachWatch-CYD's sources directly, so a
    rebuild here picks up whatever that tree currently says -- which is
    the point: edit firmware, press the button, see it. Without this the
    loop was edit, kill the GUI, make, restart the GUI, re-navigate.

    The live process has to be stopped first. It IS the binary make is
    about to overwrite, and Linux refuses to write a running executable
    ("Text file busy"), so building over the top of it fails rather than
    silently producing a stale device.
    """
    DEVICE.stop()
    try:
        out = subprocess.run(["make"], cwd=HERE, capture_output=True,
                             text=True, timeout=900)
    except Exception as e:
        return False, f"make failed to run: {e}"
    log = (out.stdout or "") + (out.stderr or "")
    if out.returncode != 0:
        # Leave the device down on a failed build -- restarting it would
        # silently run the previous binary and look like the edit did
        # nothing.
        return False, log[-4000:]
    DEVICE.start()
    return True, log[-2000:]


def live_step(params):
    """Translate a /live/step query into emulator commands."""
    cmds = []
    # Injected before the touch/step below so a detection raised in the
    # same request is visible to the loop() iteration that follows it.
    trig = params.get("trig", [""])[0]
    if trig != "":
        rssi = params.get("rssi", [""])[0]
        cmds.append("T %d %d" % (int(trig), int(rssi) if rssi else 0))
    # The virtual SquachMesh peer: one command per request, as its panel
    # sends them. See sim/meshsim.h for the language.
    mesh = params.get("mesh", [""])[0]
    if mesh:
        cmds.append("P " + " ".join(mesh.split())[:100])
    ev = params.get("ev", [""])[0]
    if ev in ("down", "move"):
        x = int(params.get("x", ["0"])[0])
        y = int(params.get("y", ["0"])[0])
        cmds += ["%s %d %d" % ("D" if ev == "down" else "M", x, y), "S 1"]
    elif ev == "up":
        cmds += ["U", "S 1"]
    # The event gets its own step so a press and its release are never
    # collapsed into one loop() iteration: the firmware distinguishes
    # touch-just-down from touch-just-up, and a touch that appeared and
    # vanished within a single iteration would register as neither. The
    # trailing step below is what advances animation between clicks.
    n = max(1, min(240, int(params.get("n", ["1"])[0])))
    cmds.append("S %d" % n)
    return DEVICE.exchange(cmds)


PAGE = """<!doctype html>
<meta charset="utf-8">
<title>SquachWatch-Sim</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; background:#0a000f; color:#e8e8ff;
         font-family:'Consolas','Share Tech Mono',monospace; }
  header { padding:14px 18px; border-bottom:1px solid #b400ff44;
           display:flex; align-items:baseline; gap:14px; flex-wrap:wrap; }
  h1 { margin:0; font-size:1.1rem; letter-spacing:.14em; color:#00fff5; font-weight:600; }
  .sub { font-size:.78rem; color:#8a7aa8; }
  .tabs { margin-left:auto; display:flex; gap:6px; }
  .tabs button { padding:6px 16px; font-size:.76rem; letter-spacing:.1em; }
  .tabs button:not(.on) { background:transparent; border:1px solid #b400ff66; color:#a98fd0; }
  main { display:flex; gap:22px; padding:18px; flex-wrap:wrap; }
  .stage { background:#000; border:1px solid #b400ff55; border-radius:8px;
           padding:14px; display:flex; flex-direction:column; align-items:center; gap:10px; }
  canvas { image-rendering:pixelated; border:1px solid #ffffff18; display:block; }
  canvas.live { cursor:crosshair; }
  .panel { display:flex; flex-direction:column; gap:12px; min-width:230px; }
  label { display:flex; flex-direction:column; gap:4px;
          font-size:.72rem; letter-spacing:.08em; color:#8fd; text-transform:uppercase; }
  select, input[type=number], input[type=text] { background:#150022; color:#fff;
          border:1px solid #b400ff66; border-radius:5px; padding:6px 8px; font:inherit; }
  .row { display:flex; gap:10px; align-items:center; flex-wrap:wrap; }
  button { background:linear-gradient(90deg,#b400ff,#ff71ce); color:#fff; border:0;
           border-radius:6px; padding:8px 14px; font:inherit; cursor:pointer; letter-spacing:.06em; }
  button.ghost { background:transparent; border:1px solid #00fff566; color:#00fff5; }
  .check { flex-direction:row; align-items:center; gap:7px; text-transform:none; font-size:.8rem; }
  #status, #liveStatus { font-size:.74rem; color:#8a7aa8; min-height:1.1em; }
  #status.err, #liveStatus.err { color:#ff5f8f; }
  .zoom { font-size:.72rem; color:#8a7aa8; }
  .state { font-size:.9rem; letter-spacing:.12em; color:#8a7aa8; }
  .state b { color:#00fff5; }
  #serial { height:240px; overflow:auto; background:#08000d;
            border:1px solid #b400ff33; border-radius:6px; padding:8px;
            font-size:.7rem; line-height:1.45; color:#7fd9a0; white-space:pre-wrap; }
  .hint { font-size:.7rem; color:#6d5c88; line-height:1.6; }
  .hide { display:none; }
  button:disabled { opacity:.4; cursor:default; }
</style>

<header>
  <h1>SQUACHWATCH-SIM</h1>
  <span class="sub">real firmware UI code, rendered natively &mdash; no device</span>
  <span class="tabs">
    <button id="tabLive" class="on">Live</button>
    <button id="tabGallery">Gallery</button>
  </span>
</header>

<!-- ============================= LIVE ============================= -->
<main id="liveView">
  <div class="stage">
    <canvas id="liveScreen" class="live" width="320" height="240"></canvas>
    <div class="state">state <b id="stateOut">--</b></div>
    <div class="row">
      <button id="liveRunBtn">Pause</button>
      <button id="rebuildBtn">Rebuild</button>
      <button id="rebootBtn" class="ghost">Reboot</button>
      <button id="wipeBtn" class="ghost">Factory reset</button>
      <span class="zoom">zoom
        <select id="liveZoom">
          <option value="1">1x</option>
          <option value="2" selected>2x</option>
          <option value="3">3x</option>
        </select>
      </span>
    </div>
    <div id="liveStatus">&nbsp;</div>
  </div>

  <div class="panel" style="flex:1; min-width:320px;">
    <label>Simulate a detection</label>
    <div class="row">
      <select id="detType" style="flex:1"></select>
      <input type="number" id="detRssi" placeholder="RSSI" style="width:84px"
             min="-110" max="-20" step="1">
      <button id="trigBtn">Trigger</button>
    </div>
    <label>A SquachWatch nearby</label>
    <div class="row">
      <button id="meshHere">Bring it nearby</button>
      <button id="meshSetup" class="ghost" title="Emulator only: accepts the warning, turns DETECT, TRANSMIT and MESSAGES on and sets a phrase">Set up SquachMesh</button>
    </div>
    <div class="row">
      <select id="meshOutfit" title="Outfit"></select>
      <select id="meshShade" title="Shades"></select>
      <select id="meshNick" title="Nickname"></select>
      <input type="text" id="meshName" maxlength="12" placeholder="custom name" style="width:120px">
    </div>
    <div class="row">
      <select id="meshLine" style="flex:1"></select>
      <button id="meshSay" class="ghost">Make it say</button>
    </div>
    <div class="row">
      <input type="text" id="meshText" maxlength="48" placeholder="or type one: A-Z 0-9 .,?!'-" style="flex:1">
      <button id="meshType" class="ghost">Make it type</button>
    </div>
    <div class="row">
      <label class="check"><input type="checkbox" id="meshShares" checked> same phrase as you</label>
      <label class="check"><input type="checkbox" id="meshReply" checked> answers back</label>
    </div>
    <div id="meshStatus" class="hint"></div>
    <p class="hint">
      A second SquachWatch that only exists in the emulator (sim/meshsim.cpp).
      It feeds real SquachMesh adverts and message frames into the firmware's
      own Mesh code, so it turns up exactly when a board would. Its look is
      copied once, on arrival, as on a board: to see a new outfit or name,
      send it away, let it leave, and bring it back. Set up is emulator-only
      &mdash; it skips the consent warning and the phrase screen.
    </p>
    <label>Serial monitor</label>
    <div id="serial"></div>
    <p class="hint">
      Click the screen to tap it. Press, drag and release all go through
      the firmware's own touch mapping, so long-presses (holding CLR for
      the outfit unlock) and drag-scrolling behave the way they do on the
      device. Settings persist across Reboot; Factory reset wipes the
      emulated NVS, which brings back the colour check and the first-boot
      walkthrough.
    </p>
    <p class="hint">
      Trigger posts a synthetic sighting into the engine, which raises
      the real ALERT screen, logs a row and bumps the counters. Leave
      RSSI blank for a plausible default. It does <b>not</b> exercise the
      matcher that decides what counts as a detection &mdash; and because
      the emulator's engine is a stub, the TYPE FILTER setting has no
      effect on it.
    </p>
  </div>
</main>

<!-- =========================== GALLERY ============================ -->
<main id="galleryView" class="hide">
  <div class="stage">
    <canvas id="screen" width="320" height="240"></canvas>
    <div class="row">
      <button id="playBtn">Pause</button>
      <button id="shotBtn" class="ghost">Save PNG</button>
      <span class="zoom">zoom
        <select id="zoom">
          <option value="1">1x</option>
          <option value="2" selected>2x</option>
          <option value="3">3x</option>
        </select>
      </span>
    </div>
    <div id="status">&nbsp;</div>
  </div>

  <div class="panel">
    <label>Screen
      <select id="screen-sel"></select>
    </label>
    <label>Background
      <select id="bg"></select>
    </label>
    <label>Theme
      <select id="theme"></select>
    </label>
    <label class="check">
      <input type="checkbox" id="portrait"> Portrait (240x320)
    </label>
    <label class="check">
      <input type="checkbox" id="onboard"> First-boot walkthrough
    </label>
    <label>Warm-up frames
      <input type="number" id="frames" value="90" min="1" max="600" step="10">
    </label>
    <label>Animation frames
      <input type="number" id="seq" value="45" min="1" max="120" step="5">
    </label>
    <button id="renderBtn">Render</button>
  </div>
</main>

<script>
const SCREENS = __SCREENS__, BACKGROUNDS = __BACKGROUNDS__, THEMES = __THEMES__;
const TYPES = __TYPES__;
const $ = id => document.getElementById(id);

// Raw RGB888 -> ImageData (RGBA). Shared by both modes.
function toImage(ctx, w, h, buf, off) {
  const img = ctx.createImageData(w, h);
  const px = w * h;
  for (let i = 0; i < px; i++) {
    img.data[i*4]   = buf[off + i*3];
    img.data[i*4+1] = buf[off + i*3+1];
    img.data[i*4+2] = buf[off + i*3+2];
    img.data[i*4+3] = 255;
  }
  return img;
}

/* ------------------------------ live ------------------------------ */
const lc = $('liveScreen'), lctx = lc.getContext('2d');
let liveRunning = true, liveBusy = false, liveSince = 0;
// Touch events queue up between polls instead of each firing its own
// request: one in-flight request at a time keeps presses, drags and
// releases in the order the mouse produced them, which matters because
// the firmware classifies a gesture from that order.
let pending = [], mouseDown = false;

function liveZoom() {
  const z = +$('liveZoom').value;
  lc.style.width = (lc.width * z) + 'px';
  lc.style.height = (lc.height * z) + 'px';
}

function canvasXY(e) {
  const r = lc.getBoundingClientRect();
  return {
    x: Math.max(0, Math.min(lc.width - 1,  Math.floor((e.clientX - r.left) * lc.width  / r.width))),
    y: Math.max(0, Math.min(lc.height - 1, Math.floor((e.clientY - r.top)  * lc.height / r.height)))
  };
}

lc.addEventListener('mousedown', e => {
  e.preventDefault();
  mouseDown = true;
  const p = canvasXY(e);
  pending.push({ev: 'down', x: p.x, y: p.y});
});
lc.addEventListener('mousemove', e => {
  if (!mouseDown) return;
  const p = canvasXY(e);
  // Consecutive moves collapse into the latest position. A drag emits
  // them far faster than the queue drains (one event per 33ms step), so
  // without this the emulated finger falls further and further behind
  // the mouse and a flick scrolls for seconds after you let go. Only
  // move-onto-move collapses -- a press or release in between is an
  // event the firmware's gesture classifier needs to see.
  const last = pending[pending.length - 1];
  if (last && last.ev === 'move') { last.x = p.x; last.y = p.y; return; }
  pending.push({ev: 'move', x: p.x, y: p.y});
});
// On window, not the canvas: releasing outside it still has to lift the
// emulated finger, or it stays pressed forever.
window.addEventListener('mouseup', () => {
  if (!mouseDown) return;
  mouseDown = false;
  pending.push({ev: 'up'});
});

async function liveTick() {
  if (liveBusy || (!liveRunning && !pending.length)) return;
  liveBusy = true;
  try {
    const q = new URLSearchParams({n: '1'});
    const ev = pending.shift();
    if (ev && ev.mesh !== undefined) {
      q.set('mesh', ev.mesh);
    } else if (ev && ev.trig !== undefined) {
      q.set('trig', ev.trig);
      if (ev.rssi) q.set('rssi', ev.rssi);
    } else if (ev) {
      q.set('ev', ev.ev);
      if (ev.x !== undefined) { q.set('x', ev.x); q.set('y', ev.y); }
    }
    const res = await fetch('/live/step?' + q);
    if (!res.ok) throw new Error((await res.text()) || res.statusText);
    const w = +res.headers.get('X-Width'), h = +res.headers.get('X-Height');
    const buf = new Uint8Array(await res.arrayBuffer());
    if (lc.width !== w || lc.height !== h) { lc.width = w; lc.height = h; liveZoom(); }
    lctx.putImageData(toImage(lctx, w, h, buf, 0), 0, 0);
    $('stateOut').textContent = res.headers.get('X-State') || '?';
    meshShow(res.headers.get('X-Mesh'));
    $('liveStatus').className = '';
    $('liveStatus').textContent = w + 'x' + h;
  } catch (e) {
    $('liveStatus').className = 'err';
    $('liveStatus').textContent = 'error: ' + e.message;
    liveRunning = false;
    $('liveRunBtn').textContent = 'Play';
  } finally {
    liveBusy = false;
  }
}

async function pollSerial() {
  try {
    const res = await fetch('/live/log?since=' + liveSince);
    if (!res.ok) return;
    const data = await res.json();
    liveSince = data.seq;
    if (data.lines.length) {
      const el = $('serial');
      const atBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 24;
      el.textContent += data.lines.join('\\n') + '\\n';
      if (atBottom) el.scrollTop = el.scrollHeight;
    }
  } catch (e) { /* a dead emulator is already surfaced by the step loop */ }
}

async function liveReset(wipe) {
  pending = []; mouseDown = false;
  $('serial').textContent = '';
  liveSince = 0;
  await fetch('/live/reset?wipe=' + (wipe ? 1 : 0), {method: 'POST'});
  liveRunning = true;
  $('liveRunBtn').textContent = 'Pause';
}

// 33ms is the firmware time the emulator advances per step, so stepping
// once per 33ms of wall clock runs animations at device speed.
setInterval(liveTick, 33);
setInterval(pollSerial, 400);

$('liveRunBtn').onclick = () => {
  liveRunning = !liveRunning;
  $('liveRunBtn').textContent = liveRunning ? 'Pause' : 'Play';
};
// Recompiles against the current firmware source and restarts the
// device. The build takes tens of seconds, so the step loop is parked
// for the duration -- otherwise every poll during the build fails
// against a stopped process and paints the canvas red.
$('rebuildBtn').onclick = async () => {
  const btn = $('rebuildBtn');
  const wasRunning = liveRunning;
  liveRunning = false;
  btn.disabled = true;
  btn.textContent = 'Building...';
  $('liveStatus').className = '';
  $('liveStatus').textContent = 'rebuilding from source...';
  try {
    const res = await fetch('/rebuild', {method: 'POST'});
    const data = await res.json();
    if (data.ok) {
      pending = []; mouseDown = false;
      $('serial').textContent = '';
      liveSince = 0;
      $('liveStatus').textContent = 'rebuilt';
      liveRunning = true;
      $('liveRunBtn').textContent = 'Pause';
    } else {
      // Compiler output goes in the serial pane: it is the widest
      // readable area on the page and it is already monospaced.
      $('serial').textContent = data.log || '(no output)';
      $('liveStatus').className = 'err';
      $('liveStatus').textContent = 'build failed -- device left stopped';
      liveRunning = false;
      $('liveRunBtn').textContent = 'Play';
    }
  } catch (e) {
    $('liveStatus').className = 'err';
    $('liveStatus').textContent = 'rebuild error: ' + e.message;
    liveRunning = wasRunning;
  } finally {
    btn.disabled = false;
    btn.textContent = 'Rebuild';
  }
};
$('rebootBtn').onclick = () => liveReset(false);
$('wipeBtn').onclick   = () => liveReset(true);
$('liveZoom').onchange = liveZoom;

TYPES.forEach(([i, name]) => $('detType').add(new Option(name, i)));
$('detType').value = 6;   // AIRTAG, the one worth reaching for first
// Queued like a touch rather than sent directly, so a trigger can't
// overtake a tap that was made before it.
$('trigBtn').onclick = () => {
  pending.push({trig: $('detType').value, rssi: $('detRssi').value});
};

// Which switch decides what happens, in the order a person would hit them.
// The firmware does the deciding; this only says why.
function meshHint(s) {
  if (!s.present) return ['Nobody nearby. Bring it nearby to start a visit.'];
  if (!s.detect) return ['It is in range, but your DETECT is off, so your Squachy cannot see it.',
                         'Settings > SQUACHMESH > DETECT, or Set up.'];
  if (!s.visiting) return ['In range. It turns up with its next advert.'];
  const out = ['Visiting your main screen.'];
  if (s.sending) {
    const why = !s.messages ? 'your MESSAGES is off'
              : !s.phrase   ? 'you have no phrase set'
              : !s.shares   ? 'it is using a different phrase' : '';
    out.push('It is sending "' + s.said + '"' + (why ? ' -- you will not see it: ' + why + '.' : '.'));
  } else if (!s.messages) {
    out.push('Your MESSAGES is off, so anything it sends is ignored.');
  }
  if (!s.transmit) out.push('Your TRANSMIT is off: it cannot see you or hear what you send.');
  else if (s.heard) out.push(s.shares ? 'It heard you say "' + s.heard + '"' + (s.replyIn >= 0 ? ' and is answering.' : '.')
                                      : 'It picked up your message but cannot read it: different phrase.');
  return out;
}
function meshLines(el, lines) {
  el.replaceChildren(...lines.map(t => Object.assign(document.createElement('div'), {textContent: t})));
}

/* --------------------------- SquachMesh ---------------------------- */
// The virtual peer in sim/meshsim.cpp. Commands ride the same queue as a
// touch, so each lands between two loop() iterations like everything else,
// and its status comes back on every frame in X-Mesh.
let mesh = {}, meshCat = false, meshCatBusy = false;
function meshCmd(c) { pending.push({mesh: c}); }
async function loadMeshCat() {
  meshCatBusy = true;
  try {
    const cat = await (await fetch('/live/meshcat')).json();
    for (const [id, list] of [['meshOutfit', cat.outfits], ['meshShade', cat.shades],
                              ['meshNick', cat.nicks], ['meshLine', cat.lines]]) {
      $(id).length = 0;
      list.forEach((n, i) => $(id).add(new Option(n, i)));
    }
    meshCat = true;
  } catch (e) { /* not up yet; tried again on a later frame */ }
  meshCatBusy = false;
}
function meshShow(h) {
  if (!h) return;
  try { mesh = JSON.parse(h); } catch (e) { return; }
  if (!meshCat && !meshCatBusy) loadMeshCat();
  $('meshHere').textContent = mesh.present ? 'Send it away' : 'Bring it nearby';
  $('meshSay').disabled = !mesh.present;
  $('meshType').disabled = !mesh.present;
  for (const [id, v] of [['meshOutfit', mesh.outfit], ['meshShade', mesh.shade],
                         ['meshNick', mesh.nick], ['meshName', mesh.name]])
    if (document.activeElement !== $(id)) $(id).value = v;
  $('meshShares').checked = !!mesh.shares;
  $('meshReply').checked = !!mesh.reply;
  meshLines($('meshStatus'), meshHint(mesh));
}
$('meshSetup').onclick = () => meshCmd('setup');
$('meshHere').onclick  = () => meshCmd(mesh.present ? 'off' : 'on');
$('meshSay').onclick   = () => meshCmd('say ' + $('meshLine').value);
$('meshType').onclick  = () => meshCmd('text ' + $('meshText').value);
$('meshOutfit').onchange = e => meshCmd('outfit ' + e.target.value);
$('meshShade').onchange  = e => meshCmd('shade ' + e.target.value);
$('meshNick').onchange   = e => meshCmd('nick ' + e.target.value);
$('meshName').onchange   = e => meshCmd('name ' + e.target.value);
$('meshShares').onchange = e => meshCmd('phrase ' + (e.target.checked ? 'same' : 'other'));
$('meshReply').onchange  = e => meshCmd('reply ' + (e.target.checked ? 'on' : 'off'));
loadMeshCat();
liveZoom();

/* ---------------------------- gallery ----------------------------- */
SCREENS.forEach(s => $('screen-sel').add(new Option(s, s)));
BACKGROUNDS.forEach((b, i) => $('bg').add(new Option(b, i)));
THEMES.forEach((t, i) => $('theme').add(new Option(t, i)));

const canvas = $('screen'), ctx = canvas.getContext('2d');
let frames = [], idx = 0, timer = null, playing = true, rendered = false;

function applyZoom() {
  const z = +$('zoom').value;
  canvas.style.width = (canvas.width * z) + 'px';
  canvas.style.height = (canvas.height * z) + 'px';
}

function show(i) {
  if (!frames.length) return;
  ctx.putImageData(frames[i % frames.length], 0, 0);
}

function play() {
  clearInterval(timer);
  timer = setInterval(() => { if (playing) show(idx++); }, 33);
}

async function render() {
  const p = new URLSearchParams({
    screen: $('screen-sel').value,
    bg: $('bg').value,
    theme: $('theme').value,
    portrait: $('portrait').checked ? 1 : 0,
    onboard: $('onboard').checked ? 1 : 0,
    frames: $('frames').value,
    seq: $('seq').value
  });
  $('status').className = '';
  $('status').textContent = 'rendering...';
  const t0 = performance.now();
  try {
    const res = await fetch('/render?' + p);
    if (!res.ok) throw new Error((await res.text()) || res.statusText);
    const w = +res.headers.get('X-Width'), h = +res.headers.get('X-Height');
    const count = +res.headers.get('X-Frames');
    const buf = new Uint8Array(await res.arrayBuffer());

    canvas.width = w; canvas.height = h;
    applyZoom();

    // Decoded once per render rather than per displayed frame, so
    // playback stays cheap.
    frames = [];
    for (let f = 0; f < count; f++) frames.push(toImage(ctx, w, h, buf, f * w * h * 3));
    idx = 0; show(0); play();
    $('status').textContent =
      w + 'x' + h + ', ' + count + ' frames, ' + Math.round(performance.now() - t0) + 'ms';
  } catch (e) {
    $('status').className = 'err';
    $('status').textContent = 'error: ' + e.message;
  }
}

$('renderBtn').onclick = render;
$('zoom').onchange = applyZoom;
$('playBtn').onclick = () => {
  playing = !playing;
  $('playBtn').textContent = playing ? 'Pause' : 'Play';
};
$('shotBtn').onclick = () => {
  const a = document.createElement('a');
  a.download = $('screen-sel').value + '.png';
  a.href = canvas.toDataURL('image/png');
  a.click();
};
// Re-render on any control change -- it's fast enough that an explicit
// Render click is only needed if you want to re-roll the randomness.
['screen-sel','bg','theme','portrait','onboard','frames','seq']
  .forEach(id => $(id).onchange = render);
applyZoom();

/* ------------------------------ tabs ------------------------------ */
function showTab(live) {
  $('liveView').classList.toggle('hide', !live);
  $('galleryView').classList.toggle('hide', live);
  $('tabLive').classList.toggle('on', live);
  $('tabGallery').classList.toggle('on', !live);
  // The gallery costs a subprocess run per render, so it only renders
  // the first time it is actually looked at.
  if (!live && !rendered) { rendered = true; render(); }
}
$('tabLive').onclick = () => showTab(true);
$('tabGallery').onclick = () => showTab(false);
</script>
"""


class Handler(http.server.BaseHTTPRequestHandler):
    # Keep-alive matters here: the live view issues ~30 requests a
    # second, and a fresh TCP connection per frame would be most of the
    # cost. It's what makes the threaded server below necessary --
    # persistent connections would deadlock a single-threaded one.
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass  # the default logs every request to stderr; far too noisy here

    def _send(self, code, ctype, body, extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _fail(self, e):
        self._send(500, "text/plain; charset=utf-8", str(e).encode())

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/rebuild":
            ok, log = rebuild()
            body = json.dumps({"ok": ok, "log": log}).encode()
            return self._send(200, "application/json", body)

        if parsed.path == "/live/reset":
            q = urllib.parse.parse_qs(parsed.query)
            try:
                DEVICE.start(wipe=q.get("wipe", ["0"])[0] == "1")
            except Exception as e:
                return self._fail(e)
            return self._send(200, "text/plain; charset=utf-8", b"ok")
        self.send_error(404)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        if parsed.path == "/":
            body = (PAGE
                    .replace("__SCREENS__", json.dumps(SCREENS))
                    .replace("__BACKGROUNDS__", json.dumps(BACKGROUNDS))
                    .replace("__THEMES__", json.dumps(THEMES))
                    .replace("__TYPES__", json.dumps(TYPES))).encode()
            return self._send(200, "text/html; charset=utf-8", body)

        if parsed.path == "/render":
            try:
                w, h, count, raw = render(params)
            except Exception as e:
                return self._fail(e)
            return self._send(200, "application/octet-stream", raw,
                              [("X-Width", str(w)), ("X-Height", str(h)),
                               ("X-Frames", str(count))])

        if parsed.path == "/live/step":
            try:
                w, h, state, raw = live_step(params)
            except Exception as e:
                return self._fail(e)
            return self._send(200, "application/octet-stream", raw,
                              [("X-Width", str(w)), ("X-Height", str(h)),
                               ("X-State", state), ("X-Mesh", DEVICE.mesh)])

        if parsed.path == "/live/meshcat":
            try:
                body = DEVICE.catalog().encode()
            except Exception as e:
                return self._fail(e)
            return self._send(200, "application/json", body)

        if parsed.path == "/live/log":
            since = int(params.get("since", ["0"])[0])
            lines, seq = DEVICE.tail(since)
            body = json.dumps({"lines": lines, "seq": seq}).encode()
            return self._send(200, "application/json", body)

        self.send_error(404)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    if not os.path.exists(BINARY):
        raise SystemExit(f"{BINARY} not found -- run `make` first")
    if not os.path.exists(LIVE_BINARY):
        raise SystemExit(f"{LIVE_BINARY} not found -- run `make live` first")
    DEVICE.start()
    with Server(("0.0.0.0", PORT), Handler) as httpd:
        print(f"SquachWatch-Sim GUI on http://localhost:{PORT}  (ctrl-c to stop)")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()
        finally:
            DEVICE.stop()
