#include "patches.h"
#include "misc_funcs.h"

void DmaMgr_Init(void);
s32 DmaMgr_RequestSync(void* ram, uintptr_t vrom, size_t size);
void Main(void* arg);

DECLARE_SEGMENT(code)

// @recomp Patched to tell the runtime that the code segment is being loaded, so it can map the
// recompiled functions for it. Original: Main_ThreadEntry in src/boot/idle.c, minus its logging.
RECOMP_PATCH void Main_ThreadEntry(void* arg) {
    DmaMgr_Init();

    // @recomp Register the code segment before copying it in.
    recomp_load_overlays((u32)(uintptr_t)_codeSegmentRomStart, _codeSegmentStart, _codeSegmentRomEnd - _codeSegmentRomStart);

    DmaMgr_RequestSync(_codeSegmentStart, (uintptr_t)_codeSegmentRomStart, _codeSegmentRomEnd - _codeSegmentRomStart);
    bzero(_codeSegmentBssStart, _codeSegmentBssEnd - _codeSegmentBssStart);
    Main(arg);
}

// @recomp Patched to tell the runtime where each overlay is being loaded. Every actor, effect,
// game state, pause menu and player overlay goes through here.
// Original: Overlay_Load in src/libu64/loadfragment2_n64.c, minus its logging.
RECOMP_PATCH size_t Overlay_Load(uintptr_t vromStart, uintptr_t vromEnd, void* vramStart, void* vramEnd, void* allocatedRamAddr) {
    s32 size = vromEnd - vromStart;
    uintptr_t end = (uintptr_t)allocatedRamAddr + size;
    OverlayRelocationSection* ovlRelocs;

    // @recomp Register the overlay before copying it in.
    recomp_load_overlays(vromStart, allocatedRamAddr, size);

    DmaMgr_RequestSync(allocatedRamAddr, vromStart, size);

    // The overlay file ends with a 32-bit offset from its end back to the relocation section.
    ovlRelocs = (OverlayRelocationSection*)(end - ((s32*)end)[-1]);
    Overlay_Relocate(allocatedRamAddr, ovlRelocs, vramStart);

    if ((s32)ovlRelocs->bssSize != 0) {
        bzero((void*)end, (s32)ovlRelocs->bssSize);
    }

    size = (uintptr_t)vramEnd - (uintptr_t)vramStart;

    osWritebackDCache(allocatedRamAddr, size);
    osInvalICache(allocatedRamAddr, size);

    return size;
}

extern vu8 D_80121210;
extern vu8 D_80121211;

// @recomp Patched to report that no 64DD is attached without loading the 64DD library and probing
// the drive's registers, which do not exist here. Main then picks the normal cartridge heap layout.
// Original: func_800AD410 in src/code/code_n64dd_800AD410.c.
RECOMP_PATCH void func_800AD410(void) {
    D_80121210 = true;
    D_80121211 = false;
}
