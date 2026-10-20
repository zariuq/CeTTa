#include "../src/generated/cetta_layout_words.generated.h"
#include "../src/binding/frame_identity.h"
#include "../src/match.h"
#include <stdio.h>

/* The generated accessors against an independent reading of each word.
 *
 * A frame identity holds its handle in the low 32 bits and its generation in
 * the 30 bits above; it is valid when both are nonzero and the top two bits
 * are zero, and an identity when its handle is also below the facility's
 * limit.  A binding context holds a frame identity in its low 62 bits and the
 * value's kind (0, 1 or 2) in the top two. */
int main(void) {
    uint64_t x = UINT64_C(0x9e3779b97f4a7c15);
    for (unsigned i = 0; i < 100000; ++i) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        /* A third of the words have small parts and clear top bits, as
         * issued identities do. */
        uint64_t word = (i % 3u) ? x : (x & UINT64_C(0x0000000f0000ffff));
        uint32_t handle = (uint32_t)word;
        uint32_t generation = (uint32_t)(word >> 32) & UINT32_C(0x3fffffff);
        unsigned top = (unsigned)(word >> 62);
        bool valid = top == 0u && handle != 0u && generation != 0u;
        CettaFrameIdentityParts parts;
        bool identity = cetta_frame_identity_parts((CettaFrameIdentity){word}, &parts);
        if (FrameIdentity_valid(word) != valid ||
            (uint32_t)FrameIdentity_handle(word) != handle ||
            parts.handle != handle || parts.generation != generation ||
            identity != (valid && handle < CETTA_FRAME_HANDLE_LIMIT))
            return 1;
        if (valid && (FrameIdentity_generation(word) != generation ||
                      FrameIdentity_encode(handle, generation) != word))
            return 2;

        uint64_t frame = word & UINT64_C(0x3fffffffffffffff);
        unsigned kind = (unsigned)(word >> 62);
        if (BindingContext_frame(word) != frame ||
            BindingContext_kind(word) != kind ||
            BindingContext_valid(word) != (kind <= 2u))
            return 3;
        if (kind <= 2u && BindingContext_encode(frame, kind) != word)
            return 4;

        /* A binding value keeps its frame identity and kind apart. */
        if (valid) {
            for (unsigned k = 0u; k <= 2u; k++) {
                BindingValue value = binding_value_make(
                    NULL, (CettaFrameIdentity){word}, (BindingValueKind)k);
                if (binding_value_epoch(value).word != word ||
                    binding_value_kind(value) != (BindingValueKind)k)
                    return 5;
            }
        }
    }
    /* Words the facility never issues are not identities. */
    if (FrameIdentity_valid(UINT64_C(0x4000000100000001)) ||
        FrameIdentity_valid(UINT64_C(0x8000000100000001)) ||
        FrameIdentity_valid(UINT64_C(0x0000000100000000)) ||
        FrameIdentity_valid(UINT64_C(0x0000000000000001)))
        return 6;
    puts("runtime layouts: frame identity and binding context accessors agree "
         "with an independent reading");
    return 0;
}
