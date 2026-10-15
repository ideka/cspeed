"""Compute real function extents from the ROM with a MIPS control-flow walk.

Binary Ninja's function starts are taken as seeds (scratch/bn_funcs.json), but
BN splits functions at some branch targets and misreads data as code, so we:

  * walk the CFG from each start; the function extends to the last reachable
    instruction (+ delay slot), skipping trailing nop padding;
  * if the walk reaches the start of the next seed, the two are one function
    and get merged (repeat as needed);
  * report gaps (bytes inside .text not covered by any function) and the
    end of .text per segment (last function reachable from a jal / pointer).

Usage:  python tools/funcbounds.py            # stats
        from funcbounds import compute; funcs = compute(A)   # [(start, end)]
"""
import bisect
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n64feat import load_all  # noqa: E402

NOP = 0
MANUAL_SEEDS = [0x80000450,   # boot(): reached from the entry stub via jr $t2
                0x8000e7a0]   # handle_CpU: reached from the __osException dispatch table


def decode_flow(x, pc):
    """Return (kind, target) for control-flow instructions, else (None, None).
    kind: 'b' conditional branch, 'B' unconditional branch, 'j' jump, 'jal', 'jr', 'eret'."""
    op = x >> 26
    rs = (x >> 21) & 31
    rt = (x >> 16) & 31
    fn = x & 63
    simm = (x & 0xFFFF) - (0x10000 if x & 0x8000 else 0)
    btarget = (pc + 4 + (simm << 2)) & 0xFFFFFFFF
    if op == 0:
        if fn == 8:
            return ("jr", None)
        if fn == 9:
            return ("jalr", None)
        return (None, None)
    if op == 1:                                   # REGIMM
        if rt in (0, 1, 2, 3, 16, 17, 18, 19):
            if rt in (1, 17) and rs == 0:         # bgez $zero = always
                return ("B", btarget)
            if rt in (0, 16) and rs == 0:         # bltz $zero = never
                return ("never", btarget)
            return ("b", btarget)
        return (None, None)
    if op == 2:
        return ("j", ((x & 0x3FFFFFF) << 2) | (pc & 0xF0000000))
    if op == 3:
        return ("jal", ((x & 0x3FFFFFF) << 2) | (pc & 0xF0000000))
    if op in (4, 0x14):                           # beq / beql
        if rs == rt:
            return ("B", btarget)
        return ("b", btarget)
    if op in (5, 6, 7, 0x15, 0x16, 0x17):          # bne bgtz blez (+likely)
        if op in (5, 0x15) and rs == rt:
            return ("never", btarget)
        return ("b", btarget)
    if op == 0x11 and rs == 8:                    # bc1f/bc1t (+likely)
        return ("b", btarget)
    if x == 0x42000018:
        return ("eret", None)
    return (None, None)


VALID_SPECIAL = {0, 2, 3, 4, 6, 7, 8, 9, 0xc, 0xd, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
                 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
                 0x38, 0x3a, 0x3b, 0x3c, 0x3e, 0x3f}                     # no tge..tne traps
VALID_OPS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
             0x1a, 0x1b, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
             0x30, 0x31, 0x34, 0x35, 0x37, 0x38, 0x39, 0x3c, 0x3d, 0x3f}
VALID_CACHE = {0, 1, 4, 5, 8, 9, 0xd, 0x10, 0x11, 0x14, 0x15, 0x19, 0x1d}
VALID_FPU_FN = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x20, 0x21, 0x24, 0x25,
                0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f}


def invalid_count(words):
    """Number of words that are not plausible R4300 instructions."""
    n = 0
    for x in words:
        op = x >> 26
        rs = (x >> 21) & 31
        rt = (x >> 16) & 31
        fn = x & 63
        ok = op in VALID_OPS
        if ok and op == 0:
            ok = fn in VALID_SPECIAL
        elif ok and op == 1:
            ok = rt in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13)          # no tgei..tnei traps
        elif ok and op == 0x10:                       # COP0
            rd = (x >> 11) & 31
            ok = (rs in (0, 4) and (x & 0x7FF) == 0 and rd not in (7, 21, 22, 23, 24, 25, 31)) or                  (rs >= 16 and fn in (1, 2, 6, 8, 0x18))
        elif ok and op == 0x11:                       # COP1
            rd = (x >> 11) & 31
            if rs in (0, 1, 4, 5):
                ok = (x & 0x7FF) == 0
            elif rs in (2, 6):                        # cfc1 / ctc1: only the control/status reg
                ok = (x & 0x7FF) == 0 and rd == 31
            elif rs == 8:
                ok = rt in (0, 1, 2, 3)
            elif rs in (16, 17):
                ok = fn in VALID_FPU_FN
            elif rs in (20, 21):
                ok = fn in (0x20, 0x21)
            else:
                ok = False
        elif ok and op == 0x2F:
            ok = rt in VALID_CACHE
        if not ok:
            n += 1
    return n


def load_names():
    import json
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "symbols", "functions.json")
    try:
        return {int(k, 16): v for k, v in json.load(open(path, encoding="utf-8")).items() if not k.startswith("_")}
    except OSError:
        return {}


def branches_out(A, start, end):
    """True if any branch/jump-table-free branch inside [start, end) targets outside it."""
    for i, x in enumerate(A.words(start, (end - start) // 4)):
        pc = start + 4 * i
        kind, tgt = decode_flow(x, pc)
        if kind in ("b", "B") and not (start <= tgt < end):
            return True
        if kind == "j" and not (start <= tgt < end) and tgt not in A.seeds:
            return True
    return False


def uses_privileged(w):
    """cop0 / cache / ll / sc / sync / syscall: never in compiled game code, and not
    supported by N64Recomp. Named libultra functions are allowed to use them."""
    for x in w:
        op = x >> 26
        if op in (0x10, 0x2F, 0x30, 0x38) or (op == 0 and (x & 63) in (0x0C, 0x0F)):
            return True
    return False


def writes_zero_reg(x):
    """True for a non-nop instruction whose destination register is $zero."""
    if x == 0:
        return False
    op = x >> 26
    rt = (x >> 16) & 31
    rd = (x >> 11) & 31
    fn = x & 63
    if op == 0:
        # R-type ALU / shifts write rd (not jr/jalr/mult/div/syscall/break/sync/trap)
        if fn in (8, 0x0C, 0x0D, 0x0F, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
                  0x11, 0x13, 0x30, 0x31, 0x32, 0x33, 0x34, 0x36):
            return False
        return rd == 0
    if op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x18, 0x19,      # ALU immediates, lui, daddi(u)
              0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x1A, 0x1B, 0x37, 0x30, 0x34):  # loads
        return rt == 0
    return False


def has_control_flow(w):
    for x in w:
        op = x >> 26
        if op in (1, 2, 3, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or (op == 0 and (x & 63) in (8, 9)) \
                or x == 0x42000018 or (op == 0x11 and ((x >> 21) & 31) == 8):
            return True
    return False


def looks_like_data(w, named=False):
    """Heuristic: invalid instructions, or mostly zero words (ignoring trailing padding),
    or writes to $zero, or no control flow at all.
    Unless `named` (a known libultra function), privileged instructions also mean data."""
    if not w or invalid_count(w) > 0:
        return True
    if any(writes_zero_reg(x) for x in w):
        return True
    if not has_control_flow(w):
        return True
    if not named and uses_privileged(w):
        return True
    n = len(w)
    while n > 0 and w[n - 1] == 0:
        n -= 1
    zeros = sum(1 for x in w[:n] if x == 0)
    if n >= 6 and zeros * 10 > n * 4:                      # > 40% nops inside the body
        return True
    return False


def walk(A, start, limit, hard=None):
    """Walk CFG from start. limit = next seed (flow reaching it exactly means that
    seed is a fragment of this function); hard = next anchored seed, which flow may
    never pass (fragment seeds between limit and hard may be jumped over).
    Returns (max_reached_pc, hit_limit)."""
    if hard is None:
        hard = limit
    far = (lambda t: t >= hard)
    seen = set()
    work = [start]
    maxpc = start
    hit = False          # flow reaches exactly `limit` (fall-through or branch to it)
    while work:
        pc = work.pop()
        while True:
            if pc == limit:
                hit = True
                break
            if far(pc):
                break
            if pc in seen:
                break
            seen.add(pc)
            maxpc = max(maxpc, pc)
            x = A.word(pc)
            if x is None:
                break
            kind, tgt = decode_flow(x, pc)
            if kind is None or kind in ("jal", "jalr", "never"):
                pc += 4
                continue
            # delay slot executes
            maxpc = max(maxpc, pc + 4)
            seen.add(pc + 4)
            if kind in ("b", "B"):
                if tgt == limit:
                    hit = True
                elif tgt >= start and not far(tgt):
                    work.append(tgt)
                if kind == "b":
                    pc += 8
                    continue
                break
            if kind == "j":
                if tgt == limit and tgt not in A.anchored:
                    hit = True
                elif tgt >= start and not far(tgt) and tgt not in A.anchored:
                    work.append(tgt)
                break
            if kind == "jr":
                tgts = jr_targets(A, pc, start, hard)
                for t in tgts:
                    if t == limit:
                        hit = True
                    elif t >= start and not far(t):
                        work.append(t)
            break  # jr / eret
    return maxpc, hit


def jr_targets(A, pc, start, limit):
    """For `jr $reg` (reg != ra): resolve a jump table or a constant target.
    Tracks lui/addiu/ori/addu constants linearly from `start` to the jr so that
    hoisted table bases (e.g. `s3 = table` at function entry) are found."""
    x = A.word(pc)
    reg = (x >> 21) & 31
    if reg == 31:
        return []
    val = {}                      # reg -> constant (table base or address)
    val2 = {}                     # same, but not cleared by jal (libultra asm ignores the ABI)
    lw_of = {}                    # reg -> (base_const, offset) from the last lw reg, off(base)
    q = start
    while q < pc:
        w = A.word(q)
        op = w >> 26
        rs = (w >> 21) & 31
        rt = (w >> 16) & 31
        rd = (w >> 11) & 31
        imm = w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 0x0F:
            val[rt] = val2[rt] = imm << 16
            lw_of.pop(rt, None)
        elif op == 0x09 or op == 0x0D:                        # addiu / ori
            for d in (val, val2):
                if rs in d:
                    d[rt] = (d[rs] + simm) & 0xFFFFFFFF if op == 0x09 else d[rs] | imm
                else:
                    d.pop(rt, None)
            lw_of.pop(rt, None)
        elif op == 0 and (w & 63) == 0x21:                     # addu rd, rs, rt
            if rs in val and rt not in val:
                val[rd] = val[rs]
            elif rt in val and rs not in val:
                val[rd] = val[rt]
            else:
                val.pop(rd, None)
            lw_of.pop(rd, None)
        elif op == 0x23:                                       # lw rt, off(rs)
            lw_of[rt] = (val.get(rs, val2.get(rs)), simm)      # capture base constant now
            val.pop(rt, None)
            val2.pop(rt, None)
        elif op == 0:
            if rd != 0 and (w & 63) not in (8, 9):
                val.pop(rd, None)
                val2.pop(rd, None)
                lw_of.pop(rd, None)
        elif op in (0x08, 0x0A, 0x0B, 0x0C, 0x0E) or 0x20 <= op <= 0x26 or op in (0x30, 0x31, 0x35, 0x37):
            val.pop(rt, None)
            val2.pop(rt, None)
            lw_of.pop(rt, None)
        elif op == 3:                                          # jal clobbers v0/v1/at/a*/t*
            for r in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 31):
                val.pop(r, None)
                lw_of.pop(r, None)
        q += 4
    if reg in val or reg in val2:                              # jr to a constant address
        return [val.get(reg, val2.get(reg))]
    if reg in lw_of and lw_of[reg][0] is not None:
        table = (lw_of[reg][0] + lw_of[reg][1]) & 0xFFFFFFFF
        out = []
        for n in range(0, 512):
            e = A.word(table + 4 * n)
            if e is None or not (start <= e < limit) or e & 3:
                break
            out.append(e)
        A.table_targets.update(out)
        return out
    return []


def compute(A, text_end=None):
    """Return list of (start, end, merged_seeds) across all segments, pruned to real code.

    junk  = functions containing invalid instructions (data misread as code)
    live  = closure from roots (entry, manual seeds, pointer targets found in ROM data)
            over jal / lui+addiu references, never passing through junk
    text end per segment = end of the last live function
    kept  = live functions, plus non-junk functions located before the text end
    """
    base_seeds = sorted(set(A.funcs) | set(MANUAL_SEEDS))
    A.table_targets = set()
    A.named = set(load_names())
    junk0 = {a for a, f in A.feat.items() if looks_like_data(f["words"], named=True)}
    A.junk0 = junk0
    funcs = _compute(A, base_seeds, refs_from=lambda a: a not in junk0)
    for _ in range(4):
        junk, live, text_end = _classify(A, funcs)
        junk_ranges = sorted((s_, e_) for s_, e_, _ in funcs if s_ in junk)

        def real(a, _je=junk_ranges, _te=text_end):
            seg = A.seg_of(a)
            if seg is None or a >= _te[seg[0]] or a in junk0:
                return False
            i = bisect.bisect_right(_je, (a, 0xFFFFFFFF)) - 1
            return not (i >= 0 and _je[i][0] <= a < _je[i][1])
        jal_tgts = set()
        for a, f in A.feat.items():
            if real(a):
                jal_tgts.update(t for t in f["callees"] if A.seg_of(t) and t < text_end[A.seg_of(t)[0]])
                jal_tgts.update(t for t in f["ptrs"] if A.seg_of(t) and t < text_end[A.seg_of(t)[0]]
                                and t % 4 == 0 and A.word(t) != 0)
        covered = [(s_, e_) for s_, e_, _ in funcs if s_ not in junk]
        jal_tgts |= pointer_seeds(A, text_end, covered)
        jal_tgts |= prologue_seeds(A, text_end, covered)
        funcs = _compute(A, sorted(set(base_seeds) | jal_tgts), refs_from=real)
    junk, live, text_end = _classify(A, funcs)
    A.text_end, A.junk, A.live = text_end, junk, live
    A.all_funcs = funcs
    return [f for f in funcs if f[0] not in junk and f[0] < text_end[A.seg_of(f[0])[0]]]


def _inside(covered, a):
    i = bisect.bisect_right(covered, (a, 0xFFFFFFFF)) - 1
    return i >= 0 and covered[i][0] <= a < covered[i][1]


def pointer_seeds(A, text_end, covered):
    """Data words anywhere in the ROM that point into code but not into a known function."""
    out = set()
    n = len(A.rom) // 4
    for w in struct.unpack(">%dI" % n, A.rom[:n * 4]):
        seg = A.seg_of(w)
        if seg and w % 4 == 0 and w < text_end[seg[0]] and not _inside(covered, w) and A.word(w) not in (None, 0):
            out.add(w)
    return out


def prologue_seeds(A, text_end, covered):
    """Gaps between functions that begin (after nop padding) with addiu $sp, $sp, -N."""
    out = set()
    for seg in A.segs:
        tend = text_end[seg[0]]
        cs = [c for c in covered if seg[1] <= c[0] < seg[2]]
        for (s0, e0), (s1, e1) in zip(cs, cs[1:]):
            a = e0
            while a < s1 and A.word(a) == 0:
                a += 4
            if a < s1 and a < tend:
                x = A.word(a)
                if (x >> 16) == 0x27BD and (x & 0x8000):
                    out.add(a)
    return out


def _classify(A, funcs):
    starts = {f[0]: f for f in funcs}
    junk = set()
    soft = set()
    for s_, e_, _ in funcs:
        w = A.words(s_, (e_ - s_) // 4)
        if looks_like_data(w, named=True) or (s_ not in A.named and branches_out(A, s_, e_)):
            junk.add(s_)
        elif s_ not in A.named and uses_privileged(w):
            soft.add(s_)
    # soft junk is kept if some hard-non-junk function jal-calls it (N64Recomp will then
    # complain about the instruction and we handle it by name)
    called = set()
    for s_, e_, _ in funcs:
        if s_ in junk:
            continue
        for x in A.words(s_, (e_ - s_) // 4):
            if (x >> 26) == 3:
                called.add(((x & 0x3FFFFFF) << 2) | 0x80000000)
    junk |= {a for a in soft if a not in called}
    # references between functions (recomputed on the walker's extents)
    refs = {}
    for s_, e_, _ in funcs:
        if s_ in junk:
            continue
        out = set()
        hi = {}
        for i, x in enumerate(A.words(s_, (e_ - s_) // 4)):
            op = x >> 26
            rs = (x >> 21) & 31
            rt = (x >> 16) & 31
            imm = x & 0xFFFF
            if op == 3:
                out.add(((x & 0x3FFFFFF) << 2) | 0x80000000)
            elif op == 0x0F:
                hi[rt] = imm << 16
            elif op in (0x09, 0x0D) and rs in hi:
                v = (hi[rs] | imm) if op == 0x0D else (hi[rs] + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
                out.add(v)
                hi.pop(rt, None)
            elif op != 0x0F:
                hi.pop(rt, None)
        refs[s_] = {t for t in out if t in starts}
    n = len(A.rom) // 4
    data_refs = {w for w in struct.unpack(">%dI" % n, A.rom[:n * 4]) if w in starts}
    roots = ({A.funcs[0]} | set(MANUAL_SEEDS) | data_refs) - junk
    live = set()
    work = list(roots)
    while work:
        a = work.pop()
        if a in live or a in junk or a not in starts:
            continue
        live.add(a)
        work.extend(refs.get(a, ()))
    # jal targets of live code are certainly code; pointer targets may be data tables
    jal_live = set()
    for s_, e_, _ in funcs:
        if s_ in live:
            for x in A.words(s_, (e_ - s_) // 4):
                if (x >> 26) == 3:
                    jal_live.add(((x & 0x3FFFFFF) << 2) | 0x80000000)
    jal_live |= {A.funcs[0]} | set(MANUAL_SEEDS)
    text_end = {}
    for seg in A.segs:
        ends = [e_ for s_, e_, _ in funcs if seg[1] <= s_ < seg[2] and s_ in live and s_ in jal_live]
        text_end[seg[0]] = max(ends) if ends else seg[1]
    return junk, live, text_end


def _compute(A, seeds, refs_from):
    # drop seeds that are only nop padding up to the next seed
    keep = []
    for i, a in enumerate(seeds):
        nxt = seeds[i + 1] if i + 1 < len(seeds) else None
        seg = A.seg_of(a)
        lim = nxt if (nxt is not None and A.seg_of(nxt) == seg) else seg[2]
        n = (lim - a) // 4
        if n <= 8 and all(w == 0 for w in A.words(a, n)):
            continue
        keep.append(a)
    A.seeds = set(keep)
    strong = anchors(A, keep, refs_from)
    moved = []
    for a in keep:
        b = a
        if a not in strong and a not in MANUAL_SEEDS:
            while A.word(b) == 0 and b < a + 64:
                b += 4
        moved.append(b)
    seeds = sorted(set(moved))
    A.seeds = set(seeds)
    A.anchored = anchors(A, seeds, refs_from)
    anchored_sorted = sorted(a for a in seeds if a in A.anchored or a in MANUAL_SEEDS)
    out = []
    i = 0
    while i < len(seeds):
        s = seeds[i]
        seg = A.seg_of(s)
        merged = [s]
        j = i + 1
        k = bisect.bisect_right(anchored_sorted, s)
        hard = anchored_sorted[k] if k < len(anchored_sorted) and A.seg_of(anchored_sorted[k]) == seg else seg[2]
        trusted = s in A.anchored or s in MANUAL_SEEDS or s not in getattr(A, "junk0", ())
        while True:
            nxt = seeds[j] if j < len(seeds) and A.seg_of(seeds[j]) == seg else None
            limit = nxt if nxt is not None else seg[2]
            maxpc, hit = walk(A, s, limit, hard if trusted else limit)
            if hit and nxt is not None and nxt not in A.anchored:
                merged.append(nxt)
                j += 1
                continue
            break
        end = maxpc + 4
        limit = seeds[j] if j < len(seeds) and A.seg_of(seeds[j]) == seg else seg[2]
        while end < limit and A.word(end) == NOP and (end % 16) != 0:
            end += 4
        out.append((s, end, merged))
        i = j
    # drop unreferenced seeds that lie inside another function's extent (fragments)
    final = []
    for idx, (s, e, m) in enumerate(out):
        inside = idx > 0 and out[idx - 1][1] > s and A.seg_of(out[idx - 1][0]) == A.seg_of(s)
        if inside and s not in A.anchored and s not in MANUAL_SEEDS and (s not in A.weak or s in A.table_targets):
            if final:
                final[-1] = (final[-1][0], max(final[-1][1], e), final[-1][2] + m)
            continue
        final.append((s, e, m))
    return final


def anchors(A, seeds, refs_from=None):
    """Strong anchors: seeds referenced by jal / lui+addiu constants from real code.
    Also records A.weak: seeds referenced by data words anywhere in the ROM (function
    pointer tables, but also jump-table entries, so these are only used for liveness)."""
    sset = set(seeds)
    anc = set()
    for a, f in A.feat.items():
        if refs_from is not None and not (A.seg_of(a) and refs_from(a)):
            continue
        anc.update(t for t in f["callees"] if t in sset)
        anc.update(c for c in f["ptrs"] if c in sset)
    n = len(A.rom) // 4
    A.weak = {w for w in struct.unpack(">%dI" % n, A.rom[:n * 4]) if w in sset}
    return anc


def main():
    rom, segs, A = load_all()
    funcs = compute(A)
    print("seeds %d -> functions %d, anchored %d, junk %d, live %d" % (len(A.funcs), len(funcs), len(A.anchored), len(A.junk), len(A.live)))
    print("text ends:", {k: hex(v) for k, v in A.text_end.items()})
    dead = [f for f in funcs if f[0] not in A.live]
    print("kept but not live (dead code / pointer-called): %d, e.g. %s" % (len(dead), ["%08x" % f[0] for f in dead[:12]]))
    merges = [f for f in funcs if len(f[2]) > 1]
    print("merged groups: %d" % len(merges))
    for s, e, m in merges[:25]:
        print("   %08x-%08x absorbs %s" % (s, e, ",".join("%08x" % x for x in m[1:])))
    # jal targets not matching a function start
    starts = {f[0] for f in funcs}
    jal_targets = set()
    for a, f in A.feat.items():
        if a in A.live:
            jal_targets.update(f["callees"])
    missing = sorted(t for t in jal_targets if t not in starts and A.seg_of(t))
    print("jal targets that are not function starts: %d %s" % (len(missing), ["%08x" % t for t in missing[:20]]))
    # gaps between consecutive functions in each segment
    for seg in segs:
        fs = [f for f in funcs if seg[1] <= f[0] < seg[2]]
        gaps = []
        for (s, e, _), (s2, _, _) in zip(fs, fs[1:]):
            if s2 > e:
                gaps.append((e, s2))
        big = [g for g in gaps if g[1] - g[0] > 16]
        print("%s: %d functions, %d gaps (%d > 16 bytes), total gap bytes %d" % (
            seg[0], len(fs), len(gaps), len(big), sum(b - a for a, b in gaps)))
        for a, b in big[:15]:
            w = A.words(a, min(4, (b - a) // 4))
            print("   gap %08x-%08x (%d) %s" % (a, b, b - a, " ".join("%08x" % x for x in w)))


if __name__ == "__main__":
    main()
