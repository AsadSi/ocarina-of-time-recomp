# Builds the mod code for Ocarina of Time: Recompiled (PAL 1.1). Adapted from MMRecompModTemplate.
# On macOS, CC and LD must point at Homebrew's clang and ld.lld, since Apple clang has no MIPS target.
BUILD_DIR := build
CC ?= clang
LD ?= ld.lld
OOT := ../../../../oot

TARGET := $(BUILD_DIR)/mod.elf
LDSCRIPT := mod.ld
ARCHFLAGS := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
             -fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset
WARNFLAGS := -Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable \
             -Wno-missing-braces -Wno-unsupported-floating-point-opt -Werror=section
CFLAGS   := $(ARCHFLAGS) $(WARNFLAGS) -D_LANGUAGE_C -nostdinc -ffunction-sections
CPPFLAGS := -DMIPS -DCOMPILER_GCC -DNON_MATCHING -DAVOID_UB -D_MIPS_SZINT=32 -D_MIPS_SZLONG=32 -D_MIPS_SZPTR=32 \
            -DOOT_VERSION=PAL_1_1 -DOOT_REVISION=1 -DOOT_REGION=REGION_EU -DPLATFORM_N64=1 -DPLATFORM_GC=0 -DPLATFORM_IQUE=0 \
            -DLIBULTRA_VERSION=LIBULTRA_VERSION_I -DLIBULTRA_PATCH=1 -DDEBUG_FEATURES=0 -DF3DEX_GBI_2 -DNDEBUG \
            -I include -I $(OOT) -I $(OOT)/build/pal-1.1 -I $(OOT)/extracted/pal-1.1 -I $(OOT)/include -I $(OOT)/include/libc
LDFLAGS  := -nostdlib -T $(LDSCRIPT) -Map $(BUILD_DIR)/mod.map --unresolved-symbols=ignore-all --emit-relocs -e 0 --no-nmagic -gc-sections

C_SRCS := $(wildcard src/*.c)
C_OBJS := $(addprefix $(BUILD_DIR)/, $(C_SRCS:.c=.o))

$(TARGET): $(C_OBJS) $(LDSCRIPT)
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(C_OBJS:.o=.d)
.PHONY: clean
