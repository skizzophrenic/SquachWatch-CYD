# Flash one probe variant to the Tab5's P4 on COM15 (after the MAC check the
# caller does) and print the lines that matter. Read-only on the C6.
import subprocess, sys, time, os, serial
v = sys.argv[1]
# Probe variants are built by ../probe/build_variants.sh into a folder per
# variant (low4/high4/low1/high1); pass that folder's parent as argv[2].
d = os.path.join(sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(__file__), v)
r = subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32p4', '--port', 'COM15', '--baud', '921600',
                    '--before', 'default_reset', '--after', 'hard_reset', 'write_flash', '--flash_mode', 'keep',
                    '--flash_freq', '80m', '--flash_size', '16MB',
                    '0x2000', os.path.join(d, 'bootloader.bin'), '0x8000', os.path.join(d, 'partition-table.bin'),
                    '0x10000', os.path.join(d, 'probe.bin')], capture_output=True, text=True)
ok = 'Hash of data verified' in r.stdout
print(v, 'flashed' if ok else 'FLASH FAILED');
if not ok: print(r.stdout[-600:], r.stderr[-600:]); sys.exit(1)
s = serial.Serial(); s.port = 'COM15'; s.baudrate = 115200; s.timeout = 0.2; s.dtr = False; s.rts = False
for i in range(40):
    try: s.open(); break
    except Exception: time.sleep(0.25)
end = time.time() + 75; buf = b''
while time.time() < end:
    x = s.read(4096)
    if x: buf += x
    if b'[DONE]' in buf: break
s.close()
t = buf.decode('utf8', 'replace')
open(os.path.join(d, 'log.txt'), 'w', encoding='utf8').write(t)
for l in t.splitlines():
    if 'probe:' in l and ('PASS' in l or 'DONE' in l or 'DIAG' in l or 'PHASE' in l or '===' in l or '....' in l):
        print('  ', l.split('probe: ', 1)[-1])
    elif 'sdio_wrapper' in l and 'failed' not in l:
        print('  ', l.split(': ', 1)[-1])
print('  card_init failures:', t.count('sdmmc_card_init failed'))
