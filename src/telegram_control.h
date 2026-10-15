#ifndef CETTA_TELEGRAM_CONTROL_H
#define CETTA_TELEGRAM_CONTROL_H
#include "telegram_agent.h"
#include <sys/types.h>

typedef struct CettaTelegramControl CettaTelegramControl;
/* Optional operator endpoint, separate from worker CWP1. The listening private
 * Unix seqpacket socket is supervisor-owned; ownership transfers on success.
 * Same-UID local operators are trusted. No evaluator/worker capability exposes
 * this entry. Store and immutable agent configuration outlive it.
 * Native host thread only: records requests, never repairs actors directly.
 * CTC1 packets: status LANE | release ID LANE BATCH | receipt ID.
 * IDs are caller-chosen immutable retry identities. Replies are metadata only.
 * LANE is canonical CHAT.THREAD; permitted chats come from the agent policy.
 */
CettaDurableStatus cetta_telegram_control_new(CettaDurableStore *store,
    const CettaTelegramAgent *agent, int listener, uid_t uid,
    CettaTelegramControl **out);
/* Nonblocking bounded pump; eight peers, five-second idle/reply expiry. A
 * recorded reply means queued, not released. Query receipt for the decision.
 * Storage faults propagate; the owner must stop/recover instead of retrying
 * an ambiguous commit. Invalid clients do not stop the service. */
CettaDurableStatus cetta_telegram_control_step(CettaTelegramControl *control);
void cetta_telegram_control_free(CettaTelegramControl *control);
bool cetta_telegram_control_lane(const CettaTelegramAgent *agent, const char *lane);
#endif
