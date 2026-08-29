# Stamps the current git tag/commit into the firmware as FIRMWARE_VERSION,
# shown on the Diary screen (see src/ui_diary.cpp). Falls back to "unknown"
# if git isn't available or this isn't a git checkout at all -- never
# breaks the build over a missing version string.
Import("env")
import subprocess


def get_version():
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
        return v if v else "unknown"
    except Exception:
        return "unknown"


env.Append(BUILD_FLAGS=['-DFIRMWARE_VERSION=\\"%s\\"' % get_version()])
