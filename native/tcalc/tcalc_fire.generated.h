/* Generated from a declared Cesta protocol. Declare the context and native hooks first. */

#ifdef CESTA_PROTOCOL_tc_fire_DEFINED
#error "duplicate protocol definition"
#endif
#define CESTA_PROTOCOL_tc_fire_DEFINED

#if defined(tc_fire) || \
    defined(Explorer) || \
    defined(ctx) || \
    defined(cesta_remaining) || \
    defined(tc_fire_outcome) || \
    defined(tc_fire_declined) || \
    defined(tc_fire_answered) || \
    defined(tc_fire_done) || \
    defined(tc_fire_raised) || \
    defined(tc_fire_suspended) || \
    defined(tc_fire_resource_failure) || \
    defined(tc_fire_engine_failure) || \
    defined(tc_fire_after_tells) || \
    defined(tc_fire_tells) || \
    defined(tc_fire_checks) || \
    defined(tc_fire_checks_first) || \
    defined(tc_fire_loose) || \
    defined(tc_fire_admit) || \
    defined(tc_fire_admit_loose) || \
    defined(tc_fire_mark) || \
    defined(tc_fire_undo) || \
    defined(tc_fire_keep) || \
    defined(tc_fire_decline_reason) || \
    defined(tc_fire_decline_count) || \
    defined(tc_fire_counters) || \
    defined(tc_fire_count_decline) || \
    defined(tc_fire_counts) || \
    defined(tc_fire_reason) || \
    defined(tc_fire_COUNTED) || \
    defined(tc_fire_declined_after_tc_fire_loose_false) || \
    defined(tc_fire_declined_after_tc_fire_tells_false)
#error "protocol identifier conflicts with a macro"
#endif

#ifndef tc_fire_CHECKED
#define tc_fire_CHECKED 0
#endif
#ifdef tc_fire_COUNT_DECLINE
#define tc_fire_COUNTED 1
#else
#define tc_fire_COUNTED 0
#define tc_fire_COUNT_DECLINE(ctx, reason) ((void)0)
#endif
#if tc_fire_CHECKED
#error "checked protocol requires validators for every used fact and resource"
#endif
#include <stdint.h>

typedef enum tc_fire_decline_reason {
    tc_fire_declined_after_tc_fire_loose_false,
    tc_fire_declined_after_tc_fire_tells_false,
    tc_fire_decline_count
} tc_fire_decline_reason;

typedef struct tc_fire_counters {
    uint64_t declines[tc_fire_decline_count ? tc_fire_decline_count : 1];
} tc_fire_counters;

#if tc_fire_COUNTED
static inline void
tc_fire_count_decline(tc_fire_counters *tc_fire_counts, tc_fire_decline_reason tc_fire_reason)
{
    if (tc_fire_counts && (unsigned)tc_fire_reason < (unsigned)tc_fire_decline_count &&
        tc_fire_counts->declines[tc_fire_reason] != UINT64_MAX)
        ++tc_fire_counts->declines[tc_fire_reason];
}
#endif

typedef enum tc_fire_outcome {
    tc_fire_declined,
    tc_fire_answered,
    tc_fire_done,
    tc_fire_raised,
    tc_fire_suspended,
    tc_fire_resource_failure,
    tc_fire_engine_failure
} tc_fire_outcome;

static inline tc_fire_outcome
tc_fire(Explorer *ctx)
{
    (void)ctx;
    if (!tc_fire_mark(ctx)) {
        return tc_fire_resource_failure;
    }
    if (tc_fire_after_tells(ctx)) {
        if (!tc_fire_tells(ctx)) {
            tc_fire_undo(ctx);
            tc_fire_COUNT_DECLINE(ctx, tc_fire_declined_after_tc_fire_tells_false);
            return tc_fire_declined;
        }
        if (tc_fire_checks(ctx)) {
            if (tc_fire_admit(ctx)) {
                tc_fire_undo(ctx);
                return tc_fire_answered;
            }
            tc_fire_undo(ctx);
            return tc_fire_engine_failure;
        } else {
            if (!tc_fire_loose(ctx)) {
                tc_fire_undo(ctx);
                tc_fire_COUNT_DECLINE(ctx, tc_fire_declined_after_tc_fire_loose_false);
                return tc_fire_declined;
            }
            if (tc_fire_admit_loose(ctx)) {
                tc_fire_undo(ctx);
                return tc_fire_answered;
            }
            tc_fire_undo(ctx);
            return tc_fire_engine_failure;
        }
    } else {
        if (tc_fire_checks_first(ctx)) {
            if (!tc_fire_tells(ctx)) {
                tc_fire_undo(ctx);
                tc_fire_COUNT_DECLINE(ctx, tc_fire_declined_after_tc_fire_tells_false);
                return tc_fire_declined;
            }
            if (tc_fire_admit(ctx)) {
                tc_fire_undo(ctx);
                return tc_fire_answered;
            }
            tc_fire_undo(ctx);
            return tc_fire_engine_failure;
        } else {
            if (!tc_fire_tells(ctx)) {
                tc_fire_undo(ctx);
                tc_fire_COUNT_DECLINE(ctx, tc_fire_declined_after_tc_fire_tells_false);
                return tc_fire_declined;
            }
            if (!tc_fire_loose(ctx)) {
                tc_fire_undo(ctx);
                tc_fire_COUNT_DECLINE(ctx, tc_fire_declined_after_tc_fire_loose_false);
                return tc_fire_declined;
            }
            if (tc_fire_admit_loose(ctx)) {
                tc_fire_undo(ctx);
                return tc_fire_answered;
            }
            tc_fire_undo(ctx);
            return tc_fire_engine_failure;
        }
    }
}
