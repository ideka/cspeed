"""Validate cspeed.us.syms.toml the way N64Recomp will, so all problems show at once.

Checks per function:
  * every branch target lies inside the function
  * every jal target is a listed function
  * no instructions N64Recomp cannot recompile (unless the function is one N64Recomp
    ignores / reimplements by name)
Prints a summary; exit code 1 if any problem.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, HERE)
from n64feat import load_all        # noqa: E402
import funcbounds as fb             # noqa: E402

SYMS = os.path.join(ROOT, "cspeed.us.syms.toml")
LISTS = os.path.join(ROOT, "N64Recomp", "src", "symbol_lists.cpp")


def n64recomp_lists():
    s = open(LISTS, encoding="utf-8").read()
    out = {}
    for n in ("reimplemented_funcs", "ignored_funcs", "renamed_funcs"):
        m = re.search(n + r"\s*[={]\s*\{?(.*?)\};", s, re.S)
        out[n] = set(re.findall(r'"([^"]+)"', m.group(1))) if m else set()
    return out


def parse_syms():
    funcs = []
    for m in re.finditer(r'\{\s*name\s*=\s*"([^"]+)",\s*vram\s*=\s*0x([0-9A-Fa-f]+),\s*size\s*=\s*0x([0-9A-Fa-f]+)\s*\}', open(SYMS, encoding="utf-8").read()):
        funcs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
    return funcs


def unsupported(x):
    op = x >> 26
    rs = (x >> 21) & 31
    fn = x & 63
    if op == 0x10 or op == 0x2F or op in (0x30, 0x38):           # cop0, cache, ll, sc
        return True
    if op == 0 and fn in (0x0C, 0x0F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x36):   # syscall, sync, traps
        return True
    if op == 1 and ((x >> 16) & 31) in (8, 9, 0xA, 0xB, 0xC, 0xE):          # trap immediates
        return True
    if op == 0x11 and rs in (2, 6) and ((x >> 11) & 31) != 31:
        return True
    return False


def main():
    rom, segs, A = load_all()
    lists = n64recomp_lists()
    skip = lists["ignored_funcs"] | lists["reimplemented_funcs"]
    funcs = parse_syms()
    starts = {v: n for n, v, s in funcs}
    problems = 0
    for name, start, size in funcs:
        end = start + size
        w = A.words(start, size // 4)
        issues = []
        for i, x in enumerate(w):
            pc = start + 4 * i
            kind, tgt = fb.decode_flow(x, pc)
            if kind in ("b", "B") and not (start <= tgt < end):
                issues.append("branch %08X -> %08X outside" % (pc, tgt))
            elif kind == "j" and not (start <= tgt < end) and tgt not in starts:
                issues.append("j %08X -> %08X outside and not a function" % (pc, tgt))
            elif kind == "jal" and tgt not in starts and A.seg_of(tgt):
                issues.append("jal %08X -> %08X not a function" % (pc, tgt))
            if name not in skip and unsupported(x):
                issues.append("unsupported insn %08X: %08X" % (pc, x))
        if issues:
            problems += 1
            print("%-28s %08X+%04X: %s" % (name, start, size, "; ".join(issues[:3]) + (" ..." if len(issues) > 3 else "")))
    print("%d functions checked, %d with problems" % (len(funcs), problems))
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
