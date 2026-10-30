/* Generated from cesta-protocol/1. Declare the context and native hooks first. */

#ifdef CESTA_PROTOCOL_ia_commit_run_DEFINED
#error "duplicate protocol definition"
#endif
#define CESTA_PROTOCOL_ia_commit_run_DEFINED

#if defined(ia_commit_run) || \
    defined(IaCommitRun) || \
    defined(ctx) || \
    defined(cesta_remaining) || \
    defined(ia_commit_run_outcome) || \
    defined(ia_commit_run_declined) || \
    defined(ia_commit_run_answered) || \
    defined(ia_commit_run_done) || \
    defined(ia_commit_run_raised) || \
    defined(ia_commit_run_suspended) || \
    defined(ia_commit_run_resource_failure) || \
    defined(ia_commit_run_engine_failure) || \
    defined(ia_commit_seen) || \
    defined(ia_commit_footprint) || \
    defined(ia_commit_linear) || \
    defined(ia_commit_makes) || \
    defined(ia_commit_take) || \
    defined(ia_commit_make) || \
    defined(ia_commit_moved) || \
    defined(ia_commit_epoch_open) || \
    defined(ia_commit_epoch_abandon) || \
    defined(ia_commit_epoch_publish) || \
    defined(ia_commit_is_current) || \
    defined(ia_commit_is_formed) || \
    defined(ia_commit_is_linear) || \
    defined(ia_commit_is_permitted) || \
    defined(ia_commit_was_taken) || \
    defined(ia_commit_was_made) || \
    defined(ia_commit_has_moved) || \
    defined(ia_commit_epoch_is_open) || \
    defined(ia_commit_run_decline_reason) || \
    defined(ia_commit_run_decline_count) || \
    defined(ia_commit_run_counters) || \
    defined(ia_commit_run_count_decline) || \
    defined(ia_commit_run_counts) || \
    defined(ia_commit_run_reason) || \
    defined(ia_commit_run_COUNTED) || \
    defined(ia_commit_run_declined_after_ia_commit_makes_false) || \
    defined(ia_commit_run_declined_after_ia_commit_linear_false) || \
    defined(ia_commit_run_declined_after_ia_commit_footprint_false) || \
    defined(ia_commit_run_declined_after_ia_commit_seen_false)
#error "protocol identifier conflicts with a macro"
#endif

#ifndef ia_commit_run_CHECKED
#define ia_commit_run_CHECKED 0
#endif
#ifdef ia_commit_run_COUNT_DECLINE
#define ia_commit_run_COUNTED 1
#else
#define ia_commit_run_COUNTED 0
#define ia_commit_run_COUNT_DECLINE(ctx, reason) ((void)0)
#endif
#if ia_commit_run_CHECKED
#ifndef ia_commit_run_CHECK_FAILURE
#include <stdio.h>
#include <stdlib.h>
#define ia_commit_run_CHECK_FAILURE(ctx, kind, id, expected) \
    (fprintf(stderr, "protocol ia_commit_run: %s %s must be %s\n", \
             kind, id, (expected) ? "true" : "false"), abort())
#endif
#endif
#include <stdint.h>

typedef enum ia_commit_run_decline_reason {
    ia_commit_run_declined_after_ia_commit_makes_false,
    ia_commit_run_declined_after_ia_commit_linear_false,
    ia_commit_run_declined_after_ia_commit_footprint_false,
    ia_commit_run_declined_after_ia_commit_seen_false,
    ia_commit_run_decline_count
} ia_commit_run_decline_reason;

typedef struct ia_commit_run_counters {
    uint64_t declines[ia_commit_run_decline_count ? ia_commit_run_decline_count : 1];
} ia_commit_run_counters;

#if ia_commit_run_COUNTED
static inline void
ia_commit_run_count_decline(ia_commit_run_counters *ia_commit_run_counts, ia_commit_run_decline_reason ia_commit_run_reason)
{
    if (ia_commit_run_counts && (unsigned)ia_commit_run_reason < (unsigned)ia_commit_run_decline_count &&
        ia_commit_run_counts->declines[ia_commit_run_reason] != UINT64_MAX)
        ++ia_commit_run_counts->declines[ia_commit_run_reason];
}
#endif

typedef enum ia_commit_run_outcome {
    ia_commit_run_declined,
    ia_commit_run_answered,
    ia_commit_run_done,
    ia_commit_run_raised,
    ia_commit_run_suspended,
    ia_commit_run_resource_failure,
    ia_commit_run_engine_failure
} ia_commit_run_outcome;

static inline ia_commit_run_outcome
ia_commit_run(IaCommitRun *ctx)
{
    (void)ctx;
#if ia_commit_run_CHECKED
    if (ia_commit_epoch_is_open(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
        for (;;) {}
    }
#endif
    if (!ia_commit_seen(ctx)) {
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
        ia_commit_run_COUNT_DECLINE(ctx, ia_commit_run_declined_after_ia_commit_seen_false);
        return ia_commit_run_declined;
    }
#if ia_commit_run_CHECKED
    if (!ia_commit_is_current(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "current", 1);
        for (;;) {}
    }
#endif
    if (!ia_commit_footprint(ctx)) {
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
        ia_commit_run_COUNT_DECLINE(ctx, ia_commit_run_declined_after_ia_commit_footprint_false);
        return ia_commit_run_declined;
    }
#if ia_commit_run_CHECKED
    if (!ia_commit_is_formed(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "formed", 1);
        for (;;) {}
    }
#endif
    if (!ia_commit_linear(ctx)) {
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
        ia_commit_run_COUNT_DECLINE(ctx, ia_commit_run_declined_after_ia_commit_linear_false);
        return ia_commit_run_declined;
    }
#if ia_commit_run_CHECKED
    if (!ia_commit_is_linear(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "linear", 1);
        for (;;) {}
    }
#endif
    if (!ia_commit_makes(ctx)) {
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
        ia_commit_run_COUNT_DECLINE(ctx, ia_commit_run_declined_after_ia_commit_makes_false);
        return ia_commit_run_declined;
    }
#if ia_commit_run_CHECKED
    if (!ia_commit_is_current(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "current", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_is_formed(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "formed", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_is_linear(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "linear", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_is_permitted(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "permitted", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (ia_commit_epoch_is_open(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
        for (;;) {}
    }
#endif
    if (!ia_commit_epoch_open(ctx)) {
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
        return ia_commit_run_resource_failure;
    }
#if ia_commit_run_CHECKED
    if (!ia_commit_epoch_is_open(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_is_current(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "current", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_is_formed(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "formed", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_is_linear(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "linear", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_is_permitted(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "permitted", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_epoch_is_open(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 1);
        for (;;) {}
    }
#endif
    if (!ia_commit_take(ctx)) {
#if ia_commit_run_CHECKED
        if (!ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 1);
            for (;;) {}
        }
#endif
        ia_commit_epoch_abandon(ctx);
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
        return ia_commit_run_engine_failure;
    }
#if ia_commit_run_CHECKED
    if (!ia_commit_was_taken(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "taken", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_epoch_is_open(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 1);
        for (;;) {}
    }
#endif
    if (!ia_commit_make(ctx)) {
#if ia_commit_run_CHECKED
        if (!ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 1);
            for (;;) {}
        }
#endif
        ia_commit_epoch_abandon(ctx);
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
        return ia_commit_run_engine_failure;
    }
#if ia_commit_run_CHECKED
    if (!ia_commit_was_made(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "fact", "made", 1);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (!ia_commit_epoch_is_open(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 1);
        for (;;) {}
    }
#endif
    if (ia_commit_moved(ctx)) {
#if ia_commit_run_CHECKED
        if (!ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 1);
            for (;;) {}
        }
#endif
#if ia_commit_run_CHECKED
        if (!ia_commit_was_made(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "fact", "made", 1);
            for (;;) {}
        }
#endif
#if ia_commit_run_CHECKED
        if (!ia_commit_has_moved(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "fact", "moved", 1);
            for (;;) {}
        }
#endif
        ia_commit_epoch_publish(ctx);
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
#if ia_commit_run_CHECKED
        if (ia_commit_epoch_is_open(ctx)) {
            ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
            for (;;) {}
        }
#endif
        return ia_commit_run_answered;
    }
#if ia_commit_run_CHECKED
    if (!ia_commit_epoch_is_open(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 1);
        for (;;) {}
    }
#endif
    ia_commit_epoch_abandon(ctx);
#if ia_commit_run_CHECKED
    if (ia_commit_epoch_is_open(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
        for (;;) {}
    }
#endif
#if ia_commit_run_CHECKED
    if (ia_commit_epoch_is_open(ctx)) {
        ia_commit_run_CHECK_FAILURE(ctx, "own", "epoch", 0);
        for (;;) {}
    }
#endif
    return ia_commit_run_engine_failure;
}
