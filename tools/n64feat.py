"""Feature extraction for N64 (MIPS) functions straight from the ROM.

Shared by libultra_id.py and gen_syms.py.
  rom, segs, A = load_all()
  A.feat[addr] -> dict of features; A.sizes[addr]; A.callers[addr]
Run directly to dump a per-function summary of the boot segment.
"""
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
ROM = os.path.join(ROOT, "California Speed (USA).z64")
SIDECAR = ROM + ".segments.json"
BN_FUNCS = os.path.join(ROOT, "scratch", "bn_funcs.json")


def _num(v):
    return int(v, 0) if isinstance(v, str) else int(v)


def load_segments(rom):
    """[(name, vram_start, vram_end, rom_start)] for code segments."""
    entry = struct.unpack(">I", rom[8:12])[0]
    hi, vals = {}, {}
    for i in range(24):
        w = struct.unpack(">I", rom[0x1000 + 4 * i: 0x1004 + 4 * i])[0]
        op, rt, rs, imm = w >> 26, (w >> 16) & 31, (w >> 21) & 31, w & 0xFFFF
        if op == 0x0F:
            hi[rt] = imm << 16
        elif op == 0x09 and rs == rt and rt in hi:
            vals[rt] = (hi[rt] + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
    segs = [("boot", entry, vals[8], 0x1000)]
    for s in json.load(open(SIDECAR, encoding="utf-8")):
        segs.append((s["name"], _num(s["vram"]), _num(s["vram"]) + _num(s["size"]), _num(s["rom"])))
    return segs


# opcode groups
LOADSTORE = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2E,
             0x31, 0x35, 0x37, 0x39, 0x3D, 0x3F, 0x2F, 0x30, 0x38}
ALU_IMM = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E}


class Analysis:
    def __init__(self, rom, segs, func_addrs):
        self.rom, self.segs = rom, segs
        self.funcs = sorted(set(a for a in func_addrs if self.seg_of(a)))
        self.sizes = {}
        for i, a in enumerate(self.funcs):
            seg = self.seg_of(a)
            nxt = self.funcs[i + 1] if i + 1 < len(self.funcs) else None
            end = nxt if (nxt is not None and nxt < seg[2]) else seg[2]
            self.sizes[a] = end - a
        self.feat = {a: self.extract(a) for a in self.funcs}
        self.callers = {}
        for a, f in self.feat.items():
            for c in set(f["callees"]):
                self.callers.setdefault(c, set()).add(a)

    def seg_of(self, addr):
        for s in self.segs:
            if s[1] <= addr < s[2]:
                return s
        return None

    def v2r(self, addr):
        s = self.seg_of(addr)
        return None if s is None else s[3] + (addr - s[1])

    def word(self, addr):
        r = self.v2r(addr)
        return struct.unpack(">I", self.rom[r:r + 4])[0] if r is not None else None

    def words(self, addr, n):
        r = self.v2r(addr)
        return struct.unpack(">%dI" % n, self.rom[r:r + 4 * n])

    def string_at(self, addr, maxlen=80):
        r = self.v2r(addr)
        if r is None:
            return None
        m = re.match(rb"[\x20-\x7e\t\r\n]{3,%d}\x00" % maxlen, self.rom[r:r + maxlen + 1])
        return m.group()[:-1].decode() if m else None

    def extract(self, a):
        n = self.sizes[a] // 4
        w = self.words(a, n)
        f = dict(addr=a, size=self.sizes[a], ninstr=n, cop0=set(), cache=set(), special=set(),
                 hw=set(), consts=set(), ptrs=set(), imm16=set(), callees=[], strings=set(), fpu=set(),
                 jr_reg=False, words=w)
        hi = {}            # reg -> value from lui (or lui+addiu base)
        for x in w:
            op = x >> 26
            rs = (x >> 21) & 31
            rt = (x >> 16) & 31
            rd = (x >> 11) & 31
            fn = x & 63
            imm = x & 0xFFFF
            simm = imm - 0x10000 if imm & 0x8000 else imm
            if op == 0x10:                                   # COP0
                if rs in (0, 4):
                    f["cop0"].add(("mfc0" if rs == 0 else "mtc0", rd))
                elif rs >= 16:
                    f["special"].add({1: "tlbr", 2: "tlbwi", 6: "tlbwr", 8: "tlbp", 24: "eret"}.get(fn, "cop0_%d" % fn))
            elif op == 0x2F:
                f["cache"].add(rt)
            elif op == 0x30:
                f["special"].add("ll")
            elif op == 0x38:
                f["special"].add("sc")
            elif op == 0x11:                                 # COP1
                if rs == 2 and rd == 31:
                    f["special"].add("cfc1_31")
                elif rs == 6 and rd == 31:
                    f["special"].add("ctc1_31")
                elif rs >= 16:
                    fmt = {16: "s", 17: "d", 20: "w", 21: "l"}.get(rs, "?")
                    nm = {0: "add", 1: "sub", 2: "mul", 3: "div", 4: "sqrt", 5: "abs", 9: "trunc.l",
                          12: "round.w", 13: "trunc.w", 14: "ceil.w", 15: "floor.w", 32: "cvt.s",
                          33: "cvt.d", 36: "cvt.w", 37: "cvt.l"}.get(fn)
                    if nm:
                        f["fpu"].add(nm + "." + fmt)
            elif op == 0:
                sp = {0x1c: "dmult", 0x1d: "dmultu", 0x1e: "ddiv", 0x1f: "ddivu", 0x3c: "dsll32",
                      0x3e: "dsrl32", 0x3f: "dsra32", 0x14: "dsllv", 0x16: "dsrlv", 0x17: "dsrav",
                      0x38: "dsll", 0x3a: "dsrl", 0x3b: "dsra", 0x2c: "dadd", 0x2d: "daddu"}.get(fn)
                if sp:
                    f["special"].add(sp)
                if fn == 8 and rs != 31:
                    f["jr_reg"] = True
            elif op == 3:
                f["callees"].append(((x & 0x3FFFFFF) << 2) | 0x80000000)

            # constant tracking
            if op == 0x0F:
                hi[rt] = imm << 16
                continue
            if rs in hi and (op in LOADSTORE or op in (0x09, 0x0D)):
                v = (hi[rs] | imm) if op == 0x0D else (hi[rs] + simm) & 0xFFFFFFFF
                if 0xA4000000 <= v < 0xA4900000:
                    f["hw"].add(v)
                else:
                    f["consts"].add(v)
                    if op in (0x09, 0x0D):
                        f["ptrs"].add(v)
                    if 0x80000000 <= v < 0x80400000:
                        s = self.string_at(v)
                        if s:
                            f["strings"].add(s)
                if op in (0x09, 0x0D):
                    hi[rt] = v                                # keep as base for later offsets
                    continue
            elif op in ALU_IMM and imm and rs not in hi:
                f["imm16"].add(imm)
            # kill tracked regs that get overwritten
            if op in ALU_IMM or (op in LOADSTORE and op not in (0x28, 0x29, 0x2B, 0x2F, 0x2E, 0x3D, 0x3F, 0x39, 0x38, 0x2A, 0x26)):
                hi.pop(rt, None)
            elif op == 0:
                hi.pop(rd, None)
        return f


def load_all():
    rom = open(ROM, "rb").read()
    segs = load_segments(rom)
    j = json.load(open(BN_FUNCS))
    j = j["functions"] if isinstance(j, dict) else j
    funcs = [int(x["address"], 16) for x in j]
    return rom, segs, Analysis(rom, segs, funcs)


HW = {0xA4040000: "SP_MEM_ADDR", 0xA4040004: "SP_DRAM_ADDR", 0xA4040008: "SP_RD_LEN", 0xA404000C: "SP_WR_LEN",
      0xA4040010: "SP_STATUS", 0xA4040014: "SP_DMA_FULL", 0xA4040018: "SP_DMA_BUSY", 0xA404001C: "SP_SEMAPHORE",
      0xA4080000: "SP_PC",
      0xA4100000: "DPC_START", 0xA4100004: "DPC_END", 0xA4100008: "DPC_CURRENT", 0xA410000C: "DPC_STATUS",
      0xA4100010: "DPC_CLOCK", 0xA4100014: "DPC_BUFBUSY", 0xA4100018: "DPC_PIPEBUSY", 0xA410001C: "DPC_TMEM",
      0xA4300000: "MI_MODE", 0xA4300004: "MI_VERSION", 0xA4300008: "MI_INTR", 0xA430000C: "MI_INTR_MASK",
      0xA4400000: "VI_STATUS", 0xA4400004: "VI_ORIGIN", 0xA4400008: "VI_WIDTH", 0xA440000C: "VI_INTR",
      0xA4400010: "VI_CURRENT", 0xA4400014: "VI_BURST", 0xA4400018: "VI_V_SYNC", 0xA440001C: "VI_H_SYNC",
      0xA4400020: "VI_LEAP", 0xA4400024: "VI_H_START", 0xA4400028: "VI_V_START", 0xA440002C: "VI_V_BURST",
      0xA4400030: "VI_X_SCALE", 0xA4400034: "VI_Y_SCALE",
      0xA4500000: "AI_DRAM_ADDR", 0xA4500004: "AI_LEN", 0xA4500008: "AI_CONTROL", 0xA450000C: "AI_STATUS",
      0xA4500010: "AI_DACRATE", 0xA4500014: "AI_BITRATE",
      0xA4600000: "PI_DRAM_ADDR", 0xA4600004: "PI_CART_ADDR", 0xA4600008: "PI_RD_LEN", 0xA460000C: "PI_WR_LEN",
      0xA4600010: "PI_STATUS", 0xA4600014: "PI_DOM1_LAT", 0xA4600018: "PI_DOM1_PWD", 0xA460001C: "PI_DOM1_PGS",
      0xA4600020: "PI_DOM1_RLS", 0xA4600024: "PI_DOM2_LAT", 0xA4600028: "PI_DOM2_PWD", 0xA460002C: "PI_DOM2_PGS",
      0xA4600030: "PI_DOM2_RLS",
      0xA4700000: "RI_MODE", 0xA4700004: "RI_CONFIG", 0xA470000C: "RI_SELECT", 0xA4700010: "RI_REFRESH",
      0xA4800000: "SI_DRAM_ADDR", 0xA4800004: "SI_PIF_RD64B", 0xA4800010: "SI_PIF_WR64B", 0xA4800018: "SI_STATUS"}


def hwname(a):
    return HW.get(a, "HW_%08X" % a)


def summary(A, a):
    f = A.feat[a]
    bits = []
    if f["cop0"]:
        bits.append("cop0=" + ",".join("%s%d" % c for c in sorted(f["cop0"])))
    if f["cache"]:
        bits.append("cache=" + ",".join("%x" % c for c in sorted(f["cache"])))
    if f["special"]:
        bits.append("sp=" + ",".join(sorted(f["special"])))
    if f["fpu"]:
        bits.append("fpu=" + ",".join(sorted(f["fpu"])))
    if f["hw"]:
        bits.append("hw=" + ",".join(hwname(h) for h in sorted(f["hw"])))
    if f["strings"]:
        bits.append("str=" + "|".join(repr(s)[:40] for s in sorted(f["strings"])[:3]))
    return "%08x %4d ins callers=%-3d callees=%-2d %s" % (
        a, f["ninstr"], len(A.callers.get(a, ())), len(set(f["callees"])), " ".join(bits))


if __name__ == "__main__":
    rom, segs, A = load_all()
    lo = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0
    hi_ = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x8001C160
    print("segments:", [(n, hex(s), hex(e), hex(r)) for n, s, e, r in segs])
    for a in A.funcs:
        if lo <= a < hi_:
            print(summary(A, a))
