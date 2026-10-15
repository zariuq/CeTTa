#include "answer_storage.h"

#include <assert.h>
#include <stdlib.h>

bool cetta_answer_storage_account_retain(CettaAnswerStorageAccount *account) {
    if (!account || !account->shares || account->shares == SIZE_MAX)
        return false;
    account->shares++;
    return true;
}

void cetta_answer_storage_account_release(CettaAnswerStorageAccount *account) {
    if (!account)
        return;
    assert(account->shares > 0u);
    if (--account->shares == 0u) {
        assert(!account->head);
        free(account);
    }
}

CettaAnswerStorage *cetta_answer_storage_new(CettaAnswerStorageAccount **slot) {
    if (!slot)
        return NULL;
    if (!*slot) {
        *slot = cetta_malloc(sizeof(**slot));
        **slot = (CettaAnswerStorageAccount){.shares = 1u};
    }
    CettaAnswerStorageAccount *account = *slot;
    if (account->shares == SIZE_MAX)
        return NULL;
    CettaAnswerStorage *storage = cetta_malloc(sizeof(*storage));
    *storage = (CettaAnswerStorage){.shares = 1u, .account = account,
        .next = account->head};
    if (account->head)
        account->head->previous = storage;
    account->head = storage;
    account->shares++;
    arena_init(&storage->arena);
    arena_set_runtime_kind(&storage->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_hashcons(&storage->arena, NULL);
    arena_set_block_capacity(&storage->arena, 512u);
    return storage;
}

void cetta_answer_storage_prepare(CettaAnswerStorage *storage) {
    size_t capacity = storage->arena.block_capacity;
    if (capacity < 4096u && storage->arena.reserved_bytes >= capacity)
        arena_set_block_capacity(&storage->arena, capacity * 2u);
}

void cetta_answer_storage_retain(void *opaque) {
    CettaAnswerStorage *storage = opaque;
    assert(storage && storage->shares > 0u && storage->shares < SIZE_MAX);
    storage->shares++;
}

void cetta_answer_storage_release(void *opaque) {
    CettaAnswerStorage *storage = opaque;
    assert(storage && storage->shares > 0u);
    if (--storage->shares != 0u)
        return;
    CettaAnswerStorageAccount *account = storage->account;
    if (storage->previous)
        storage->previous->next = storage->next;
    else
        account->head = storage->next;
    if (storage->next)
        storage->next->previous = storage->previous;
    arena_free(&storage->arena);
    free(storage);
    cetta_answer_storage_account_release(account);
}

bool cetta_answer_storage_retain_into(CettaAnswerStorage *storage, Arena *receiver) {
    return storage && receiver && storage->shares < SIZE_MAX &&
        arena_retain_owner_with_storage(receiver, storage,
            cetta_answer_storage_retain, cetta_answer_storage_release,
            cetta_answer_storage_census);
}

void cetta_answer_storage_census(const void *raw, CettaStorageCensus *census) {
    const CettaAnswerStorage *storage = raw;
    if (!storage || !cetta_storage_object(census, storage, sizeof(*storage), CETTA_STORAGE_BLOCK)) return;
    (void)cetta_storage_object(census, storage->account, sizeof(*storage->account), CETTA_STORAGE_BLOCK);
    arena_storage_census(&storage->arena, census);
}

void cetta_answer_storage_account_census(const CettaAnswerStorageAccount *account,
        CettaStorageCensus *census) {
    if (!account || !cetta_storage_object(census, account, 0u, CETTA_STORAGE_VISITED)) return;
    (void)cetta_storage_object(census, account, sizeof(*account), CETTA_STORAGE_BLOCK);
    for (const CettaAnswerStorage *held = account->head; held; held = held->next) {
        if (!cetta_storage_object(census, held, 0u, CETTA_STORAGE_VISITED)) {
            cetta_storage_refuse(census, "AnswerStorageAccountCycle"); return;
        }
        cetta_answer_storage_census(held, census);
    }
}

bool cetta_answer_storage_account_usage(
    const CettaAnswerStorageAccount *account, CettaAnswerStorageUsage *out) {
    if (!out)
        return false;
    CettaAnswerStorageUsage usage = {0};
    if (account) {
        usage.control_bytes = sizeof(*account);
        for (const CettaAnswerStorage *held = account->head; held; held = held->next) {
            if (__builtin_add_overflow(usage.control_bytes,
                    sizeof(*held), &usage.control_bytes) ||
                __builtin_add_overflow(usage.arena_live_bytes,
                    arena_accounted_live_bytes(&held->arena), &usage.arena_live_bytes) ||
                __builtin_add_overflow(usage.arena_reserved_bytes,
                    held->arena.reserved_bytes, &usage.arena_reserved_bytes) ||
                __builtin_add_overflow(usage.collections, 1u, &usage.collections))
                return false;
        }
    }
    *out = usage;
    return true;
}
