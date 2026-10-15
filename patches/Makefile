TARGET = patches.elf

CC      ?= clang
LD      ?= ld.lld
PYTHON  ?= python

# No decompilation exists for California Speed; patches are written against our own headers.
DECOMP_INCLUDE_DIRS :=

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg \
			-fomit-frame-pointer -ffreestanding -Wall -Wextra
CPPFLAGS := -DMIPS $(DECOMP_INCLUDE_DIRS)
LDFLAGS  := -nostdlib -T patches.ld -Map patches.map --unresolved-symbols=ignore-all --emit-relocs

C_SRCS := $(wildcard *.c)
C_OBJS := $(C_SRCS:.c=.o)
C_DEPS := $(C_SRCS:.c=.d)

# The LLVM installed on this machine may lack the Mips target. In that case produce an
# empty patches.elf so the rest of the build still works (no patches are applied).
MIPS_OK := $(shell $(PYTHON) ../tools/mips_cc_check.py $(CC))

ifeq ($(MIPS_OK),yes)
$(TARGET): $(C_OBJS) patches.ld
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(C_OBJS): %.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@
else
$(TARGET): $(C_SRCS) patches.ld
	@echo WARNING: $(CC) cannot target MIPS - writing an empty $(TARGET), no patches will be applied.
	$(PYTHON) ../tools/make_empty_patches_elf.py $(TARGET)
endif

ifeq ($(OS),Windows_NT)
clean:
	powershell -NoProfile -Command "foreach ($$path in @('$(C_OBJS)','$(TARGET)','$(C_DEPS)','patches.map')) { if (Test-Path -LiteralPath $$path) { Remove-Item -LiteralPath $$path -Force } }"
else
clean:
	rm -f $(C_OBJS) $(TARGET) $(C_DEPS) patches.map
endif

-include $(C_DEPS)
