"""
N64 ROM loader for Binary Ninja (big-endian .z64 only).

What it does:
  * Detects the z64 magic (80 37 12 40). Byte-swapped .n64/.v64 files are rejected
    with a log message telling you to convert first.
  * Maps the boot segment: ROM 0x1000 -> header entry point. Its size is derived by
    parsing the IDO/libultra BSS-clear loop at the entry point (lui/addiu t0 = bss start,
    lui/addiu t1 = bss size). Falls back to 1 MiB if the pattern isn't found.
  * Maps the boot BSS as a zero-filled RW segment.
  * Maps the whole cartridge at 0xB0000000 (KSEG1 PI domain 1) as read-only data, so
    DMA source addresses resolve.
  * Adds symbols for the common RCP/PI/VI/AI/SI/MI hardware registers.
  * Reads extra segments (overlays, main code, etc.) from an optional sidecar JSON file
    next to the ROM:  <rom filename>.segments.json
        [ {"name": "main", "rom": "0x2CD60", "vram": "0x80063710", "size": "0x79F10",
           "bss_size": "0x929E0"} , ... ]
    Numbers may be ints or hex strings.
"""
import json
import os
import struct

from binaryninja import (Architecture, BinaryView, Endianness, SegmentFlag,
                         SectionSemantics, Symbol, SymbolType, log_info,
                         log_warn, log_error)

Z64_MAGIC = b"\x80\x37\x12\x40"
N64_MAGIC = b"\x37\x80\x40\x12"
V64_MAGIC = b"\x40\x12\x37\x80"

RWX = (SegmentFlag.SegmentReadable | SegmentFlag.SegmentWritable |
       SegmentFlag.SegmentExecutable | SegmentFlag.SegmentContainsCode |
       SegmentFlag.SegmentContainsData)
RW = (SegmentFlag.SegmentReadable | SegmentFlag.SegmentWritable |
      SegmentFlag.SegmentContainsData)
RO = SegmentFlag.SegmentReadable | SegmentFlag.SegmentContainsData

HW_REGS = {
    0xA4040000: "SP_MEM_ADDR_REG", 0xA4040004: "SP_DRAM_ADDR_REG",
    0xA4040008: "SP_RD_LEN_REG", 0xA404000C: "SP_WR_LEN_REG",
    0xA4040010: "SP_STATUS_REG", 0xA4040014: "SP_DMA_FULL_REG",
    0xA4040018: "SP_DMA_BUSY_REG", 0xA404001C: "SP_SEMAPHORE_REG",
    0xA4080000: "SP_PC_REG", 0xA4100000: "DPC_START_REG", 0xA4100004: "DPC_END_REG",
    0xA4100008: "DPC_CURRENT_REG", 0xA410000C: "DPC_STATUS_REG",
    0xA4300000: "MI_INIT_MODE_REG", 0xA4300004: "MI_VERSION_REG",
    0xA4300008: "MI_INTR_REG", 0xA430000C: "MI_INTR_MASK_REG",
    0xA4400000: "VI_STATUS_REG", 0xA4400004: "VI_ORIGIN_REG", 0xA4400008: "VI_WIDTH_REG",
    0xA440000C: "VI_INTR_REG", 0xA4400010: "VI_CURRENT_REG", 0xA4400014: "VI_BURST_REG",
    0xA4400018: "VI_V_SYNC_REG", 0xA440001C: "VI_H_SYNC_REG", 0xA4400020: "VI_LEAP_REG",
    0xA4400024: "VI_H_START_REG", 0xA4400028: "VI_V_START_REG", 0xA440002C: "VI_V_BURST_REG",
    0xA4400030: "VI_X_SCALE_REG", 0xA4400034: "VI_Y_SCALE_REG",
    0xA4500000: "AI_DRAM_ADDR_REG", 0xA4500004: "AI_LEN_REG", 0xA4500008: "AI_CONTROL_REG",
    0xA450000C: "AI_STATUS_REG", 0xA4500010: "AI_DACRATE_REG", 0xA4500014: "AI_BITRATE_REG",
    0xA4600000: "PI_DRAM_ADDR_REG", 0xA4600004: "PI_CART_ADDR_REG",
    0xA4600008: "PI_RD_LEN_REG", 0xA460000C: "PI_WR_LEN_REG", 0xA4600010: "PI_STATUS_REG",
    0xA4600014: "PI_BSD_DOM1_LAT_REG", 0xA4600018: "PI_BSD_DOM1_PWD_REG",
    0xA460001C: "PI_BSD_DOM1_PGS_REG", 0xA4600020: "PI_BSD_DOM1_RLS_REG",
    0xA4600024: "PI_BSD_DOM2_LAT_REG", 0xA4600028: "PI_BSD_DOM2_PWD_REG",
    0xA460002C: "PI_BSD_DOM2_PGS_REG", 0xA4600030: "PI_BSD_DOM2_RLS_REG",
    0xA4700000: "RI_MODE_REG", 0xA4700004: "RI_CONFIG_REG", 0xA470000C: "RI_SELECT_REG",
    0xA4700010: "RI_REFRESH_REG",
    0xA4800000: "SI_DRAM_ADDR_REG", 0xA4800004: "SI_PIF_ADDR_RD64B_REG",
    0xA4800010: "SI_PIF_ADDR_WR64B_REG", 0xA4800018: "SI_STATUS_REG",
    0xA4000000: "SP_DMEM", 0xA4001000: "SP_IMEM",
}


def _num(v):
    return int(v, 0) if isinstance(v, str) else int(v)


class N64ROMView(BinaryView):
    name = "N64 ROM"
    long_name = "Nintendo 64 ROM (z64)"

    def __init__(self, data):
        BinaryView.__init__(self, parent_view=data, file_metadata=data.file)
        self.raw = data
        self.entry = 0

    @classmethod
    def is_valid_for_data(cls, data):
        magic = data.read(0, 4)
        if magic == Z64_MAGIC:
            return True
        if magic in (N64_MAGIC, V64_MAGIC):
            log_warn("N64 ROM loader: byte-swapped ROM detected; convert it to .z64 "
                     "(big-endian) first.")
        return False

    def perform_get_default_endianness(self):
        return Endianness.BigEndian

    def perform_get_address_size(self):
        return 4

    def perform_is_executable(self):
        return True

    def perform_get_entry_point(self):
        return self.entry

    # ---- helpers -------------------------------------------------------
    def _u32(self, off):
        return struct.unpack(">I", self.raw.read(off, 4))[0]

    def _parse_bss_clear(self, rom_off):
        """Find lui/addiu pairs for t0 (bss start) and t1 (bss size) in the entry stub."""
        hi = {}
        vals = {}
        for i in range(24):
            w = self._u32(rom_off + 4 * i)
            op = w >> 26
            rt = (w >> 16) & 31
            rs = (w >> 21) & 31
            imm = w & 0xFFFF
            if op == 0x0F:                                # lui
                hi[rt] = imm << 16
            elif op == 0x09 and rs == rt and rt in hi:    # addiu rt, rt, imm
                simm = imm - 0x10000 if imm & 0x8000 else imm
                vals[rt] = (hi[rt] + simm) & 0xFFFFFFFF
            elif op == 0x0D and rs == rt and rt in hi:    # ori
                vals[rt] = hi[rt] | imm
        return vals.get(8), vals.get(9)                   # t0, t1

    def _sidecar_segments(self):
        fn = self.file.original_filename
        if not fn:
            return []
        path = fn + ".segments.json"
        if not os.path.exists(path):
            log_info("N64 ROM loader: no sidecar %s (only boot segment mapped)" % path)
            return []
        try:
            with open(path, "r", encoding="utf-8") as f:
                segs = json.load(f)
            log_info("N64 ROM loader: loaded %d extra segment(s) from %s" % (len(segs), path))
            return segs
        except Exception as e:  # noqa: BLE001
            log_error("N64 ROM loader: failed to read %s: %s" % (path, e))
            return []

    def _map_code_segment(self, name, rom, vram, size, bss_size=0):
        self.add_auto_segment(vram, size, rom, size, RWX)
        self.add_auto_section("." + name, vram, size,
                              SectionSemantics.ReadOnlyCodeSectionSemantics)
        if bss_size:
            self.add_auto_segment(vram + size, bss_size, 0, 0, RW)
            self.add_auto_section("." + name + ".bss", vram + size, bss_size,
                                  SectionSemantics.ReadWriteDataSectionSemantics)
        log_info("N64 ROM loader: %-8s ROM %08X -> %08X..%08X (bss %X)" %
                 (name, rom, vram, vram + size, bss_size))

    # ---- init ----------------------------------------------------------
    def init(self):
        try:
            arch = Architecture["mips32"]            # big-endian MIPS in Binary Ninja
            self.arch = arch
            self.platform = arch.standalone_platform

            rom_len = self.raw.length
            self.entry = self._u32(0x08)
            title = self.raw.read(0x20, 20).decode("ascii", "replace").strip()
            code = self.raw.read(0x3B, 4).decode("ascii", "replace")
            log_info("N64 ROM loader: '%s' [%s] entry %08X size %X" %
                     (title, code, self.entry, rom_len))

            # Boot segment: ROM 0x1000 -> entry.
            bss_start, bss_size = self._parse_bss_clear(0x1000)
            if bss_start and bss_start > self.entry and (bss_start - self.entry) < rom_len:
                boot_size = bss_start - self.entry
            else:
                log_warn("N64 ROM loader: could not parse BSS-clear loop; "
                         "using 1 MiB boot segment")
                boot_size, bss_size = 0x100000, 0
            self._map_code_segment("boot", 0x1000, self.entry, boot_size, bss_size or 0)

            # Extra segments from sidecar.
            for s in self._sidecar_segments():
                self._map_code_segment(s.get("name", "seg"), _num(s["rom"]), _num(s["vram"]),
                                       _num(s["size"]), _num(s.get("bss_size", 0)))

            # Whole cartridge in the PI domain-1 address space (read-only data).
            self.add_auto_segment(0xB0000000, rom_len, 0, rom_len, RO)
            self.add_auto_section(".cart", 0xB0000000, rom_len,
                                  SectionSemantics.ReadOnlyDataSectionSemantics)
            self.define_auto_symbol(Symbol(SymbolType.DataSymbol, 0xB0000000, "rom_header"))
            self.define_auto_symbol(Symbol(SymbolType.DataSymbol, 0xB0000040, "ipl3"))

            # Hardware registers (no backing data).
            self.add_auto_segment(0xA4000000, 0x00900000, 0, 0, RW)
            for addr, nm in HW_REGS.items():
                self.define_auto_symbol(Symbol(SymbolType.DataSymbol, addr, nm))

            # Entry point.
            self.add_entry_point(self.entry)
            self.define_auto_symbol(Symbol(SymbolType.FunctionSymbol, self.entry, "entrypoint"))
            return True
        except Exception as e:  # noqa: BLE001
            log_error("N64 ROM loader: init failed: %s" % e)
            return False


N64ROMView.register()
