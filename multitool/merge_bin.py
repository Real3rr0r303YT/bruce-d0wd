# Erzeugt nach dem Build ein Komplett-Image (Bootloader+Partitionen+App) fuer Offset 0x0
Import("env")
import os

def merge_bin(source, target, env):
    build = env.subst("$BUILD_DIR")
    out = os.path.join(env.subst("$PROJECT_DIR"), "esp-multitool-full.bin")
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    args = [env.subst("$PYTHONEXE"), esptool, "--chip", "esp32", "merge_bin", "-o", out,
            "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB"]
    for off, img in env.get("FLASH_EXTRA_IMAGES", []):
        args += [env.subst(off), env.subst(img)]
    app_off = env.subst("$ESP32_APP_OFFSET") or "0x10000"
    args += [app_off, os.path.join(build, env.subst("${PROGNAME}.bin"))]
    env.Execute(" ".join('"%s"' % a for a in args))
    print("[merge_bin] -> " + out)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
