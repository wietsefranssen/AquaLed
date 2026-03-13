Import("env")
import subprocess

def get_firmware_version():
    try:
        tag = subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match"],
            stderr=subprocess.DEVNULL
        ).strip().decode("utf-8")
        return tag
    except Exception:
        pass
    try:
        branch = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            stderr=subprocess.DEVNULL
        ).strip().decode("utf-8")
        commit = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL
        ).strip().decode("utf-8")
        return branch + "@" + commit
    except Exception:
        return "dev"

version = get_firmware_version()
print("Firmware version: " + version)
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(version))])
