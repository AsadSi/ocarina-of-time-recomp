TARGET = patches.elf

CC      ?= clang
LD      ?= ld.lld

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable -Wno-missing-braces -Wno-unsupported-floating-point-opt
OOT := ../../oot
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -DCOMPILER_GCC -DNON_MATCHING -DAVOID_UB \
			-D_MIPS_SZINT=32 -D_MIPS_SZLONG=32 -D_MIPS_SZPTR=32 \
			-DOOT_VERSION=PAL_1_1 -DOOT_REVISION=1 -DOOT_REGION=REGION_EU -DPLATFORM_N64=1 -DPLATFORM_GC=0 -DPLATFORM_IQUE=0 \
			-DLIBULTRA_VERSION=LIBULTRA_VERSION_I -DLIBULTRA_PATCH=1 -DDEBUG_FEATURES=0 -DF3DEX_GBI_2 -DNDEBUG \
			-I $(OOT) -I $(OOT)/build/pal-1.1 -I $(OOT)/extracted/pal-1.1 -I $(OOT)/include -I $(OOT)/include/libc -I../lib/rt64/include
LDFLAGS  := -nostdlib -T patches.ld -T syms.ld -Map patches.map --unresolved-symbols=ignore-all --emit-relocs

C_SRCS := $(wildcard *.c)
C_OBJS := $(C_SRCS:.c=.o)
C_DEPS := $(C_SRCS:.c=.d)

$(TARGET): $(C_OBJS) patches.ld syms.ld
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(C_OBJS): %.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(C_OBJS) $(TARGET) $(C_DEPS)

-include $(C_DEPS)

.PHONY: clean
