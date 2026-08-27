#!/usr/bin/env python3
"""SquachWatch-CYD v1.0.3 patch — Python version.

Run from PowerShell or cmd in the project root:
    python patch.py
"""
import re
import sys
from pathlib import Path

def main() -> int:
    root = Path(__file__).resolve().parent
    main_cpp = root / "src" / "main.cpp"
    det_cpp  = root / "src" / "detection.cpp"

    if not main_cpp.exists() or not det_cpp.exists():
        print(f"ERROR: expected {main_cpp} and {det_cpp} to exist.")
        print("       Run this script from the project root (where platformio.ini is).")
        return 1

    # ---- 1. Patch src/main.cpp ----
    main_src = main_cpp.read_text()
    main_changed = False

    # Add SPIClass declaration (only if missing)
    if "SPIClass            touchSPI(HSPI);" not in main_src:
        new_main = re.sub(
            r"(// XPT2046_Touchscreen v1\.4 constructor[^\n]*\n)(XPT2046_Touchscreen\s+touch\()",
            r"SPIClass            touchSPI(HSPI);\n\1\2",
            main_src,
        )
        if new_main != main_src:
            main_src = new_main
            main_changed = True
            print("  + Added SPIClass touchSPI declaration")
        else:
            print("  - Could not find insertion point for SPIClass — pattern mismatch")
            print("    (this means main.cpp has been edited and the patch no longer matches)")
    else:
        print("  - SPIClass touchSPI declaration already present")

    # Change touch.begin(HSPI) -> touch.begin(touchSPI)
    if "touch.begin(HSPI);" in main_src:
        main_src = main_src.replace("touch.begin(HSPI);", "touch.begin(touchSPI);")
        main_changed = True
        print("  + Changed touch.begin(HSPI) to touch.begin(touchSPI)")
    elif "touch.begin(touchSPI);" in main_src:
        print("  - touch.begin(touchSPI) already present")
    else:
        print("  - Could not find touch.begin(...) call")

    if main_changed:
        main_cpp.write_text(main_src)

    # ---- 2. Patch src/detection.cpp ----
    det_src = det_cpp.read_text()
    det_changed = False

    # Replace uuid16 extraction
    if "((uint16_t)p[0] << 8) | p[1]" in det_src:
        print("  - uuid16 getNative() extraction already present")
    else:
        new_det = re.sub(
            r"uint16_t uuid16 = \(uint16_t\)\s*u\.to16\s*\(\s*\);",
            "const uint8_t* p = u.getNative();\n"
            "                        uint16_t uuid16 = ((uint16_t)p[0] << 8) | p[1];",
            det_src,
        )
        if new_det != det_src:
            det_src = new_det
            det_changed = True
            print("  + Changed uuid16 extraction to use getNative()")
        else:
            print("  - Could not find uuid16 = (uint16_t)u.to16() pattern in detection.cpp")

    if det_changed:
        det_cpp.write_text(det_src)

    print("")
    print("Done. Next steps:")
    print("  Remove-Item -Recurse -Force .pio")
    print("  pio run -t upload")
    return 0

if __name__ == "__main__":
    sys.exit(main())
