// Force-included into PatchesLib. Recompiled functions are weak so that a patch can replace the
// original with the same name, which relies on the linker keeping the first weak definition it sees.
// Apple's linker doesn't, so make the patches strong: a strong definition wins on every linker.
#include "recomp.h"
#undef RECOMP_FUNC
#define RECOMP_FUNC __attribute__((noinline))
