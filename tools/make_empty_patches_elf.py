"""Create a minimal, empty MIPS ELF so N64Recomp's patch pipeline runs without a MIPS compiler.

Used by patches/Makefile when no MIPS-capable compiler is available, so the build still works without patches.
Produces patches/patches.elf with empty .text/.recomp_patch/.recomp_export/.recomp_event/.data
sections at the patch RAM base and an empty symbol table.
"""
import struct
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "patches/patches.elf"
RAMBASE = 0x80801000

names = [b"", b".text", b".recomp_patch", b".recomp_export", b".recomp_event", b".rodata", b".data", b".bss", b".symtab", b".strtab", b".shstrtab"]
shstr = b""
name_off = {}
for n in names:
    name_off[n] = len(shstr)
    shstr += n + b"\0"
strtab = b"\0dummy\0"
# one null symbol + one function symbol "dummy" (jr ra; nop) so the output binary is not empty
text = struct.pack(">II", 0x03E00008, 0x00000000)
symtab = b"\0" * 16 + struct.pack(">IIIBBH", 1, RAMBASE, len(text), 0x12, 0, 1)   # STB_GLOBAL|STT_FUNC, shndx .text

# layout: ELF header (52) | program header (32) | text | symtab | strtab | shstrtab | section headers
off = 52
phoff = off; off += 32
text_off = off; off += len(text)
symtab_off = off; off += len(symtab)
strtab_off = off; off += len(strtab)
shstr_off = off; off += len(shstr)
off = (off + 3) & ~3
shoff = off

SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_NOBITS = 0, 1, 2, 3, 8
SHF_WRITE, SHF_ALLOC, SHF_EXEC = 1, 2, 4

def sh(name, typ, flags, addr, offset, size, link=0, info=0, align=4, entsize=0):
    return struct.pack(">IIIIIIIIII", name_off[name], typ, flags, addr, offset, size, link, info, align, entsize)

sections = [
    sh(b"", SHT_NULL, 0, 0, 0, 0, align=0),
    sh(b".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXEC, RAMBASE, text_off, len(text), align=16),
    sh(b".recomp_patch", SHT_PROGBITS, SHF_ALLOC | SHF_EXEC, RAMBASE + len(text), shstr_off, 0, align=16),
    sh(b".recomp_export", SHT_PROGBITS, SHF_ALLOC | SHF_EXEC, RAMBASE + len(text), shstr_off, 0, align=16),
    sh(b".recomp_event", SHT_PROGBITS, SHF_ALLOC | SHF_EXEC, RAMBASE + len(text), shstr_off, 0, align=16),
    sh(b".rodata", SHT_PROGBITS, SHF_ALLOC, RAMBASE + len(text), shstr_off, 0, align=16),
    sh(b".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, RAMBASE + len(text), shstr_off, 0, align=16),
    sh(b".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, RAMBASE + len(text), shstr_off, 0, align=16),
    sh(b".symtab", SHT_SYMTAB, 0, 0, symtab_off, len(symtab), link=9, info=1, entsize=16),
    sh(b".strtab", SHT_STRTAB, 0, 0, strtab_off, len(strtab), align=1),
    sh(b".shstrtab", SHT_STRTAB, 0, 0, shstr_off, len(shstr), align=1),
]
shnum = len(sections)
ident = b"\x7fELF" + bytes([1, 2, 1, 0]) + b"\0" * 8      # 32-bit, big-endian, version 1
EM_MIPS, ET_EXEC = 8, 2
e_flags = 0x10000000 | 0x00000004                         # EF_MIPS_ARCH_2 | EF_MIPS_ABI_O32-ish
ehdr = ident + struct.pack(">HHIIIIIHHHHHH", ET_EXEC, EM_MIPS, 1, RAMBASE, phoff, shoff, e_flags,
                           52, 32, 1, 40, shnum, shnum - 1)
# PT_LOAD: .text at vaddr RAMBASE, "rom" (paddr) 0 - N64Recomp maps sections to segments for data
phdr = struct.pack(">IIIIIIII", 1, text_off, RAMBASE, 0, len(text), len(text), 5, 16)
body = bytearray(ehdr + phdr + text + symtab + strtab + shstr)
body += b"\0" * (shoff - len(body))
body += b"".join(sections)
open(OUT, "wb").write(body)
print("wrote %s (%d bytes, %d sections)" % (OUT, len(body), shnum))
