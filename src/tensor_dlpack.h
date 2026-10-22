#ifndef CETTA_TENSOR_DLPACK_H
#define CETTA_TENSOR_DLPACK_H

#include "tensor.h"
/* An optional provider uses the caller's DLPack headers, not a vendored ABI.
 * A nonstandard include location can define CETTA_DLPACK_HEADER. */
#ifndef CETTA_DLPACK_HEADER
#define CETTA_DLPACK_HEADER <dlpack/dlpack.h>
#endif
#include CETTA_DLPACK_HEADER

/* CPU, scalar lanes only. The caller supplies a live readable allocation
 * span containing every referenced byte, and excludes concurrent mutation
 * until copying returns. No external mutable storage is retained. */
CettaTensorStatus cetta_tensor_dlpack_copy(const DLTensor *source,
    const void *allocation_base,size_t allocation_bytes,CettaTensor **out);
/* These consume the managed object on entry, including refusal paths.
 * Their deleter is called exactly once when non-NULL. */
CettaTensorStatus cetta_tensor_dlpack_consume(DLManagedTensor *source,
    const void *allocation_base,size_t allocation_bytes,CettaTensor **out);
CettaTensorStatus cetta_tensor_dlpack_consume_versioned(DLManagedTensorVersioned *source,
    const void *allocation_base,size_t allocation_bytes,CettaTensor **out);
/* Always a fresh, independently writable copy. Sparse layouts must be
 * explicitly converted to dense first. The consumer owns the deleter. */
CettaTensorStatus cetta_tensor_dlpack_export_copy(const CettaTensor *source,
    DLManagedTensorVersioned **out);
CettaTensorStatus cetta_tensor_dlpack_export_legacy_copy(const CettaTensor *source,
    DLManagedTensor **out);

#endif
