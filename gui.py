#!/usr/bin/env python3
"""Browser GUI for SquachWatch-Sim.

Runs inside WSL (where the emulator binary lives) and serves a page the
Windows browser can open -- which sidesteps the fact that Windows 10 has
no WSLg, so an SDL window would need an X server or a native toolchain.

    python3 gui.py            # then open http://localhost:842

Frames come over the wire as raw RGB888 straight from the emulator's
--raw mode and go into a canvas via ImageData, so there's no PNG encode
on this side and no decode on the other. Stdlib only.
"""

import http.server
import os
import socketserver
import subprocess
import tempfile
import urllib.parse

PORT = int(os.environ.get("SQUACHSIM_PORT", "842"))
HERE = os.path.dirname(os.path.abspath(__file__))
BINARY = os.path.join(HERE, "squachsim")

SCREENS = ["clear", "log", "alert", "settings", "diary", "hunt",
           "rawscan", "watchalert", "colorcheck", "boot"]

# Mirrors Settings::Background in include/settings.h -- order matters,
# the index is what --bg takes.
BACKGROUNDS = ["MATRIX RAIN", "STARFIELD", "FLYING TOASTERS", "AQUARIUM",
               "TERMINAL LOG", "FIREFLIES", "FIRE", "SNOWFALL",
               "RF SPECTRUM", "WIREFRAME TUNNEL"]

# Theme::kPalettes in src/theme.cpp.
THEMES = ["VAPRW4VE", "CYB3RGR33N", "AMB3RTERM", "BUBBL3GUM", "GH0ST", "BL00D"]


def render(params):
    """Run the emulator once and return (width, height, count, raw bytes)."""
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


PAGE = """<!doctype html>
<meta charset="utf-8">
<title>SquachWatch-Sim</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; background:#0a000f; color:#e8e8ff;
         font-family:'Consolas','Share Tech Mono',monospace; }
  header { padding:14px 18px; border-bottom:1px solid #b400ff44;
           display:flex; align-items:baseline; gap:14px; }
  h1 { margin:0; font-size:1.1rem; letter-spacing:.14em; color:#00fff5; font-weight:600; }
  .sub { font-size:.78rem; color:#8a7aa8; }
  main { display:flex; gap:22px; padding:18px; flex-wrap:wrap; }
  .stage { background:#000; border:1px solid #b400ff55; border-radius:8px;
           padding:14px; display:flex; flex-direction:column; align-items:center; gap:10px; }
  canvas { image-rendering:pixelated; border:1px solid #ffffff18; display:block; }
  .panel { display:flex; flex-direction:column; gap:12px; min-width:230px; }
  label { display:flex; flex-direction:column; gap:4px;
          font-size:.72rem; letter-spacing:.08em; color:#8fd; text-transform:uppercase; }
  select, input[type=number] { background:#150022; color:#fff;
          border:1px solid #b400ff66; border-radius:5px; padding:6px 8px; font:inherit; }
  .row { display:flex; gap:10px; align-items:center; flex-wrap:wrap; }
  button { background:linear-gradient(90deg,#b400ff,#ff71ce); color:#fff; border:0;
           border-radius:6px; padding:8px 14px; font:inherit; cursor:pointer; letter-spacing:.06em; }
  button.ghost { background:transparent; border:1px solid #00fff566; color:#00fff5; }
  .check { flex-direction:row; align-items:center; gap:7px; text-transform:none; font-size:.8rem; }
  #status { font-size:.74rem; color:#8a7aa8; min-height:1.1em; }
  #status.err { color:#ff5f8f; }
  .zoom { font-size:.72rem; color:#8a7aa8; }
</style>

<header>
  <h1>SQUACHWATCH-SIM</h1>
  <span class="sub">real firmware UI code, rendered natively &mdash; no device</span>
</header>

<main>
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
const $ = id => document.getElementById(id);

SCREENS.forEach(s => $('screen-sel').add(new Option(s, s)));
BACKGROUNDS.forEach((b, i) => $('bg').add(new Option(b, i)));
THEMES.forEach((t, i) => $('theme').add(new Option(t, i)));

const canvas = $('screen'), ctx = canvas.getContext('2d');
let frames = [], idx = 0, timer = null, playing = true;

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
  // The emulator advances 33ms of firmware time per frame, so playing
  // at 33ms keeps animations at the speed the device runs them.
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
    seq: $('seq').value,
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

    // Raw RGB888 -> ImageData (RGBA), done once per render rather than
    // per displayed frame so playback stays cheap.
    frames = [];
    const px = w * h;
    for (let f = 0; f < count; f++) {
      const img = ctx.createImageData(w, h);
      const src = f * px * 3;
      for (let i = 0; i < px; i++) {
        img.data[i*4]   = buf[src + i*3];
        img.data[i*4+1] = buf[src + i*3+1];
        img.data[i*4+2] = buf[src + i*3+2];
        img.data[i*4+3] = 255;
      }
      frames.push(img);
    }
    idx = 0; show(0); play();
    $('status').textContent =
      `${w}x${h}, ${count} frames, ${Math.round(performance.now() - t0)}ms`;
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
render();
</script>
"""


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # the default logs every request to stderr; too noisy here

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/":
            import json
            body = (PAGE
                    .replace("__SCREENS__", json.dumps(SCREENS))
                    .replace("__BACKGROUNDS__", json.dumps(BACKGROUNDS))
                    .replace("__THEMES__", json.dumps(THEMES))).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if parsed.path == "/render":
            try:
                w, h, count, raw = render(urllib.parse.parse_qs(parsed.query))
            except Exception as e:
                msg = str(e).encode()
                self.send_response(500)
                self.send_header("Content-Type", "text/plain; charset=utf-8")
                self.send_header("Content-Length", str(len(msg)))
                self.end_headers()
                self.wfile.write(msg)
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(raw)))
            self.send_header("X-Width", str(w))
            self.send_header("X-Height", str(h))
            self.send_header("X-Frames", str(count))
            self.end_headers()
            self.wfile.write(raw)
            return

        self.send_error(404)


if __name__ == "__main__":
    if not os.path.exists(BINARY):
        raise SystemExit(f"{BINARY} not found -- run `make` first")
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("0.0.0.0", PORT), Handler) as httpd:
        print(f"SquachWatch-Sim GUI on http://localhost:{PORT}  (ctrl-c to stop)")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()
