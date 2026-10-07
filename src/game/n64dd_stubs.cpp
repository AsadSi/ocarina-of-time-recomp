#include <cstdio>

#include "recomp.h"

// libultra functions that only OoT's 64DD disk drive library calls. The recompiler leaves them for
// the runtime to implement and the runtime doesn't, since MM never calls them. The func_800AD410 patch
// keeps the 64DD library from ever loading, so these should be unreachable; say so if they're not.

extern "C" void osEPiWriteIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    fprintf(stderr, "osEPiWriteIo called from 64DD code that should be unreachable\n");
}

extern "C" void osLeoDiskInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    fprintf(stderr, "osLeoDiskInit called from 64DD code that should be unreachable\n");
}
