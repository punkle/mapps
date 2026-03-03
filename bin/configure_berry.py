"""
PlatformIO pre-script: Configure Berry library.json for the mapps build.

Patches the library.json in Berry's PIO-managed libdeps directory so that
only the needed sources are compiled with the correct flags, and runs the
coc tool to generate the constant string/bytes tables.

Works both when mapps is the main project (standalone) and when consumed
as a PlatformIO library (via library.json extraScript).
"""
Import("env")
import inspect
import json
import os
import subprocess
import sys

BERRY_LIBRARY_JSON = {
    "name": "berry",
    "version": "0.0.0",
    "description": "Berry scripting language for embedded systems",
    "repository": {
        "type": "git",
        "url": "https://github.com/berry-lang/berry.git"
    },
    "build": {
        "flags": [
            "-Wno-unused-parameter",
            "-Wno-sign-compare",
            "-Wno-missing-field-initializers"
        ],
        "includeDir": "src",
        "srcDir": ".",
        "srcFilter": [
            "-<*>",
            "+<src/*.c>"
        ],
        "libCompatMode": "off"
    }
}


def find_berry_libdeps_dir(env):
    """Find Berry's directory under .pio/libdeps/<env>/."""
    libdeps_dir = os.path.join(env["PROJECT_LIBDEPS_DIR"], env["PIOENV"])
    if not os.path.isdir(libdeps_dir):
        return None
    for name in os.listdir(libdeps_dir):
        if name.lower().startswith("berry"):
            candidate = os.path.join(libdeps_dir, name)
            if os.path.isdir(candidate):
                return candidate
    return None


def run_coc(berry_dir, conf_path):
    """Run Berry's coc tool to generate constant tables."""
    generate_dir = os.path.join(berry_dir, "generate")
    marker = os.path.join(generate_dir, "be_const_strtab.h")
    if os.path.isfile(marker):
        return  # already generated

    os.makedirs(generate_dir, exist_ok=True)

    coc_script = os.path.join(berry_dir, "tools", "coc", "coc")
    src_dir = os.path.join(berry_dir, "src")
    default_dir = os.path.join(berry_dir, "default")

    cmd = [
        sys.executable, coc_script,
        "-o", generate_dir,
        src_dir, default_dir,
        "-c", conf_path
    ]
    print(f"  [Berry] Running coc: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  [Berry] coc stderr: {result.stderr}")
        raise RuntimeError(f"Berry coc failed with exit code {result.returncode}")
    print(f"  [Berry] Generated constant tables in {generate_dir}")


berry_dir = find_berry_libdeps_dir(env)
if berry_dir:
    lib_json_path = os.path.join(berry_dir, "library.json")
    with open(lib_json_path, "w") as f:
        json.dump(BERRY_LIBRARY_JSON, f, indent=4)
        f.write("\n")
    print(f"  [Berry] Configured library.json in {berry_dir}")

    # Ensure berry_conf.h is discoverable by the Berry library.
    # Derive mapps src/ from this script's location so it works both when
    # mapps is the main project and when consumed as a PIO library.
    # Note: __file__ is not defined when SCons exec()s this script, so we
    # read the filename from the current frame's code object instead.
    _this_file = os.path.abspath(inspect.currentframe().f_code.co_filename)
    mapps_root = os.path.dirname(os.path.dirname(_this_file))
    mapps_src = os.path.join(mapps_root, "src")
    env.Append(CPPPATH=[mapps_src])
    print(f"  [Berry] Added {mapps_src} to include path for berry_conf.h")

    # Run coc to generate constant tables
    conf_path = os.path.join(mapps_src, "berry_conf.h")
    run_coc(berry_dir, conf_path)
else:
    print("  [Berry] Warning: Berry libdeps directory not found (will be available after first install)")
