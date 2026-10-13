#ifndef CETTA_TELEGRAM_AGENT_H
#define CETTA_TELEGRAM_AGENT_H
#include "durable_host.h"
#include "telegram_action.h"

typedef struct {
    const CettaHostProgram *program; /* trusted durable:telegram_agent, version telegram-agent/1 */
    const char *source, *worker;
    const CettaTelegramActionPolicy *actions;
    int fuel;
    /* telegram-channel/1: how long an operator command waits for the agent
     * before the service replies itself. 0 selects 2000 ms. */
    uint32_t command_deadline_ms;
} CettaTelegramAgent;
bool cetta_telegram_agent_valid(const CettaTelegramAgent *agent);

/* Native service entry. Selects only fixed receive/reply/completion functions.
 * Derives exact key/prefix grants from recorded input/origin/intent, then checks
 * those routing records again in the ticket's fresh snapshot. Worker data
 * cannot select a program, actor, channel, scope or expression.
 * Returns an evaluated ticket; caller owns it and must require one COMPLETE
 * transition before accepting index 0. No result or a literal Empty means the
 * chat is busy or held; keep its input. Empty is not an acceptable transition.
 * No acceptance, dispatch or SQL occurs here.
 * Action permission failure selects a fixed rejection program on the same
 * ticket, only after a COMPLETE, denial-free result. Each evaluation has the
 * supplied fuel bound; at most two evaluations occur. Other incomplete or
 * malformed results remain uncommittable and require host diagnostics.
 * Host/evaluator thread, fixed HE extended profile; store/program outlive ticket.
 */
CettaDurableStatus cetta_telegram_agent_decide(CettaDurableStore *store,
    const CettaTelegramAgent *agent, const char *input_key, CettaHostDecision **out);

typedef enum { TELEGRAM_WAIT, TELEGRAM_SEND, TELEGRAM_WORKER, TELEGRAM_DONE, TELEGRAM_FOREIGN }
    CettaTelegramAdmission;
/* Fixed pure policy query, not a commit or dispatch. Observes the accepted
 * intent, actor, attempt and outcome together. An optional metadata watch
 * lets the scheduler wait without repeating evaluation. The native host must
 * serialize application transitions and admission on its one evaluator thread;
 * actor repair requires a stopped service. HTTP callbacks never change actors.
 * Dispatch still rechecks the immutable intent and attempt/outcome at claim.
 */
CettaDurableStatus cetta_telegram_agent_admit(CettaDurableStore *store,
    const CettaTelegramAgent *agent, const char *outbox_key, size_t watch_budget,
    CettaTelegramAdmission *admission, CettaDurableWatch **watch);
#endif
