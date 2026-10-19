/* SPDX-License-Identifier: MIT */
#ifndef CETTA_SIWC_STORE_H
#define CETTA_SIWC_STORE_H
#include <jansson.h>
#include <stdbool.h>
#include <pthread.h>
#include <stddef.h>
typedef struct { int directory, file; pthread_mutex_t *local; } CettaSiwcLock;
bool cetta_siwc_id(const char *id);
int cetta_siwc_store_open(const char *path);
/* 1 acquired, 0 busy, -1 store error. The lock file is separate from the
 * atomically replaced record, and an in-process mutex complements fcntl. */
int cetta_siwc_store_lock(int directory, const char *id, CettaSiwcLock *out);
void cetta_siwc_store_unlock(CettaSiwcLock *lock);
json_t *cetta_siwc_store_read(int directory, const char *name);
bool cetta_siwc_store_write(int directory, const char *name, const json_t *value);
json_t *cetta_siwc_store_accounts(int directory);
json_t *cetta_siwc_public_account(const json_t *record);
#endif
