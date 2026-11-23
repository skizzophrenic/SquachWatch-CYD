#!/usr/bin/env python3
"""Save a WiFi network on the Tab5, for HUMAN SENSING on its own radio.

    python tools/tab5_wifi.py

The Tab5 has no WIFI NETWORKS screen yet. This finds the Tab5 by its USB
serial number (never by port), asks for the network's name and password (the
password is not shown as it is typed and is never printed), and sends them as
one WIFISAVE console line. SquachWatch keeps them in its saved-network list;
HUMAN SENSING joins it, pings the router and senses from that, the next time
the screen opens (leave it and come back, or restart).
"""
import getpass, sys, time
import serial
import serial.tools.list_ports

TAB5 = "30:ED:A0:E3:24:83"


def main():
    port = next((p.device for p in serial.tools.list_ports.comports() if (p.serial_number or "").upper() == TAB5), None)
    if not port:
        print("The Tab5 is not plugged in (or not on a direct USB port).")
        return 1
    print(f"Tab5 found on {port}.")
    name = input("WiFi network name: ").strip()
    if not name or len(name.encode()) > 32 or "\t" in name:
        print("A network name is 1 to 32 characters.")
        return 1
    pw = getpass.getpass("Password (not shown, press Enter if none): ")
    if len(pw.encode()) > 64 or "\t" in pw:
        print("That password is too long for WiFi (64 at most).")
        return 1
    s = serial.Serial()
    s.port = port; s.baudrate = 115200; s.timeout = 0.2; s.dtr = False; s.rts = False
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()
    s.write(("WIFISAVE " + name + "\t" + pw + "\n").encode()); s.flush()
    pw = None
    end = time.time() + 5
    buf = ""
    while time.time() < end:
        buf += s.read(4096).decode("utf-8", "replace")
        for line in buf.split("\n"):
            if line.startswith("[wifi]") and "saved" in line:
                print(line.strip())
                s.close()
                if "not saved" in line:
                    return 1
                print("Now leave HUMAN SENSING and open it again: it joins the network.")
                return 0
    s.close()
    print("No answer from the Tab5. Is it unlocked, and running SquachWatch?")
    return 1


if __name__ == "__main__":
    sys.exit(main())
