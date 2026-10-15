#ifndef CETTA_TELEGRAM_INTAKE_H
#define CETTA_TELEGRAM_INTAKE_H
#include "durable_host.h"
#include "durable_inbox.h"

typedef struct CettaTelegramIntake CettaTelegramIntake;
/* Native host only. Recover the exact recorded poll and pin a trusted program
 * version. The store outlives this ticket. Calls run on the evaluator thread. */
CettaDurableStatus cetta_telegram_intake_begin(CettaDurableStore *store,
    const char *source, const char *program_version, CettaTelegramIntake **out);
void cetta_telegram_intake_free(CettaTelegramIntake *ticket);
/* Build fresh program spaces and evaluate telegram:poll on this ticket's own
 * observation. Config is host-owned immutable data, never a worker proposal:
 * (telegram:policy 1 (allowed-chat-ids...) allow-private (operator-ids...) (peer-ids...)).
 * Each ID list is bounded to 256. Previous outcomes are cleared on failure.
 * Program must include the HE stdlib and durable:telegram. No concurrent use. */
CettaDurableStatus cetta_telegram_intake_evaluate(CettaTelegramIntake *ticket,
    const CettaHostProgram *program, const Atom *config, int fuel);
/* Recovery has a separate fixed entry. The supplied host clock is persisted
 * against the pending response before evaluation (an existing sample is reused).
 * Retry/hold policy is pure MeTTa, and only this ticket's stored result commits.
 * A completed batch cannot be committed through the recovery entry. */
CettaDurableStatus cetta_telegram_intake_evaluate_recovery(CettaTelegramIntake *ticket,
    const CettaHostProgram *program, const Atom *config, int64_t sampled_utc_ms, int fuel);
CettaDurableStatus cetta_telegram_intake_commit_recovery(CettaTelegramIntake *ticket,
                                                       int64_t *revision);
const EvalOutcome *cetta_telegram_intake_outcome(const CettaTelegramIntake *ticket);
bool cetta_telegram_intake_held(const CettaTelegramIntake *ticket);
int64_t cetta_telegram_intake_revision(const CettaTelegramIntake *ticket);
/* Commit only the ticket's COMPLETE, unique batch result. Holds, retry hints,
 * denied/incomplete evaluations and malformed outputs cannot acknowledge input.
 * The first recorded classification wins across policy/config upgrades, but a
 * duplicate with different normalized provider data is still CORRUPT.
 * Recovery uses the separate evaluate_recovery/commit_recovery transition;
 * do not clear a held poll or re-poll in a loop. No intake function sends HTTP. */
CettaDurableStatus cetta_telegram_intake_commit(CettaTelegramIntake *ticket,
                                              CettaInboxCommit *out);
#endif
