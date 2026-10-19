#include "../src/generated/cetta_layout_words.generated.h"
#include "../src/binding/frame_identity.h"
#include <stdio.h>

int main(void) {
    uint64_t x = UINT64_C(0x9e3779b97f4a7c15);
    for (unsigned i = 0; i < 100000; ++i) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        uint32_t base = (uint32_t)x;
        uint32_t epoch = (uint32_t)(x >> 32);
        if (VarId_base(x) != base || VarId_epoch(x) != epoch || VarId_encode(base, epoch) != x)
            return 1;
        uint32_t word = (uint32_t)x;
        CettaFrameIdentityParts parts;
        bool valid = cetta_frame_identity_parts(word, &parts);
        const uint32_t prefixes[] = {0, 2, 6, 14};
        const unsigned depths[] = {1, 2, 3, 4};
        const unsigned widths[] = {20, 24, 26, 26};
        const uint32_t starts[] = {0, 1048576, 17825792, 84934656};
        const uint32_t maxima[] = {2046, 62, 6, 2};
        uint32_t handle = 152043520 + (word & UINT32_C(0x0fffffff));
        uint32_t generation = 0, maximum = 0;
        bool expected = false;
        for (unsigned c = 0; c < 4; ++c) {
            if (word >> (32 - depths[c]) != prefixes[c]) continue;
            handle = starts[c] + (word & ((UINT32_C(1) << widths[c]) - 1));
            generation = (word >> widths[c]) & ((UINT32_C(1) << (32 - depths[c] - widths[c])) - 1);
            maximum = maxima[c];
            expected = handle != 0 && generation != 0 && generation <= maximum;
            break;
        }
        if (parts.handle != handle || parts.generation != generation || parts.generation_max != maximum ||
            valid != expected || FrameIdentity_valid(word) != expected) return 2;
        if (valid && FrameIdentity_encode(handle, generation) != word) return 3;
    }
    puts("runtime layouts: packed coordinates and independent frame cases agree");
    return 0;
}
