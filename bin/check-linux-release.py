#!/usr/bin/env python3
"""Reject Linux release binaries that exceed our supported runtime ABI."""
import argparse
import os
from pathlib import Path
import re
import subprocess

BASELINE = (2, 28)
TARGETS = {
    "amd64": ("Advanced Micro Devices X86-64", "/lib64/ld-linux-x86-64.so.2"),
    "arm64": ("AArch64", "/lib/ld-linux-aarch64.so.1"),
}
GLIBC_LIBRARIES = {
    "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2",
    "librt.so.1", "libresolv.so.2", "libutil.so.1",
}


def validate(header, program, dynamic, versions, architecture, libc):
    machine, loader = TARGETS[architecture]
    fields = dict(re.findall(r"^\s*(Class|Data|Machine):\s*(.*?)\s*$", header, re.M))
    if (fields.get("Class") != "ELF64" or fields.get("Machine") != machine
            or "little endian" not in fields.get("Data", "")):
        raise ValueError("expected a little-endian ELF64 {} executable".format(architecture))
    needed = set(re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic))
    interpreters = re.findall(r"Requesting program interpreter:\s*(.*?)\]", program)
    if re.search(r"\((?:RPATH|RUNPATH)\)", dynamic):
        raise ValueError("release must not contain RPATH/RUNPATH entries")
    if libc == "musl":
        if needed or interpreters or re.search(r"^\s*INTERP\s", program, re.M):
            raise ValueError("musl release must be fully static")
        return "fully static musl"
    if interpreters != [loader]:
        raise ValueError("glibc release must use {}".format(loader))
    unexpected = needed - GLIBC_LIBRARIES - {Path(loader).name}
    if unexpected or "libc.so.6" not in needed:
        raise ValueError("expected only system glibc libraries; found {}".format(sorted(needed)))
    required = set(re.findall(r"\bName:\s+GLIBC_(\S+)", versions))
    if not required:
        raise ValueError("missing glibc symbol requirements")
    for version in required:
        if (not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", version)
                or tuple(map(int, version.split("."))) > BASELINE):
            raise ValueError("GLIBC_{} exceeds the supported glibc 2.28 ABI".format(version))
    maximum = max(required, key=lambda value: tuple(map(int, value.split("."))))
    return "glibc <= 2.28 (highest required symbol: GLIBC_{})".format(maximum)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("architecture", choices=TARGETS)
    parser.add_argument("libc", choices=("glibc", "musl"))
    args = parser.parse_args()
    try:
        reports = [subprocess.check_output(
            ["readelf", "--wide", option, str(args.binary.resolve())],
            env=dict(os.environ, LC_ALL="C"), text=True)
            for option in ("--file-header", "--program-headers", "--dynamic", "--version-info")]
        result = validate(*reports, args.architecture, args.libc)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        parser.exit(1, "Linux release ABI check failed: {}\n".format(error))
    print("{}: {}".format(args.binary, result))


if __name__ == "__main__":
    main()
