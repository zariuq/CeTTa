#ifndef CETTA_ANSWER_STORAGE_H
#define CETTA_ANSWER_STORAGE_H

#include "atom.h"

typedef struct CettaAnswerStorage CettaAnswerStorage;
typedef struct {
    size_t shares;
    CettaAnswerStorage *head;
} CettaAnswerStorageAccount;

/* One append-only syntax collection. The builder owns the initial share;
 * receiver arenas may retain further shares. Do not reset or free this arena
 * directly. Storage independence does not establish context compatibility,
 * computation sharing, resource authority or answer occurrence identity.
 * Calls, including receiver-arena release, are serialized by the caller. */
struct CettaAnswerStorage {
    Arena arena;
    size_t shares;
    CettaAnswerStorageAccount *account;
    CettaAnswerStorage *previous;
    CettaAnswerStorage *next;
};

/* Lazily create the account's initial share. Each collection keeps its own
 * account share, so retiring the account's builder need not retire a loan. */
CettaAnswerStorage *cetta_answer_storage_new(CettaAnswerStorageAccount **account);
/* Retain the same census when a store replaces its collection builder. */
bool cetta_answer_storage_account_retain(CettaAnswerStorageAccount *account);
void cetta_answer_storage_account_release(CettaAnswerStorageAccount *account);
void cetta_answer_storage_prepare(CettaAnswerStorage *storage);
void cetta_answer_storage_retain(void *storage);
void cetta_answer_storage_release(void *storage);
bool cetta_answer_storage_retain_into(CettaAnswerStorage *storage, Arena *receiver);

/* Constructor-derived closure and stability summaries are required. Variables
 * still require freshening, and opaque resources require their own protocols.
 * A record from another arena cannot acquire this collection's protection. */
static inline bool cetta_answer_storage_can_borrow(
    const CettaAnswerStorage *storage, const Atom *record) {
    const uint32_t mask = ATOM_FLAG_TERM_STABLE | ATOM_FLAG_HAS_VARS |
        ATOM_FLAG_HAS_IDENTITY_GROUNDED | ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE;
    return storage && record && record->arena_id == storage->arena.identity &&
        (record->flags & ATOM_FLAG_ARENA_CLOSED) != 0u &&
        (record->flags & mask) == ATOM_FLAG_TERM_STABLE;
}

typedef struct {
    size_t control_bytes;
    size_t arena_live_bytes;
    size_t arena_reserved_bytes;
    size_t collections;
} CettaAnswerStorageUsage;

/* Count each collection once, including collections whose builder has been
 * retired while a receiver still holds them. Answer vectors and external
 * resource backing storage are accounted by their respective services. */
bool cetta_answer_storage_account_usage(
    const CettaAnswerStorageAccount *account, CettaAnswerStorageUsage *out);
/* Quiescent graph census, including retired builders still held by receivers.
 * A single collection retains its account header, not sibling collections. */
void cetta_answer_storage_account_census(
    const CettaAnswerStorageAccount *account, CettaStorageCensus *census);
void cetta_answer_storage_census(const void *storage, CettaStorageCensus *census);

#endif
