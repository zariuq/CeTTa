#ifndef CETTA_TENSOR_SCREENING_H
#define CETTA_TENSOR_SCREENING_H

#include "tensor.h"

typedef struct CettaTensorScreenIndex CettaTensorScreenIndex;
typedef struct CettaTensorScreenReceipt CettaTensorScreenReceipt;

/* Owners, views and the sequential score schedule are retained by construction.
 * There is no constructor accepting externally supplied bounds/certificates.
 * Unsupported compaction or numerical admission keeps an exact full-scan index. */
CettaTensorStatus cetta_tensor_screen_index(CettaTensor *source,
    CettaTensorScreenIndex **out);
void cetta_tensor_screen_index_retain(CettaTensorScreenIndex *index);
void cetta_tensor_screen_index_release(void *index);
CettaTensor *cetta_tensor_screen_index_source(const CettaTensorScreenIndex *index);
bool cetta_tensor_screen_index_has_compact(const CettaTensorScreenIndex *index);
size_t cetta_tensor_screen_index_retained_bound(const CettaTensorScreenIndex *index);

/* Query, optional eligibility mask and k belong to the receipt, not to a later
 * caller-supplied assertion. Absent or false mask entries disable a row.
 * A certified receipt is an inclusive complete superset, never an exact rank.
 * A non-certified receipt retains every eligible row. Exact mask projection
 * may still elide ineligible rows, independently of numerical admission. */
CettaTensorStatus cetta_tensor_screen(CettaTensorScreenIndex *index,
    CettaTensor *query, CettaTensor *mask, size_t k, CettaTensorScreenReceipt **out);
void cetta_tensor_screen_receipt_release(void *receipt);
bool cetta_tensor_screen_is_certified(const CettaTensorScreenReceipt *receipt);
size_t cetta_tensor_screen_k(const CettaTensorScreenReceipt *receipt);
CettaTensor *cetta_tensor_screen_query(const CettaTensorScreenReceipt *receipt);
CettaTensor *cetta_tensor_screen_mask(const CettaTensorScreenReceipt *receipt);
CettaTensor *cetta_tensor_screen_source(const CettaTensorScreenReceipt *receipt);
CettaTensor *cetta_tensor_screen_candidates(const CettaTensorScreenReceipt *receipt);
CettaTensor *cetta_tensor_screen_lower(const CettaTensorScreenReceipt *receipt);
CettaTensor *cetta_tensor_screen_upper(const CettaTensorScreenReceipt *receipt);
size_t cetta_tensor_screen_retained_bound(const CettaTensorScreenReceipt *receipt);

/* Exact means the existing sequential matvec/top-k observation, including
 * ascending source-coordinate ties. A changed floating environment discards
 * pruning. Returned score positions align with returned source coordinates. */
CettaTensorStatus cetta_tensor_screen_rerank(const CettaTensorScreenReceipt *receipt,
    CettaTensor **indices, CettaTensor **scores);

#endif
