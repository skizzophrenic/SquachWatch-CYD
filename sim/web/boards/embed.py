#!/usr/bin/env python3
"""Put the board drawings into the browser emulator's page.

The emulator page draws each board around its screen from the SVGs in this
folder. They go into the page as <template id="board-NAME"> blocks between
the BOARDS:BEGIN and BOARDS:END markers, so the page stays one file (the
release workflow publishes index.html, squachsim.js and squachsim.wasm and
nothing else). Every id in a drawing gets the board's name in front, so the
bench's copy and the shelf's little copy never answer for each other.

Run it after changing a drawing:

    python3 sim/web/boards/embed.py

It writes web-flasher/emulator/index.html (what the site serves) and copies it
to sim/web/index.html, which is the same page for `make wasm` and a local look.

Each drawing is in millimetres (its viewBox), and the page relies on these
named parts: #screen (the panel's active area: the canvas goes exactly there),
#usb with data-dir (left/right/down/up: where the cable leaves), and where the
board has them #btn-boot (data-side="back" when it is on the back), #crown,
#trackball, and keys as <g class="key" data-key="a">.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
PAGE = os.path.join(REPO, 'web-flasher', 'emulator', 'index.html')
COPY = os.path.join(REPO, 'sim', 'web', 'index.html')
BOARDS = ['cyd28', 'cyd35', 'tdeck', 'watch', 'cardputer']   # the page's order


def drawing(name):
    t = open(os.path.join(HERE, name + '.svg'), encoding='utf-8').read()
    t = re.sub(r'<\?xml[^>]*\?>', '', t)
    t = re.sub(r'<!DOCTYPE[^>]*>', '', t)
    t = re.sub(r'<!--.*?-->', '', t, flags=re.S)
    t = re.sub(r'\bid="([^"]+)"', lambda m: f'id="{name}-{m.group(1)}"', t)
    t = re.sub(r'url\(#([^)]+)\)', lambda m: f'url(#{name}-{m.group(1)})', t)
    t = re.sub(r'(xlink:href|href)="#([^"]+)"', lambda m: f'{m.group(1)}="#{name}-{m.group(2)}"', t)
    t = '\n'.join(line.strip() for line in t.splitlines() if line.strip())
    assert f'id="{name}-screen"' in t, name + ': no #screen'
    assert f'id="{name}-usb"' in t, name + ': no #usb'
    return f'<template id="board-{name}">\n{t}\n</template>'


def main():
    page = open(PAGE, encoding='utf-8').read()
    begin, end = '<!-- BOARDS:BEGIN -->', '<!-- BOARDS:END -->'
    a, b = page.index(begin) + len(begin), page.index(end)
    page = page[:a] + '\n' + '\n'.join(drawing(n) for n in BOARDS) + '\n' + page[b:]
    for out in (PAGE, COPY):
        with open(out, 'w', encoding='utf-8', newline='\n') as f:
            f.write(page)
    print(f'{len(BOARDS)} boards -> {os.path.relpath(PAGE, REPO)} ({len(page) // 1024} KB) and {os.path.relpath(COPY, REPO)}')


if __name__ == '__main__':
    main()
