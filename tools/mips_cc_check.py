"""Print 'yes' if the given C compiler can target MIPS (big-endian, o32), else 'no'.
Used by patches/Makefile to fall back to an empty patches.elf when no MIPS toolchain exists."""
import os
import subprocess
import sys
import tempfile

cc = sys.argv[1] if len(sys.argv) > 1 else "clang"
with tempfile.TemporaryDirectory() as td:
    src = os.path.join(td, "t.c")
    obj = os.path.join(td, "t.o")
    open(src, "w").write("int f(int a) { return a + 1; }\n")
    try:
        if "gcc" in os.path.basename(cc):
            flags = ["-march=vr4300", "-mabi=32", "-O2", "-G0", "-mno-abicalls", "-fno-pic"]
        else:
            flags = ["-target", "mips", "-mips2", "-mabi=32", "-O2", "-G0", "-mno-abicalls"]
        r = subprocess.run([cc, *flags, "-c", src, "-o", obj], capture_output=True)
        print("yes" if r.returncode == 0 and os.path.exists(obj) else "no")
    except OSError:
        print("no")
