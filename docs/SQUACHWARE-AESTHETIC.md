# SquachWare → CYD aesthetic mapping

For future maintainers porting SquachWatch to other boards, or just
to understand where the colors come from. The SquachWare CSS lives
at [talkingsasquach.com](https://talkingsasquach.com).

| SquachWare CSS variable | Hex (web) | RGB565 (CYD) | Where it's used |
|---|---|---|---|
| `--bg`             | `#0a000f` | `0x0801` | Every screen background |
| `--taskbar`        | `#0d001a` | `0x0803` | Reserved (status bar) |
| `--win-border`     | `#b400ff` | `0xAC1F` | Window borders, dividers, button borders |
| `--purple`         | `#b400ff` | `0xAC1F` | Same as `--win-border` |
| `--cyan`           | `#00fff5` | `0x07FF` | Body text, button labels |
| `--pink`           | `#ff2d78` | `0xF96F` | High-threat detection, ALERT border |
| `--vapor-pink`     | `#ff71ce` | `0xFB99` | Soft pink, ghost avatar, ALERT wordmark |
| `--vapor-purple`   | `#b967ff` | `0xBB5F` | Soft purple, matrix rain, scanline, ghost |
| `--vapor-blue`     | `#01cdfe` | `0x067F` | (Reserved) |
| `--vapor-yellow`   | `#fffb96` | `0xFFD2` | Skimmer-class threat color |
| `--green`          | `#00ff88` | `0x07E0` | "ALL CLEAR" text, "CLEAR" state |
| `TFT_RED`          | `#ff0000` | `0xF800` | Critical alerts (reserved) |
| `TFT_WHITE`        | `#ffffff` | `0xFFFF` | Headers, MAC addresses |
| `TFT_BLACK`        | `#000000` | `0x0000` | Reserved (deepest alert regions) |

## Fonts

| Role on web | CSS | CYD font |
|---|---|---|
| Big header (SquachWare) | `Orbitron 900` | `Font6` (32 px, blocky) — fall back to `Font4` (26 px) if it doesn't fit |
| Body / labels | `VT323` | `Font2` (16 px) |
| Mono / data | `Share Tech Mono` | `Font1` (6 px monospace) |

The 14-column matrix-rain glyph set is copied verbatim from
SquachWare's HTML:

```
アイウエオカキクケコサシスセソ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@#$%SASQUACH
```

Note the `SASQUACH` token at the end — the brand spelling, not the
cryptid.

## Titlebar gradient

`linear-gradient(90deg, var(--purple), var(--cyan), var(--pink))` is
simulated in `Theme::titlebarColor(x, w)` with a piecewise lerp
(purple → cyan for the first half, cyan → pink for the second half).

## Titlebar strip

```
[ purple ─── cyan ─── pink ]  (gradient, 14 px tall)
[ 1-px purple bottom border ]
[ centered white title text ]
```

## Touch button

```
+----------------+
|     [ LABEL ]  |   <- 1-px purple border, BG fill, cyan text
+----------------+
```

Pressed state: filled purple, white text.
